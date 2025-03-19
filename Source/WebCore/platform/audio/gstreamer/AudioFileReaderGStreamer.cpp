/*
 *  Copyright (C) 2011, 2012 Igalia S.L
 *  Copyright (C) 2011 Zan Dobersek  <zandobersek@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "AudioFileReader.h"

#if ENABLE(WEB_AUDIO) && USE(GSTREAMER)

#include "AudioBus.h"
#include "AudioSampleFormat.h"
#include "GStreamerCommon.h"
#include "GStreamerElementHarness.h"
#include "GStreamerQuirks.h"
#include "GStreamerRegistryScanner.h"
#include <gst/base/gsttypefindhelper.h>
#include <wtf/Noncopyable.h>
#include <wtf/PrintStream.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/ThreadSafeWeakPtr.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_audio_file_reader_debug);
#define GST_CAT_DEFAULT webkit_audio_file_reader_debug

class AudioFileReader : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<AudioFileReader, WTF::DestructionThread::Main> {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(AudioFileReader);
    WTF_MAKE_NONCOPYABLE(AudioFileReader);
public:
    explicit AudioFileReader(std::span<const uint8_t>);
    ~AudioFileReader() = default;

    RefPtr<AudioBus> createBus(float sampleRate, bool mixToMono);

private:
    RefPtr<GStreamerElementHarness> m_decoderHarness;

    std::span<const uint8_t> m_data;
    bool m_error { false };
    float m_sampleRate { 0 };
    int m_channels { 0 };
    size_t m_channelSize { 0 };
    HashMap<int, std::span<float>, WTF::IntHash<int>, WTF::UnsignedWithZeroKeyHashTraits<int>> m_channelData;
};

static inline std::optional<int> channelTypeFromGStreamerPosition(int position)
{
    int channelId = 0;
    switch (position) {
    case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT:
    case GST_AUDIO_CHANNEL_POSITION_MONO:
        channelId = AudioBus::ChannelLeft;
        break;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT:
        channelId = AudioBus::ChannelRight;
        break;
    case GST_AUDIO_CHANNEL_POSITION_LFE1:
        channelId = AudioBus::ChannelLFE;
        break;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER:
        channelId = AudioBus::ChannelCenter;
        break;
    case GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT:
    case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
        channelId = AudioBus::ChannelSurroundLeft;
        break;
    case GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT:
    case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
        channelId = AudioBus::ChannelSurroundRight;
        break;
    default:
        GST_WARNING("Unhandled channel: %d", position);
        return { };
    };
    return channelId;
}

AudioFileReader::AudioFileReader(std::span<const uint8_t> data)
    : m_data(data)
{
    auto buffer = wrapSpanData(m_data);
    auto caps = adoptGRef(gst_type_find_helper_for_buffer(nullptr, buffer.get(), nullptr));
    GST_DEBUG("Caps typefind result: %" GST_PTR_FORMAT, caps.get());
    if (!caps) {
        GST_WARNING("Typefinding failed");
        m_error = true;
        return;
    }

    auto pluginsToSkip = GStreamerQuirksManager::singleton().disallowedWebAudioDecoders();
    auto& scanner = GStreamerRegistryScanner::singleton();
    auto lookupResult = scanner.areCapsSupported(GStreamerRegistryScanner::Configuration::Decoding, caps, false, pluginsToSkip);
    if (!lookupResult) {
        GST_WARNING("No decoder found for caps %" GST_PTR_FORMAT, caps.get());
        m_error = true;
        return;
    }

    GRefPtr<GstElement> element = gst_element_factory_create(lookupResult.factory.get(), nullptr);
    configureAudioDecoderForHarnessing(element);
    m_decoderHarness = GStreamerElementHarness::create(WTFMove(element), [this](auto&, auto&& sample) {
        GstMappedAudioBuffer mappedBuffer(sample, GST_MAP_READ);
        if (!mappedBuffer)
            return;

        m_channels = GST_AUDIO_BUFFER_CHANNELS(mappedBuffer.get());
        m_channelSize += gst_buffer_get_size(gst_sample_get_buffer(sample.get())) / GST_AUDIO_BUFFER_BPF(mappedBuffer.get());

#define CONVERT_SAMPLES(samples)                                        \
        for (size_t channel = 0; channel < samples.size(); channel++) { \
            auto channelData = samples[channel];                        \
            for (size_t i = 0; i < channelData.size(); i++)             \
                convertedData[channel][i] = convertAudioSample<float>(channelData[i]); \
        }

        Vector<std::span<float>> convertedData;
        switch (GST_AUDIO_BUFFER_FORMAT(mappedBuffer.get())) {
        case GST_AUDIO_FORMAT_U8: {
            auto samples = mappedBuffer.samples<uint8_t>(0);
            CONVERT_SAMPLES(samples);
            break;
        }
        case GST_AUDIO_FORMAT_S16: {
            auto samples = mappedBuffer.samples<int16_t>(0);
            CONVERT_SAMPLES(samples);
            break;
        }
        case GST_AUDIO_FORMAT_S32: {
            auto samples = mappedBuffer.samples<int32_t>(0);
            CONVERT_SAMPLES(samples);
            break;
        }
        case GST_AUDIO_FORMAT_F32:
            convertedData = mappedBuffer.samples<float>(0);
            break;
        default:
            break;
        };
#undef CONVERT_SAMPLES

        for (size_t channel = 0; channel < convertedData.size(); channel++) {
            auto channelType = channelTypeFromGStreamerPosition(channel);
            if (!channelType)
                continue;
            m_channelData.add(*channelType, convertedData[channel]);
        }
    });

    if (!m_decoderHarness->pushSample(adoptGRef(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr)))) {
        GST_WARNING_OBJECT(m_decoderHarness->element(), "Parser or downstream decoder failed to process data");
        m_error = true;
        return;
    }

    m_decoderHarness->reset();
}

RefPtr<AudioBus> AudioFileReader::createBus(float sampleRate, bool mixToMono)
{
    if (m_error)
        return nullptr;

    GST_DEBUG("sampleRate: %f, mixToMono: %s", sampleRate, boolForPrinting(mixToMono));
    m_sampleRate = sampleRate;

    GST_DEBUG("Transfering data to audio bus containing %d channels, each with %zu bytes", m_channels, m_channelSize);
    auto audioBus = AudioBus::create(m_channels, m_channelSize, false);
    audioBus->setSampleRate(m_sampleRate);

    for (auto& [index, storage] : m_channelData)
        audioBus->setChannelMemory(index, storage);

    if (mixToMono)
        return AudioBus::createByMixingToMono(audioBus.get());
    return audioBus;
}

RefPtr<AudioBus> createBusFromInMemoryAudioFile(std::span<const uint8_t> data, bool mixToMono, float sampleRate)
{
    ensureGStreamerInitialized();
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_audio_file_reader_debug, "webkitaudiofilereader", 0, "WebKit WebAudio FileReader");
    });

    GST_DEBUG("Creating bus from in-memory audio data (%zu bytes)", data.size());
    auto reader = AudioFileReader(data);
    return reader.createBus(sampleRate, mixToMono);
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO) && USE(GSTREAMER)
