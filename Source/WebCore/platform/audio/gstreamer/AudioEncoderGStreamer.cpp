/*
 * Copyright (C) 2023 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "AudioEncoderGStreamer.h"

#if ENABLE(WEB_CODECS) && USE(GSTREAMER)

#include "GStreamerCodecUtilities.h"
#include "GStreamerCommon.h"
#include "GStreamerElementHarness.h"
#include "GStreamerRegistryScanner.h"
#include "PlatformRawAudioDataGStreamer.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/WorkQueue.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_audio_encoder_debug);
#define GST_CAT_DEFAULT webkit_audio_encoder_debug

static WorkQueue& gstEncoderWorkQueue()
{
    static NeverDestroyed<Ref<WorkQueue>> queue(WorkQueue::create("GStreamer AudioEncoder Queue"));
    return queue.get();
}

class GStreamerInternalAudioEncoder : public ThreadSafeRefCounted<GStreamerInternalAudioEncoder>
    , public CanMakeWeakPtr<GStreamerInternalAudioEncoder, WeakPtrFactoryInitialization::Eager> {
    WTF_MAKE_FAST_ALLOCATED;

public:
    static Ref<GStreamerInternalAudioEncoder> create(const String& codecName, AudioEncoder::OutputCallback&& outputCallback, AudioEncoder::PostTaskCallback&& postTaskCallback, GRefPtr<GstElement>&& element) { return adoptRef(*new GStreamerInternalAudioEncoder(codecName, WTFMove(outputCallback), WTFMove(postTaskCallback), WTFMove(element))); }
    ~GStreamerInternalAudioEncoder() = default;

    String initialize(const AudioEncoder::Config&);
    void postTask(Function<void()>&& task) { m_postTaskCallback(WTFMove(task)); }
    bool encode(AudioEncoder::RawFrame&&);
    void flush(Function<void()>&&);
    void close() { m_isClosed = true; }

    const RefPtr<GStreamerElementHarness> harness() const { return m_harness; }
    bool isClosed() const { return m_isClosed; }

private:
    GStreamerInternalAudioEncoder(const String& codecName, AudioEncoder::OutputCallback&&, AudioEncoder::PostTaskCallback&&, GRefPtr<GstElement>&&);

    const String m_codecName;
    AudioEncoder::OutputCallback m_outputCallback;
    AudioEncoder::PostTaskCallback m_postTaskCallback;
    int64_t m_timestamp { 0 };
    std::optional<uint64_t> m_duration;
    bool m_isClosed { false };
    bool m_isInitialized { false };
    RefPtr<GStreamerElementHarness> m_harness;
};

void GStreamerAudioEncoder::create(const String& codecName, const AudioEncoder::Config& config, CreateCallback&& callback, DescriptionCallback&& descriptionCallback, OutputCallback&& outputCallback, PostTaskCallback&& postTaskCallback)
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_audio_encoder_debug, "webkitaudioencoder", 0, "WebKit WebCodecs Audio Encoder");
    });

    GRefPtr<GstElement> element;
    if (codecName.startsWith("pcm-"_s)) {
        auto components = codecName.split('-');
        if (components.size() != 2) {
            gstEncoderWorkQueue().dispatch([callback = WTFMove(callback), codecName]() mutable {
                callback(makeUnexpected(makeString("Invalid LPCM codec string: "_s, codecName)));
            });
            return;
        }
        element = gst_element_factory_make("identity", nullptr);
    } else {
        auto& scanner = GStreamerRegistryScanner::singleton();
        auto lookupResult = scanner.isCodecSupported(GStreamerRegistryScanner::Configuration::Encoding, codecName);
        if (!lookupResult) {
            gstEncoderWorkQueue().dispatch([callback = WTFMove(callback), codecName]() mutable {
                callback(makeUnexpected(makeString("No GStreamer encoder found for codec ", codecName)));
            });
            return;
        }
        element = gst_element_factory_create(lookupResult.factory.get(), nullptr);
    }
    auto encoder = makeUniqueRef<GStreamerAudioEncoder>(codecName, WTFMove(outputCallback), WTFMove(postTaskCallback), WTFMove(element));
    auto error = encoder->initialize(config);
    gstEncoderWorkQueue().dispatch([callback = WTFMove(callback), descriptionCallback = WTFMove(descriptionCallback), encoder = WTFMove(encoder), error]() mutable {
        auto internalEncoder = encoder->m_internalEncoder;
        internalEncoder->postTask([callback = WTFMove(callback), descriptionCallback = WTFMove(descriptionCallback), encoder = WTFMove(encoder), error]() mutable {
            if (!error.isEmpty()) {
                GST_WARNING("Error creating encoder: %s", error.ascii().data());
                callback(makeUnexpected(makeString("GStreamer encoding initialization failed with error: ", error)));
                return;
            }

            GST_DEBUG("Encoder created");
            callback(UniqueRef<AudioEncoder> { WTFMove(encoder) });

            AudioEncoder::ActiveConfiguration configuration;
            descriptionCallback(WTFMove(configuration));
        });
    });
}

GStreamerAudioEncoder::GStreamerAudioEncoder(const String& codecName, OutputCallback&& outputCallback, PostTaskCallback&& postTaskCallback, GRefPtr<GstElement>&& element)
    : m_internalEncoder(GStreamerInternalAudioEncoder::create(codecName, WTFMove(outputCallback), WTFMove(postTaskCallback), WTFMove(element)))
{
}

GStreamerAudioEncoder::~GStreamerAudioEncoder()
{
    GST_DEBUG_OBJECT(m_internalEncoder->harness()->element(), "Destroying");
    close();
}

String GStreamerAudioEncoder::initialize(const AudioEncoder::Config& config)
{
    return m_internalEncoder->initialize(config);
}

void GStreamerAudioEncoder::encode(RawFrame&& frame, EncodeCallback&& callback)
{
    gstEncoderWorkQueue().dispatch([frame = WTFMove(frame), encoder = m_internalEncoder, callback = WTFMove(callback)]() mutable {
        auto result = encoder->encode(WTFMove(frame));
        if (encoder->isClosed())
            return;

        String resultString;
        if (result)
            encoder->harness()->processOutputBuffers();
        else
            resultString = "Encoding failed"_s;
        callback(WTFMove(resultString));
    });
}

void GStreamerAudioEncoder::flush(Function<void()>&& callback)
{
    gstEncoderWorkQueue().dispatch([encoder = m_internalEncoder, callback = WTFMove(callback)]() mutable {
        encoder->flush(WTFMove(callback));
    });
}

void GStreamerAudioEncoder::reset()
{
    GST_DEBUG_OBJECT(m_internalEncoder->harness()->element(), "Resetting");
    m_internalEncoder->close();
}

void GStreamerAudioEncoder::close()
{
    GST_DEBUG_OBJECT(m_internalEncoder->harness()->element(), "Closing");
    m_internalEncoder->close();
}

GStreamerInternalAudioEncoder::GStreamerInternalAudioEncoder(const String& codecName, AudioEncoder::OutputCallback&& outputCallback, AudioEncoder::PostTaskCallback&& postTaskCallback, GRefPtr<GstElement>&& encoderElement)
    : m_codecName(codecName)
    , m_outputCallback(WTFMove(outputCallback))
    , m_postTaskCallback(WTFMove(postTaskCallback))
{
#if 0
    GRefPtr<GstElement> element = gst_element_factory_make("fixme", nullptr);
    if (codecName.startsWith("mp4a"_s)) {
        // m_inputCaps = adoptGRef(gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 4, "channels", G_TYPE_INT, config.numberOfChannels, nullptr));
        // auto codecData = wrapSpanData(config.description);
        // if (codecData)
        //     gst_caps_set_simple(m_inputCaps.get(), "codec_data", GST_TYPE_BUFFER, codecData.get(), "stream-format", G_TYPE_STRING, "raw", nullptr);
        // else
        //     gst_caps_set_simple(m_inputCaps.get(), "stream-format", G_TYPE_STRING, "adts", nullptr);
    } else if (codecName == "mp3"_s) {
        // m_inputCaps = adoptGRef(gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, "rate", G_TYPE_INT, config.sampleRate, "channels", G_TYPE_INT, config.numberOfChannels, "parsed", G_TYPE_BOOLEAN, TRUE, nullptr));
    } else if (codecName == "opus"_s) {
        // int channelMappingFamily = config.numberOfChannels <= 2 ? 0 : 1;
        // m_inputCaps = adoptGRef(gst_caps_new_simple("audio/x-opus", "channel-mapping-family", G_TYPE_INT, channelMappingFamily, nullptr));
        // m_header = wrapSpanData(config.description);
        // if (m_header)
        //     parser = "opusparse";
    } else if (codecName == "alaw"_s) {
        // m_inputCaps = adoptGRef(gst_caps_new_simple("audio/x-alaw", "rate", G_TYPE_INT, config.sampleRate, "channels", G_TYPE_INT, config.numberOfChannels, nullptr));
    } else if (codecName == "ulaw"_s) {
        // m_inputCaps = adoptGRef(gst_caps_new_simple("audio/x-mulaw", "rate", G_TYPE_INT, config.sampleRate, "channels", G_TYPE_INT, config.numberOfChannels, nullptr));
    } else if (codecName == "flac"_s) {
        // m_header = wrapSpanData(config.description);
        // if (!m_header) {
        //     GST_WARNING("Decoder config description for flac codec is mandatory");
        //     return;
        // }
        // parser = "flacparse";
        // m_inputCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-flac"));
    } else if (codecName == "vorbis"_s) {
        // m_header = wrapSpanData(config.description);
        // if (!m_header) {
        //     GST_WARNING("Decoder config description for vorbis codec is mandatory");
        //     return;
        // }
        // parser = "oggparse";
        // m_inputCaps = adoptGRef(gst_caps_new_empty_simple("application/ogg"));
    } else if (codecName.startsWith("pcm-"_s)) {
        // auto components = codecName.split('-');
        // auto pcmFormat = components[1].convertToASCIILowercase();
        // GstAudioFormat gstPcmFormat = GST_AUDIO_FORMAT_UNKNOWN;
        // if (pcmFormat == "u8"_s)
        //     gstPcmFormat = GST_AUDIO_FORMAT_U8;
        // else if (pcmFormat == "s16"_s)
        //     gstPcmFormat = GST_AUDIO_FORMAT_S16;
        // else if (pcmFormat == "s24"_s)
        //     gstPcmFormat = GST_AUDIO_FORMAT_S24;
        // else if (pcmFormat == "s32"_s)
        //     gstPcmFormat = GST_AUDIO_FORMAT_S32;
        // else if (pcmFormat == "f32"_s)
        //     gstPcmFormat = GST_AUDIO_FORMAT_F32;
        // else {
        //     GST_WARNING("Invalid LPCM codec format: %s", pcmFormat.ascii().data());
        //     return;
        // }
        // m_inputCaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, gst_audio_format_to_string(gstPcmFormat),
        //     "rate", G_TYPE_INT, config.sampleRate, "channels", G_TYPE_INT, config.numberOfChannels,
        //     "layout", G_TYPE_STRING, "interleaved", nullptr));
        // parser = "rawaudioparse";
    } else
        return;
#endif

    GRefPtr<GstElement> harnessedElement = gst_bin_new(nullptr);
    auto audioconvert = gst_element_factory_make("audioconvert", nullptr);
    auto audioresample = gst_element_factory_make("audioresample", nullptr);
    gst_bin_add_many(GST_BIN_CAST(harnessedElement.get()), audioconvert, audioresample, encoderElement.get(), nullptr);
    gst_element_link_many(audioconvert, audioresample, encoderElement.get(), nullptr);
    auto sinkPad = adoptGRef(gst_element_get_static_pad(audioconvert, "sink"));
    gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("sink", sinkPad.get()));
    auto srcPad = adoptGRef(gst_element_get_static_pad(encoderElement.get(), "src"));
    gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("src", srcPad.get()));

    m_harness = GStreamerElementHarness::create(WTFMove(harnessedElement), [weakThis = WeakPtr { *this }, this](auto&, const GRefPtr<GstBuffer>& outputBuffer) {
        if (!weakThis)
            return;
        if (m_isClosed)
            return;

        bool isKeyFrame = !GST_BUFFER_FLAG_IS_SET(outputBuffer.get(), GST_BUFFER_FLAG_DELTA_UNIT);
        GST_TRACE_OBJECT(m_harness->element(), "Notifying encoded%s frame", isKeyFrame ? " key" : "");
        GstMappedBuffer encodedImage(outputBuffer.get(), GST_MAP_READ);
        AudioEncoder::EncodedFrame encodedFrame {
            Vector<uint8_t> { std::span<const uint8_t> { encodedImage.data(), encodedImage.size() } },
            isKeyFrame, m_timestamp, m_duration,
        };

        m_postTaskCallback([weakThis = WeakPtr { *this }, this, encodedFrame = WTFMove(encodedFrame)]() mutable {
            if (!weakThis)
                return;
            if (m_isClosed)
                return;
            m_outputCallback({ WTFMove(encodedFrame) });
        });
    });
}

String GStreamerInternalAudioEncoder::initialize(const AudioEncoder::Config& config)
{
    GST_DEBUG_OBJECT(m_harness->element(), "Initializing encoder for codec %s", m_codecName.ascii().data());
    GRefPtr<GstCaps> encoderCaps;
    // if (m_codecName == "vp8"_s)
    //     encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-vp8"));
    // else if (m_codecName.startsWith("vp09"_s)) {
    //     encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-vp9"));
    //     if (auto profileId = GStreamerCodecUtilities::parseVP9Profile(m_codecName)) {
    //         auto profile = makeString(profileId);
    //         gst_caps_set_simple(encoderCaps.get(), "profile", G_TYPE_STRING, profile.ascii().data(), nullptr);
    //     }
    // } else if (m_codecName.startsWith("avc1"_s)) {
    //     encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-h264"));
    //     auto [profile, level] = GStreamerCodecUtilities::parseH264ProfileAndLevel(m_codecName);
    //     if (profile)
    //         gst_caps_set_simple(encoderCaps.get(), "profile", G_TYPE_STRING, profile, nullptr);
    //     // FIXME: Set level on caps too?
    //     UNUSED_VARIABLE(level);
    // } else if (m_codecName.startsWith("av01"_s)) {
    //     // FIXME: parse codec parameters.
    //     encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-av1"));
    // } else if (m_codecName.startsWith("hvc1"_s) || m_codecName.startsWith("hev1"_s)) {
    //     encoderCaps = adoptGRef(gst_caps_new_empty_simple("video/x-h265"));
    //     if (const char* profile = GStreamerCodecUtilities::parseHEVCProfile(m_codecName))
    //         gst_caps_set_simple(encoderCaps.get(), "profile", G_TYPE_STRING, profile, nullptr);
    // } else
    return makeString("Unsupported outgoing video encoding: "_s, m_codecName);

    // if (config.width)
    //     gst_caps_set_simple(encoderCaps.get(), "width", G_TYPE_INT, static_cast<int>(config.width), nullptr);
    // if (config.height)
    //     gst_caps_set_simple(encoderCaps.get(), "height", G_TYPE_INT, static_cast<int>(config.height), nullptr);

    // FIXME: Propagate config.frameRate to caps?

    // if (!videoEncoderSetFormat(WEBKIT_VIDEO_ENCODER(m_harness->element()), WTFMove(encoderCaps)))
    //     return "Unable to set encoder format"_s;

    if (config.bitRate)
        g_object_set(m_harness->element(), "bitrate", static_cast<uint32_t>(config.bitRate / 1024), nullptr);
    m_isInitialized = true;
    return emptyString();
}

bool GStreamerInternalAudioEncoder::encode(AudioEncoder::RawFrame&& rawFrame)
{
    if (!m_isInitialized) {
        GST_WARNING_OBJECT(m_harness->element(), "Encoder not initialized");
        return true;
    }

    m_timestamp = rawFrame.timestamp;
    m_duration = rawFrame.duration;

    // if (shouldGenerateKeyFrame) {
    //     GST_INFO_OBJECT(m_harness->element(), "Requesting key-frame!");
    //     m_harness->pushEvent(gst_video_event_new_downstream_force_key_unit(GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, FALSE, 1));
    // }

    auto gstAudioFrame = downcast<PlatformRawAudioDataGStreamer>(rawFrame.frame.get());
    auto sample = gstAudioFrame->sample();
    auto buffer = gst_sample_get_buffer(sample);
    auto writableBuffer = adoptGRef(gst_buffer_make_writable(buffer));
    GST_BUFFER_PTS(writableBuffer.get()) = m_timestamp;

    auto writableSample = adoptGRef(gst_sample_make_writable(sample));
    gst_sample_set_buffer(writableSample.get(), writableBuffer.get());
    return m_harness->pushSample(WTFMove(writableSample));
}

void GStreamerInternalAudioEncoder::flush(Function<void()>&& callback)
{
    m_harness->flush();
    m_postTaskCallback(WTFMove(callback));
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_CODECS) && USE(GSTREAMER)
