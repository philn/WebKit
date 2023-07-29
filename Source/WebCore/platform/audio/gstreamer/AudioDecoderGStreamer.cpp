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
#include "AudioDecoderGStreamer.h"

#if ENABLE(WEB_CODECS) && USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "GStreamerElementHarness.h"
#include "GStreamerRegistryScanner.h"
#include "PlatformRawAudioDataGStreamer.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/UniqueRef.h>
#include <wtf/WorkQueue.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_audio_decoder_debug);
#define GST_CAT_DEFAULT webkit_audio_decoder_debug

static WorkQueue& gstDecoderWorkQueue()
{
    static NeverDestroyed<Ref<WorkQueue>> queue(WorkQueue::create("GStreamer AudioDecoder Queue"));
    return queue.get();
}

class GStreamerInternalAudioDecoder : public ThreadSafeRefCounted<GStreamerInternalAudioDecoder> {
public:
    static Ref<GStreamerInternalAudioDecoder> create(const String& codecName, const AudioDecoder::Config& config, AudioDecoder::OutputCallback&& outputCallback, AudioDecoder::PostTaskCallback&& postTaskCallback, GRefPtr<GstElement>&& element)
    {
        return adoptRef(*new GStreamerInternalAudioDecoder(codecName, config, WTFMove(outputCallback), WTFMove(postTaskCallback), WTFMove(element)));
    }
    ~GStreamerInternalAudioDecoder() = default;

    void postTask(Function<void()>&& task) { m_postTaskCallback(WTFMove(task)); }
    void decode(std::span<const uint8_t>, bool isKeyFrame, int64_t timestamp, std::optional<uint64_t> duration,  AudioDecoder::DecodeCallback&&);
    void flush(Function<void()>&&);
    void close() { m_isClosed = true; }

    bool isStarted() const { return m_harness->isStarted(); }

private:
    GStreamerInternalAudioDecoder(const String& codecName, const AudioDecoder::Config&, AudioDecoder::OutputCallback&&, AudioDecoder::PostTaskCallback&&, GRefPtr<GstElement>&&);

    AudioDecoder::OutputCallback m_outputCallback;
    AudioDecoder::PostTaskCallback m_postTaskCallback;

    RefPtr<GStreamerElementHarness> m_harness;
    bool m_isClosed { false };
};

bool GStreamerAudioDecoder::create(const String& codecName, const Config& config, CreateCallback&& callback, OutputCallback&& outputCallback, PostTaskCallback&& postTaskCallback)
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_audio_decoder_debug, "webkitaudiodecoder", 0, "WebKit WebCodecs Audio Decoder");
    });

    auto& scanner = GStreamerRegistryScanner::singleton();
    auto lookupResult = scanner.isCodecSupported(GStreamerRegistryScanner::Configuration::Decoding, codecName);
    if (!lookupResult) {
        GST_WARNING("No decoder found for codec %s", codecName.ascii().data());
        return false;
    }

    GRefPtr<GstElement> element = gst_element_factory_create(lookupResult.factory.get(), nullptr);
    auto decoder = makeUniqueRef<GStreamerAudioDecoder>(codecName, config, WTFMove(outputCallback), WTFMove(postTaskCallback), WTFMove(element));
    if (!decoder->m_internalDecoder->isStarted()) {
        GST_WARNING("Internal video decoder failed to configure for codec %s", codecName.ascii().data());
        return false;
    }

    gstDecoderWorkQueue().dispatch([callback = WTFMove(callback), decoder = WTFMove(decoder)]() mutable {
        auto internalDecoder = decoder->m_internalDecoder;
        internalDecoder->postTask([callback = WTFMove(callback), decoder = WTFMove(decoder)]() mutable {
            GST_DEBUG("Audio decoder created");
            callback(UniqueRef<AudioDecoder> { WTFMove(decoder) });
        });
    });

    return true;
}

GStreamerAudioDecoder::GStreamerAudioDecoder(const String& codecName, const Config& config, OutputCallback&& outputCallback, PostTaskCallback&& postTaskCallback, GRefPtr<GstElement>&& element)
    : m_internalDecoder(GStreamerInternalAudioDecoder::create(codecName, config, WTFMove(outputCallback), WTFMove(postTaskCallback), WTFMove(element)))
{
}

GStreamerAudioDecoder::~GStreamerAudioDecoder()
{
    close();
}

void GStreamerAudioDecoder::decode(EncodedData&& data, DecodeCallback&& callback)
{
    gstDecoderWorkQueue().dispatch([value = Vector<uint8_t> { data.data }, isKeyFrame = data.isKeyFrame, timestamp = data.timestamp, duration = data.duration, decoder = m_internalDecoder, callback = WTFMove(callback)]() mutable {
        decoder->decode({ value.data(), value.size() }, isKeyFrame, timestamp, duration, WTFMove(callback));
    });
}

void GStreamerAudioDecoder::flush(Function<void()>&& callback)
{
    gstDecoderWorkQueue().dispatch([decoder = m_internalDecoder, callback = WTFMove(callback)]() mutable {
        decoder->flush(WTFMove(callback));
    });
}

void GStreamerAudioDecoder::reset()
{
    m_internalDecoder->close();
}

void GStreamerAudioDecoder::close()
{
    m_internalDecoder->close();
}

GStreamerInternalAudioDecoder::GStreamerInternalAudioDecoder(const String& codecName, const AudioDecoder::Config& config, AudioDecoder::OutputCallback&& outputCallback, AudioDecoder::PostTaskCallback&& postTaskCallback, GRefPtr<GstElement>&& element)
    : m_outputCallback(WTFMove(outputCallback))
    , m_postTaskCallback(WTFMove(postTaskCallback))
{
    configureAudioDecoderForHarnessing(element);

    GST_DEBUG_OBJECT(element.get(), "Configuring decoder for codec %s", codecName.ascii().data());
    GRefPtr<GstCaps> inputCaps;
    const char* parser = nullptr;
    if (codecName.startsWith("mp4a"_s)) {
        parser = "aacparse";
        inputCaps = adoptGRef(gst_caps_new_empty_simple("audio/mpeg"));
    } else if (codecName.startsWith("mp3"_s)) {
        // parser = "mpegaudioparse";
        inputCaps = adoptGRef(gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, nullptr));
    } else if (codecName.startsWith("opus"_s)) {
        // parser = "opusparse";
        inputCaps = adoptGRef(gst_caps_new_empty_simple("audio/x-opus"));
    } else if (codecName.startsWith("alaw"_s)) {
        inputCaps = adoptGRef(gst_caps_new_simple("audio/x-alaw", "rate", G_TYPE_INT, config.sampleRate, "channels", G_TYPE_INT, config.numberOfChannels, nullptr));
    } else if (codecName.startsWith("mulaw"_s)) {
        inputCaps = adoptGRef(gst_caps_new_simple("audio/x-mulaw", "rate", G_TYPE_INT, config.sampleRate, "channels", G_TYPE_INT, config.numberOfChannels, nullptr));
    }

    //     Vector<uint8_t> data { config.description };
    //     if (!data.isEmpty()) {
    //         auto bufferSize = data.size();
    //         auto bufferData = data.data();
    //         auto* codecData = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, bufferData, bufferSize, 0, bufferSize, new Vector<uint8_t>(WTFMove(data)), [](gpointer data) {
    //             delete static_cast<Vector<uint8_t>*>(data);
    //         });

    //         gst_caps_set_simple(inputCaps.get(), "codec_data", GST_TYPE_BUFFER, codecData, nullptr);
    //     }

    GRefPtr<GstElement> harnessedElement;
    auto* factory = gst_element_get_factory(element.get());
    if (parser && !gst_element_factory_can_sink_all_caps(factory, inputCaps.get())) {
        // The decoder won't accept the input caps, so put a parser in front.
        auto* parserElement = makeGStreamerElement(parser, nullptr);
        if (!parserElement) {
            GST_WARNING_OBJECT(element.get(), "Required parser %s not found, decoding will fail", parser);
            return;
        }
        harnessedElement = gst_bin_new(nullptr);
        gst_bin_add_many(GST_BIN_CAST(harnessedElement.get()), parserElement, element.get(), nullptr);
        gst_element_link(parserElement, element.get());
        auto sinkPad = adoptGRef(gst_element_get_static_pad(parserElement, "sink"));
        gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("sink", sinkPad.get()));
        auto srcPad = adoptGRef(gst_element_get_static_pad(element.get(), "src"));
        gst_element_add_pad(harnessedElement.get(), gst_ghost_pad_new("src", srcPad.get()));
    } else
        harnessedElement = WTFMove(element);

    m_harness = GStreamerElementHarness::create(WTFMove(harnessedElement), [protectedThis = Ref { *this }, this](auto& stream, const GRefPtr<GstBuffer>& outputBuffer) {
        if (protectedThis->m_isClosed)
            return;

        GST_TRACE_OBJECT(m_harness->element(), "Got frame with PTS: %" GST_TIME_FORMAT, GST_TIME_ARGS(GST_BUFFER_PTS(outputBuffer.get())));

        // if (m_presentationSize.isEmpty())
        //     m_presentationSize = getAudioResolutionFromCaps(stream.outputCaps().get()).value_or(FloatSize { 0, 0 });

        m_postTaskCallback([protectedThis = Ref { *this }, this, outputBuffer = GRefPtr<GstBuffer>(outputBuffer), outputCaps = stream.outputCaps()]() mutable {
            if (protectedThis->m_isClosed)
                return;

            auto sample = adoptGRef(gst_sample_new(outputBuffer.get(), outputCaps.get(), nullptr, nullptr));
            auto audioData = PlatformRawAudioDataGStreamer::create(WTFMove(sample));
            m_outputCallback(AudioDecoder::DecodedData { WTFMove(audioData), // static_cast<int64_t>(GST_BUFFER_PTS(outputBuffer.get())), GST_BUFFER_DURATION(outputBuffer.get())
                });
        });
    });
    m_harness->start(WTFMove(inputCaps));
}

void GStreamerInternalAudioDecoder::decode(std::span<const uint8_t> frameData, bool isKeyFrame, int64_t timestamp, std::optional<uint64_t> duration, AudioDecoder::DecodeCallback&& callback)
{
    GST_DEBUG_OBJECT(m_harness->element(), "Decoding%s frame", isKeyFrame ? " key" : "");

    Vector<uint8_t> data { frameData };
    if (data.isEmpty()) {
        m_postTaskCallback([protectedThis = Ref { *this }, callback = WTFMove(callback)]() mutable {
            if (protectedThis->m_isClosed)
                return;

            protectedThis->m_outputCallback(makeUnexpected("Empty frame"_s));
            callback({ });
        });
        return;
    }

    auto bufferSize = data.size();
    auto bufferData = data.data();
    auto buffer = adoptGRef(gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, bufferData, bufferSize, 0, bufferSize, new Vector<uint8_t>(WTFMove(data)), [](gpointer data) {
        delete static_cast<Vector<uint8_t>*>(data);
    }));

    GST_BUFFER_DTS(buffer.get()) = GST_BUFFER_PTS(buffer.get()) = timestamp;
    if (duration)
        GST_BUFFER_DURATION(buffer.get()) = *duration;

    auto result = m_harness->pushBuffer(WTFMove(buffer));
    m_postTaskCallback([protectedThis = Ref { *this }, callback = WTFMove(callback), result]() mutable {
        if (protectedThis->m_isClosed)
            return;

        if (result)
            protectedThis->m_harness->processOutputBuffers();
        else
            protectedThis->m_outputCallback(makeUnexpected("Decode error"_s));

        callback({ });
    });
}

void GStreamerInternalAudioDecoder::flush(Function<void()>&& callback)
{
    if (m_isClosed) {
        GST_DEBUG_OBJECT(m_harness->element(), "Decoder closed, nothing to flush");
        m_postTaskCallback(WTFMove(callback));
        return;
    }

    m_harness->flushBuffers();
    m_postTaskCallback(WTFMove(callback));
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_CODECS) && USE(GSTREAMER)
