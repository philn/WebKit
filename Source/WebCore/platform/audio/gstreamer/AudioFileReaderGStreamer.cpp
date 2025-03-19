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

#if ENABLE(WEB_AUDIO)

#include "AudioBus.h"
#include "GStreamerCommon.h"
#include "GStreamerElementHarness.h"
#include "GStreamerQuirks.h"
#include "GStreamerRegistryScanner.h"
#include <gst/audio/audio-info.h>
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
    RefPtr<GStreamerElementHarness> m_deinterleaveHarness;
    Vector<RefPtr<GStreamerElementHarness>> m_deinterleavedHarnesses;

    std::span<const uint8_t> m_data;
    bool m_error { false };
    std::optional<bool> m_isInterleaved;
    float m_sampleRate { 0 };
    int m_channels { 0 };
    UncheckedKeyHashMap<int, GRefPtr<GstBufferList>> m_buffers;
    std::optional<int> m_firstChannelType;
    unsigned m_channelSize { 0 };
};

static void copyGstreamerBuffersToAudioChannel(const GRefPtr<GstBufferList>& buffers, AudioChannel* audioChannel)
{
    auto destination = audioChannel->mutableSpan();
    unsigned bufferCount = gst_buffer_list_length(buffers.get());
    uint64_t offset = 0;
    for (unsigned i = 0; i < bufferCount; ++i) {
        GstMappedBuffer buffer(gst_buffer_list_get(buffers.get(), i), GST_MAP_READ);
        auto count = buffer.size() / sizeof(float);
        memcpySpan(destination.subspan(offset, count), buffer.span<float>());
        offset += count;
    }
}

static inline std::optional<int> channelTypeFromCaps(GstCaps* caps)
{
    int channelId = 0;
    GstAudioInfo info;
    gst_audio_info_from_caps(&info, caps);
    switch (GST_AUDIO_INFO_POSITION(&info, 0)) {
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
        GST_WARNING("Unhandled channel: %d", GST_AUDIO_INFO_POSITION(&info, 0));
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

    GRefPtr deInterleave = makeGStreamerElement("deinterleave"_s, "deinterleave"_s);
    g_object_set(deInterleave.get(), "keep-positions", TRUE, nullptr);

    m_deinterleaveHarness = GStreamerElementHarness::create(WTFMove(deInterleave), [](auto&, auto&&) {}, [protectedThis = ThreadSafeWeakPtr { *this }, this](const auto& pad) -> RefPtr<GStreamerElementHarness> {
        RefPtr self = protectedThis.get();
        if (!self)
            return nullptr;

        if (!m_firstChannelType) {
            auto caps = adoptGRef(gst_pad_query_caps(pad.get(), nullptr));
            auto channelType = channelTypeFromCaps(caps.get());
            if (channelType)
                m_firstChannelType = WTFMove(channelType);
        }
        m_channels++;

        GRefPtr<GstElement> element = gst_element_factory_make("identity", nullptr);
        auto harness = GStreamerElementHarness::create(WTFMove(element), [protectedThis = ThreadSafeWeakPtr { *this }, this](auto&, auto&& sample) {
            RefPtr self = protectedThis.get();
            if (!self)
                return;

            auto buffer = gst_sample_get_buffer(sample.get());
            if (!buffer)
                return;

            auto caps = gst_sample_get_caps(sample.get());
            if (!caps)
                return;

            auto channelType = channelTypeFromCaps(caps);
            if (!channelType)
                return;

            if (!m_firstChannelType) {
                ASSERT_NOT_REACHED();
                return;
            }

            if (*channelType == *m_firstChannelType) {
                GstAudioInfo info;
                gst_audio_info_from_caps(&info, caps);
                m_channelSize += gst_buffer_get_size(buffer) / info.bpf;
            }

            // Shift hash table key values by one, otherwise we would hit an ASSERT here when channelType is
            // 0 (Left), which is also KeyTraits::emptyValue() which is not allowed.
            int keyId = *channelType + 1;
            auto result = m_buffers.ensure(keyId, [] {
                return adoptGRef(gst_buffer_list_new());
            });
            auto& bufferList = result.iterator->value;
            ASSERT(gst_buffer_list_is_writable(bufferList.get()));
            gst_buffer_list_add(bufferList.get(), gst_buffer_ref(buffer));
        });
        m_deinterleavedHarnesses.append(harness.ptr());
        return harness;
    });

    GRefPtr<GstElement> element = gst_element_factory_create(lookupResult.factory.get(), nullptr);
    configureAudioDecoderForHarnessing(element);
    m_decoderHarness = GStreamerElementHarness::create(WTFMove(element), [this](auto&, auto&& sample) {
        if (!m_isInterleaved) {
            auto caps = gst_sample_get_caps(sample.get());
            GstAudioInfo info;
            gst_audio_info_from_caps(&info, caps);
            auto layout = GST_AUDIO_INFO_LAYOUT(&info);
            m_isInterleaved = layout == GST_AUDIO_LAYOUT_INTERLEAVED;
        }
        if (*m_isInterleaved) {
            m_deinterleaveHarness->pushSample(WTFMove(sample));
            return;
        }

        GstMappedAudioBuffer mappedBuffer(sample, GST_MAP_READ);
        if (!mappedBuffer)
            return;

    });

    if (!m_decoderHarness->pushSample(adoptGRef(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr)))) {
        GST_WARNING_OBJECT(m_decoderHarness->element(), "Parser or downstream decoder failed to process data");
        m_error = true;
        return;
    }

    for (auto& stream : m_decoderHarness->outputStreams()) {
        while (auto event = stream->pullEvent())
            m_deinterleaveHarness->pushEvent(WTFMove(event));
    }

    m_decoderHarness->reset();
}

RefPtr<AudioBus> AudioFileReader::createBus(float sampleRate, bool mixToMono)
{
    if (m_error)
        return nullptr;

    GST_DEBUG("sampleRate: %f, mixToMono: %s", sampleRate, boolForPrinting(mixToMono));
    m_sampleRate = sampleRate;

    GST_DEBUG("Transfering data to audio bus containing %d channels, each with %u frames", m_channels, m_channelSize);
    auto audioBus = AudioBus::create(m_channels, m_channelSize, true);
    audioBus->setSampleRate(m_sampleRate);

    for (auto& [key, buffer] : m_buffers)
        copyGstreamerBuffersToAudioChannel(buffer, audioBus->channelByType(key - 1));

    m_buffers.clear();

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
    return AudioFileReader(data).createBus(sampleRate, mixToMono);
}

#undef GST_CAT_DEFAULT

} // WebCore

#endif // ENABLE(WEB_AUDIO)
