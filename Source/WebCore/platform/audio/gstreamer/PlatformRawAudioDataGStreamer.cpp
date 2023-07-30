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
#include "PlatformRawAudioDataGStreamer.h"

#if USE(GSTREAMER)

GST_DEBUG_CATEGORY(webkit_audio_data_debug);
#define GST_CAT_DEFAULT webkit_audio_data_debug

namespace WebCore {

static void ensureAudioDataDebugCategoryInitialized()
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_audio_data_debug, "webkitaudiodata", 0, "WebKit Audio Data");
    });
}

RefPtr<PlatformRawAudioData> PlatformRawAudioData::create(std::span<const uint8_t>&& data, AudioSampleFormat format, float sampleRate, int64_t timestamp, size_t numberOfFrames, size_t numberOfChannels)
{
    UNUSED_PARAM(numberOfFrames);
    ensureAudioDataDebugCategoryInitialized();
    GstAudioLayout layout;
    GstAudioFormat gstFormat;
    switch (format) {
    case AudioSampleFormat::U8:
        gstFormat = GST_AUDIO_FORMAT_U8;
        layout = GST_AUDIO_LAYOUT_INTERLEAVED;
        break;
    case AudioSampleFormat::S16:
        gstFormat = GST_AUDIO_FORMAT_S16;
        layout = GST_AUDIO_LAYOUT_INTERLEAVED;
        break;
    case AudioSampleFormat::S32:
        gstFormat = GST_AUDIO_FORMAT_S32;
        layout = GST_AUDIO_LAYOUT_INTERLEAVED;
        break;
    case AudioSampleFormat::F32:
        gstFormat = GST_AUDIO_FORMAT_F32;
        layout = GST_AUDIO_LAYOUT_INTERLEAVED;
        break;
    case AudioSampleFormat::U8Planar:
        gstFormat = GST_AUDIO_FORMAT_U8;
        layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;
        break;
    case AudioSampleFormat::S16Planar:
        gstFormat = GST_AUDIO_FORMAT_S16;
        layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;
        break;
    case AudioSampleFormat::S32Planar:
        gstFormat = GST_AUDIO_FORMAT_S32;
        layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;
        break;
    case AudioSampleFormat::F32Planar:
        gstFormat = GST_AUDIO_FORMAT_F32;
        layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;
        break;
    }

    GstAudioInfo info;
    gst_audio_info_set_format(&info, gstFormat, static_cast<int>(sampleRate), numberOfChannels, nullptr);
    GST_AUDIO_INFO_LAYOUT(&info) = layout;

    auto caps = adoptGRef(gst_audio_info_to_caps(&info));
    GST_TRACE("Creating raw audio wrapper with caps %" GST_PTR_FORMAT, caps.get());
    auto buffer = adoptGRef(gst_buffer_new_memdup(data.data(), data.size_bytes()));
    if (timestamp)
        GST_BUFFER_PTS(buffer.get()) = timestamp * 1000;
    GST_BUFFER_DURATION(buffer.get()) = (numberOfFrames / sampleRate) * 1000000000;

    auto sample = adoptGRef(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    return PlatformRawAudioDataGStreamer::create(WTFMove(sample), timestamp);
}

PlatformRawAudioDataGStreamer::PlatformRawAudioDataGStreamer(GRefPtr<GstSample>&& sample, std::optional<int64_t>&& timestamp)
    : m_sample(WTFMove(sample))
    , m_timestamp(WTFMove(timestamp))
{
    ensureAudioDataDebugCategoryInitialized();
    auto* caps = gst_sample_get_caps(m_sample.get());
    gst_audio_info_from_caps(&m_info, caps);
}

AudioSampleFormat PlatformRawAudioDataGStreamer::format() const
{
    auto gstFormat = GST_AUDIO_INFO_FORMAT(&m_info);
    auto layout = GST_AUDIO_INFO_LAYOUT(&m_info);
    switch (gstFormat) {
    case GST_AUDIO_FORMAT_U8:
        if (layout == GST_AUDIO_LAYOUT_INTERLEAVED)
            return AudioSampleFormat::U8;
        return AudioSampleFormat::U8Planar;
    case GST_AUDIO_FORMAT_S16:
        if (layout == GST_AUDIO_LAYOUT_INTERLEAVED)
            return AudioSampleFormat::S16;
        return AudioSampleFormat::S16Planar;
    case GST_AUDIO_FORMAT_S32:
        if (layout == GST_AUDIO_LAYOUT_INTERLEAVED)
            return AudioSampleFormat::S32;
        return AudioSampleFormat::S32Planar;
    case GST_AUDIO_FORMAT_F32:
        if (layout == GST_AUDIO_LAYOUT_INTERLEAVED)
            return AudioSampleFormat::F32;
        return AudioSampleFormat::F32Planar;
    default:
        break;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return AudioSampleFormat::U8;
}

size_t PlatformRawAudioDataGStreamer::sampleRate() const
{
    return GST_AUDIO_INFO_RATE(&m_info);
}

size_t PlatformRawAudioDataGStreamer::numberOfChannels() const
{
    return GST_AUDIO_INFO_CHANNELS(&m_info);
}

size_t PlatformRawAudioDataGStreamer::numberOfFrames() const
{
    auto totalFrames = gst_buffer_get_size(gst_sample_get_buffer(m_sample.get())) / GST_AUDIO_INFO_BPS(&m_info);
    return totalFrames / numberOfChannels();
}

std::optional<uint64_t> PlatformRawAudioDataGStreamer::duration() const
{
    auto* buffer = gst_sample_get_buffer(m_sample.get());
    if (!GST_BUFFER_DURATION_IS_VALID(buffer))
        return { };

    return GST_TIME_AS_USECONDS(GST_BUFFER_DURATION(buffer));
}

int64_t PlatformRawAudioDataGStreamer::timestamp() const
{
    // According to spec the timestamp can be negative, so we can't rely solely on the buffer PTS
    // (which is unsigned).
    if (m_timestamp)
        return *m_timestamp;

    auto* buffer = gst_sample_get_buffer(m_sample.get());
    return GST_TIME_AS_USECONDS(GST_BUFFER_PTS(buffer));
}

} // namespace WebCore

#undef GST_CAT_DEFAULT

#endif // USE(GSTREAMER)
