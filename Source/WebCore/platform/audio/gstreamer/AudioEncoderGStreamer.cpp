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
#include <wtf/PrintStream.h>
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
    GRefPtr<GstElement> m_encoder;
    GRefPtr<GstElement> m_capsFilter;
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
    , m_encoder(WTFMove(encoderElement))
{
    static Atomic<uint64_t> counter = 0;
    auto binName = makeString("audio-encoder-"_s, codecName, '-', counter.exchangeAdd(1));
    GRefPtr<GstElement> harnessedElement = gst_bin_new(binName.ascii().data());
    auto audioconvert = gst_element_factory_make("audioconvert", nullptr);
    auto audioresample = gst_element_factory_make("audioresample", nullptr);
    m_capsFilter = gst_element_factory_make("capsfilter", nullptr);
    gst_bin_add_many(GST_BIN_CAST(harnessedElement.get()), audioconvert, audioresample, m_encoder.get(), m_capsFilter.get(), nullptr);
    gst_element_link_many(audioconvert, audioresample, m_encoder.get(), m_capsFilter.get(), nullptr);
    auto sinkPad = adoptGRef(gst_element_get_static_pad(audioconvert, "sink"));
    gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("sink", sinkPad.get()));
    auto srcPad = adoptGRef(gst_element_get_static_pad(m_capsFilter.get(), "src"));
    gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("src", srcPad.get()));

    m_harness = GStreamerElementHarness::create(WTFMove(harnessedElement), [weakThis = WeakPtr { *this }, this](auto&, const GRefPtr<GstBuffer>& outputBuffer) {
        if (!weakThis)
            return;
        if (m_isClosed)
            return;

        if (auto meta = gst_buffer_get_audio_clipping_meta(outputBuffer.get())) {
            gst_printerrln("clipping-meta: %zu-%zu", meta->start, meta->end);
        }
        GST_TRACE_OBJECT(m_harness->element(), "phil %" GST_PTR_FORMAT, outputBuffer.get());
        // if (GST_BUFFER_IS_DISCONT(outputBuffer.get()))
        //     return;

        bool isKeyFrame = !GST_BUFFER_FLAG_IS_SET(outputBuffer.get(), GST_BUFFER_FLAG_DELTA_UNIT);
        GST_TRACE_OBJECT(m_harness->element(), "Notifying encoded%s frame", isKeyFrame ? " key" : "");
        GstMappedBuffer mappedBuffer(outputBuffer.get(), GST_MAP_READ);
        AudioEncoder::EncodedFrame encodedFrame {
            mappedBuffer.createVector(),
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

static unsigned getGstOpusEncFrameSizeFlag(const char* nick)
{
    static GFlagsClass* flagsClass = static_cast<GFlagsClass*>(g_type_class_ref(g_type_from_name("GstOpusEncFrameSize")));
    ASSERT(flagsClass);

    GFlagsValue* flag = g_flags_get_value_by_nick(flagsClass, nick);
    if (!flag)
        return 0;

    return flag->value;
}

String GStreamerInternalAudioEncoder::initialize(const AudioEncoder::Config& config)
{
    GST_DEBUG_OBJECT(m_harness->element(), "Initializing encoder for codec %s", m_codecName.ascii().data());
    GRefPtr<GstCaps> encoderCaps;
    if (m_codecName.startsWith("mp4a"_s)) {
        // FIXME: handle codec-specific parameters.
        encoderCaps = adoptGRef(gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 4, nullptr));
    } else if (m_codecName == "mp3"_s)
        encoderCaps = adoptGRef(gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, nullptr));
    else if (m_codecName == "opus"_s) {
        int channelMappingFamily = config.numberOfChannels <= 2 ? 0 : 1;
        encoderCaps = adoptGRef(gst_caps_new_simple("audio/x-opus", "channel-mapping-family", G_TYPE_INT, channelMappingFamily, nullptr));
        if (config.bitRate)
            g_object_set(m_encoder.get(), "bitrate", static_cast<int>(config.bitRate), nullptr);
        if (auto parameters = config.opusConfig) {
            GUniquePtr<char> name(gst_element_get_name(m_encoder.get()));
            if (g_str_has_prefix(name.get(), "opusenc")) {
                g_object_set(m_encoder.get(), "packet-loss-percentage", static_cast<int>(parameters->packetlossperc),
                    "inband-fec", static_cast<gboolean>(parameters->useinbandfec), "dtx", static_cast<gboolean>(parameters->usedtx), nullptr);
                GST_DEBUG_OBJECT(m_encoder.get(), "DTX enabled: %s", boolForPrinting(parameters->usedtx));
                gst_util_set_object_arg(G_OBJECT(m_encoder.get()), "bitrate-type", "cbr");

                // The frame-size property is expressed in milli-seconds, the value in parameters is
                // expressed in micro-seconds.
                auto frameSize = makeString(parameters->frameDuration / 1000);
                if (auto flag = getGstOpusEncFrameSizeFlag(frameSize.ascii().data()))
                    g_object_set(m_encoder.get(), "frame-size", flag, nullptr);
                else
                    GST_WARNING_OBJECT(m_encoder.get(), "Unhandled frameDuration: %" G_GUINT64_FORMAT, parameters->frameDuration);

                if (parameters->complexity && parameters->complexity <= 10)
                    g_object_set(m_encoder.get(), "complexity", static_cast<int>(parameters->complexity), nullptr);
            }
        }
    } else if (m_codecName == "alaw"_s)
        encoderCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-alaw"));
    else if (m_codecName == "ulaw"_s)
        encoderCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-mulaw"));
    else if (m_codecName == "flac"_s) {
        // FIXME: handle codec-specific parameters.
        encoderCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-flac"));
    } else if (m_codecName == "vorbis"_s)
        encoderCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-vorbis"));
    else if (m_codecName.startsWith("pcm-"_s)) {
        auto components = m_codecName.split('-');
        auto pcmFormat = components[1].convertToASCIILowercase();
        GstAudioFormat gstPcmFormat = GST_AUDIO_FORMAT_UNKNOWN;
        if (pcmFormat == "u8"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_U8;
        else if (pcmFormat == "s16"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_S16;
        else if (pcmFormat == "s24"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_S24;
        else if (pcmFormat == "s32"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_S32;
        else if (pcmFormat == "f32"_s)
            gstPcmFormat = GST_AUDIO_FORMAT_F32;
        else
            return makeString("Invalid LPCM codec format: "_s, pcmFormat);

        encoderCaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, gst_audio_format_to_string(gstPcmFormat),
            "layout", G_TYPE_STRING, "interleaved", nullptr));
    } else
        return makeString("Unsupported audio codec: "_s, m_codecName);

    gst_caps_set_simple(encoderCaps.get(), "rate", G_TYPE_INT, config.sampleRate, "channels", G_TYPE_INT, config.numberOfChannels, nullptr);
    g_object_set(m_capsFilter.get(), "caps", encoderCaps.get(), nullptr);

    g_object_set(m_encoder.get(), "hard-resync", TRUE, nullptr);

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

    auto gstAudioFrame = downcast<PlatformRawAudioDataGStreamer>(rawFrame.frame.get());
    auto sample = gstAudioFrame->sample();
    auto buffer = gst_sample_get_buffer(sample);
    // auto writableBuffer = adoptGRef(gst_buffer_make_writable(buffer));
    // GST_BUFFER_PTS(writableBuffer.get()) = m_timestamp;
    GST_BUFFER_PTS(buffer) = m_timestamp;

    // auto writableSample = adoptGRef(gst_sample_make_writable(sample));
    // gst_sample_set_buffer(writableSample.get(), writableBuffer.get());
    // return m_harness->pushSample(WTFMove(writableSample));
    return m_harness->pushSample(sample);
}

void GStreamerInternalAudioEncoder::flush(Function<void()>&& callback)
{
    m_harness->flush();
    m_postTaskCallback(WTFMove(callback));
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_CODECS) && USE(GSTREAMER)
