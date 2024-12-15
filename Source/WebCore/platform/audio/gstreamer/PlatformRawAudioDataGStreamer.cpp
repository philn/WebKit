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

#if ENABLE(WEB_CODECS) && USE(GSTREAMER)

#include "AudioSampleFormat.h"
#include "GStreamerCommon.h"
#include "GUniquePtrGStreamer.h"
#include "MediaSampleGStreamer.h"
#include "SharedBuffer.h"
#include "WebCodecsAudioDataAlgorithms.h"
#include <gst/audio/audio-converter.h>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/StringCommon.h>

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
#if 0
GstAudioConverter* getAudioConverterForFormat(StringView&& key, GstAudioInfo& sourceInfo, GstAudioInfo& destinationInfo)
{
    static NeverDestroyed<UncheckedKeyHashMap<String, GUniquePtr<GstAudioConverter>>> audioConverters;
    auto result = audioConverters->ensure(key.toString(), [&] {
        return GUniquePtr<GstAudioConverter>(gst_audio_converter_new(GST_AUDIO_CONVERTER_FLAG_NONE, &sourceInfo, &destinationInfo, nullptr));
    });
    return result.iterator->value.get();
}
#endif

static std::pair<GstAudioFormat, GstAudioLayout> convertAudioSampleFormatToGStreamerFormat(const AudioSampleFormat& format)
{
    switch (format) {
    case AudioSampleFormat::U8:
        return { GST_AUDIO_FORMAT_U8, GST_AUDIO_LAYOUT_INTERLEAVED };
    case AudioSampleFormat::S16:
        return { GST_AUDIO_FORMAT_S16, GST_AUDIO_LAYOUT_INTERLEAVED };
    case AudioSampleFormat::S32:
        return { GST_AUDIO_FORMAT_S32, GST_AUDIO_LAYOUT_INTERLEAVED };
    case AudioSampleFormat::F32:
        return { GST_AUDIO_FORMAT_F32, GST_AUDIO_LAYOUT_INTERLEAVED };
    case AudioSampleFormat::U8Planar:
        return { GST_AUDIO_FORMAT_U8, GST_AUDIO_LAYOUT_NON_INTERLEAVED };
    case AudioSampleFormat::S16Planar:
        return { GST_AUDIO_FORMAT_S16, GST_AUDIO_LAYOUT_NON_INTERLEAVED };
    case AudioSampleFormat::S32Planar:
        return { GST_AUDIO_FORMAT_S32, GST_AUDIO_LAYOUT_NON_INTERLEAVED };
    case AudioSampleFormat::F32Planar:
        return { GST_AUDIO_FORMAT_F32, GST_AUDIO_LAYOUT_NON_INTERLEAVED };
    }
    RELEASE_ASSERT_NOT_REACHED();
    return { GST_AUDIO_FORMAT_UNKNOWN, GST_AUDIO_LAYOUT_INTERLEAVED };
}

Ref<PlatformRawAudioData> PlatformRawAudioData::create(Ref<MediaSample>&& sample)
{
    ASSERT(sample->platformSample().type == PlatformSample::GStreamerSampleType);
    return PlatformRawAudioDataGStreamer::create(GRefPtr { sample->platformSample().sample.gstSample });
}

RefPtr<PlatformRawAudioData> PlatformRawAudioData::create(std::span<const uint8_t> sourceData, AudioSampleFormat format, float sampleRate, int64_t timestamp, size_t numberOfFrames, size_t numberOfChannels)
{
    ensureAudioDataDebugCategoryInitialized();
    auto [gstFormat, layout] = convertAudioSampleFormatToGStreamerFormat(format);

    GstAudioInfo info;
    gst_audio_info_set_format(&info, gstFormat, static_cast<int>(sampleRate), numberOfChannels, nullptr);
    GST_AUDIO_INFO_LAYOUT(&info) = layout;

    auto caps = adoptGRef(gst_audio_info_to_caps(&info));
    GST_TRACE("Creating raw audio wrapper with caps %" GST_PTR_FORMAT, caps.get());

    Ref data = SharedBuffer::create(Vector<uint8_t>(sourceData));
    gpointer bufferData = const_cast<void*>(static_cast<const void*>(data->span().data()));
    auto bufferLength = data->size();
    auto buffer = adoptGRef(gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, bufferData, bufferLength, 0, bufferLength, reinterpret_cast<gpointer>(&data.leakRef()), [](gpointer data) {
        static_cast<SharedBuffer*>(data)->deref();
    }));
    GST_BUFFER_DURATION(buffer.get()) = (numberOfFrames / sampleRate) * 1000000000;

    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    if (timestamp < 0)
        segment.rate = -1.0;

    GST_BUFFER_PTS(buffer.get()) = abs(timestamp) * 1000;

    gst_buffer_add_audio_meta(buffer.get(), &info, numberOfFrames, nullptr);

    auto sample = adoptGRef(gst_sample_new(buffer.get(), caps.get(), &segment, nullptr));
    return PlatformRawAudioDataGStreamer::create(WTFMove(sample));
}

PlatformRawAudioDataGStreamer::PlatformRawAudioDataGStreamer(GRefPtr<GstSample>&& sample)
    : m_sample(WTFMove(sample))
{
    ensureAudioDataDebugCategoryInitialized();
    gst_audio_info_from_caps(&m_info, gst_sample_get_caps(m_sample.get()));
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
    auto totalSamples = gst_buffer_get_size(gst_sample_get_buffer(m_sample.get())) / GST_AUDIO_INFO_BPS(&m_info);
    return totalSamples / numberOfChannels();
}

std::optional<uint64_t> PlatformRawAudioDataGStreamer::duration() const
{
    auto buffer = gst_sample_get_buffer(m_sample.get());
    if (!GST_BUFFER_DURATION_IS_VALID(buffer))
        return { };

    return GST_TIME_AS_USECONDS(GST_BUFFER_DURATION(buffer));
}

int64_t PlatformRawAudioDataGStreamer::timestamp() const
{
    auto buffer = gst_sample_get_buffer(m_sample.get());
    auto timestamp = GST_TIME_AS_USECONDS(GST_BUFFER_PTS(buffer));
    auto segment = gst_sample_get_segment(m_sample.get());
    if (segment->rate < 0)
        return -timestamp;
    return timestamp;
}

size_t PlatformRawAudioDataGStreamer::memoryCost() const
{
    return gst_buffer_get_size(gst_sample_get_buffer(m_sample.get()));
}

bool PlatformRawAudioDataGStreamer::isInterleaved() const
{
    return GST_AUDIO_INFO_LAYOUT(&m_info) == GST_AUDIO_LAYOUT_INTERLEAVED;
}

static std::variant<Vector<std::span<uint8_t>>, Vector<std::span<int16_t>>, Vector<std::span<int32_t>>, Vector<std::span<float>>> planesOfSamples(AudioSampleFormat format, const GstAudioBuffer* audioBuffer, size_t samplesOffset)
{
    auto subspan = [samplesOffset](auto span) {
        RELEASE_ASSERT(samplesOffset <= span.size());
        return span.subspan(samplesOffset, span.size() - samplesOffset);
    };
    auto planeCount = static_cast<uint32_t>(audioBuffer->n_planes);
    switch (format) {
    case AudioSampleFormat::U8:
    case AudioSampleFormat::U8Planar:
        return Vector<std::span<uint8_t>> { planeCount, [&](auto index) {
            auto span = unsafeMakeSpan(static_cast<uint8_t*>(audioBuffer->planes[index]), audioBuffer->n_samples);
            return subspan(span);
        } };
    case AudioSampleFormat::S16:
    case AudioSampleFormat::S16Planar:
        return Vector<std::span<int16_t>> { planeCount, [&](auto index) {
            auto span = unsafeMakeSpan(static_cast<int16_t*>(audioBuffer->planes[index]), audioBuffer->n_samples);
            return subspan(span);
        } };
    case AudioSampleFormat::S32:
    case AudioSampleFormat::S32Planar:
        return Vector<std::span<int32_t>> { planeCount, [&](auto index) {
            auto span = unsafeMakeSpan(static_cast<int32_t*>(audioBuffer->planes[index]), audioBuffer->n_samples);
            return subspan(span);
        } };
    case AudioSampleFormat::F32:
    case AudioSampleFormat::F32Planar:
        return Vector<std::span<float>> { planeCount, [&](auto index) {
            auto span = unsafeMakeSpan(static_cast<float*>(audioBuffer->planes[index]), audioBuffer->n_samples);
            return subspan(span);
        } };
    }
    RELEASE_ASSERT_NOT_REACHED();
    return Vector<std::span<uint8_t>> { };
}

void PlatformRawAudioData::copyTo(std::span<uint8_t> destination, AudioSampleFormat destinationFormat, size_t planeIndex, std::optional<size_t> frameOffset, std::optional<size_t>, unsigned long copyElementCount)
{
#if 1
    // WebCodecsAudioDataAlgorithms's computeCopyElementCount ensures that all parameters are correct.
    auto& audioData = downcast<PlatformRawAudioDataGStreamer>(*this);

    auto sourceFormat = format();
    auto sourceSample = audioData.sample();
    bool destinationIsInterleaved = isAudioSampleFormatInterleaved(destinationFormat);

    if (audioSampleElementFormat(sourceFormat) == audioSampleElementFormat(destinationFormat)) {
        if (numberOfChannels() == 1 || (audioData.isInterleaved() && destinationIsInterleaved)) {
            // Simplest case.
            ASSERT(!planeIndex);
            GstMappedBuffer mappedBuffer(gst_sample_get_buffer(sourceSample), GST_MAP_READ);
            auto source = mappedBuffer.span();
            GUniquePtr<GstAudioInfo> sourceInfo(gst_audio_info_copy(audioData.info()));
            size_t frameOffsetInBytes = frameOffset.value_or(0) * GST_AUDIO_INFO_BPF(sourceInfo.get());
            RELEASE_ASSERT(frameOffsetInBytes <= source.size());
            auto subSource = source.subspan(frameOffsetInBytes, source.size() - frameOffsetInBytes);
            memcpySpan(destination, subSource);
            return;
        }
    }

    GstMappedAudioBuffer mappedBuffer(sourceSample, GST_MAP_READ);
    const auto inputBuffer = mappedBuffer.get();

    auto source = planesOfSamples(sourceFormat, inputBuffer, frameOffset.value_or(0) * (audioData.isInterleaved() ? numberOfChannels() : 1));

    if (!audioData.isInterleaved() && destinationIsInterleaved) {
        // Copy of all channels of the source into the destination buffer and deinterleave.
        // Ideally we would use an AudioToolbox's AudioConverter but it performs incorrect rounding during sample conversion in a way that makes us fail the W3C's AudioData tests.
        ASSERT(!planeIndex);
        ASSERT(!(copyElementCount % numberOfChannels()));

        auto copyElements = [numberOfChannels = numberOfChannels()]<typename T>(std::span<T> destination, auto& source, size_t frames) {
            RELEASE_ASSERT(destination.size() >= frames * numberOfChannels);
            RELEASE_ASSERT(source[0].size() >= frames); // All planes have the exact same size.
            size_t index = 0;
            for (size_t frame = 0; frame < frames; frame++) {
                for (size_t channel = 0; channel < source.size(); channel++)
                    destination[index++] = convertAudioSample<T>(source[channel][frame]);
            }
        };

        WTF::switchOn(audioElementSpan(destinationFormat, destination), [&](auto dst) {
            switchOn(source, [&](auto& src) {
                size_t numberOfFrames = copyElementCount / numberOfChannels();
                copyElements(dst, src, numberOfFrames);
            });
        });
        return;
    }

    // interleaved -> interleaved
    // planar -> planar
    // interleaved -> planar

    // Interleaved to planar, only copy samples of the correct channel (plane) to the destination.
    // If destination is interleaved, copy of all channels of the source into the destination buffer.
    size_t sampleOffset = audioData.isInterleaved() ? planeIndex : 0;
    size_t sampleIncrement = audioData.isInterleaved() && !destinationIsInterleaved ? numberOfChannels() : 1;
    size_t sourcePlane = audioData.isInterleaved() ? 0 : planeIndex;

    auto copyElements = []<typename T>(std::span<T> destination, auto sourcePlane, size_t sampleOffset, size_t sampleIndexIncrement, size_t samples) {
        RELEASE_ASSERT(destination.size() >= samples);
        RELEASE_ASSERT(sourcePlane.size() >= sampleIndexIncrement * samples + sampleOffset - 1);
        size_t sourceSampleIndex = sampleOffset;
        for (size_t sample = 0; sample < samples; sample++) {
            destination[sample] = convertAudioSample<T>(sourcePlane[sourceSampleIndex]);
            sourceSampleIndex += sampleIndexIncrement;
        }
    };
    WTF::switchOn(audioElementSpan(destinationFormat, destination), [&](auto dst) {
        switchOn(source, [&](auto& src) {
            copyElements(dst, src[sourcePlane], sampleOffset, sampleIncrement, copyElementCount);
        });
    });
#else
    auto& self = *reinterpret_cast<PlatformRawAudioDataGStreamer*>(this);

    [[maybe_unused]] auto [sourceFormat, sourceLayout] = convertAudioSampleFormatToGStreamerFormat(self.format());
    auto [destinationFormat, destinationLayout] = convertAudioSampleFormatToGStreamerFormat(format);
    auto sourceOffset = frameOffset.value_or(0);

#ifndef GST_DISABLE_GST_DEBUG
    const char* destinationFormatDescription = gst_audio_format_to_string(destinationFormat);
    GST_TRACE("Copying %s %s data at planeIndex %zu, destination format is %s %s, source offset: %zu", layoutToString(sourceLayout), gst_audio_format_to_string(sourceFormat), planeIndex, layoutToString(destinationLayout), destinationFormatDescription, sourceOffset);
#endif

    GST_TRACE("Input caps: %" GST_PTR_FORMAT, gst_sample_get_caps(self.sample()));

    GstMappedAudioBuffer mappedBuffer(self.sample(), GST_MAP_READ);
    const auto inputBuffer = mappedBuffer.get();

    if (self.format() == format) {
        WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // GLib port
        if (destinationLayout == GST_AUDIO_LAYOUT_NON_INTERLEAVED) {
            auto size = computeBytesPerSample(format) * inputBuffer->n_samples;
            memcpy(destination.data(), static_cast<uint8_t*>(inputBuffer->planes[planeIndex]) + sourceOffset, size);
        } else {
            GstMappedBuffer in(inputBuffer->buffer, GST_MAP_READ);
            memcpy(destination.data(), in.data() + sourceOffset, in.size() - sourceOffset);
        }
        WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
        return;
    }

    GstAudioInfo destinationInfo;
    gst_audio_info_set_format(&destinationInfo, destinationFormat, static_cast<int>(self.sampleRate()), self.numberOfChannels(), nullptr);
    GST_AUDIO_INFO_LAYOUT(&destinationInfo) = destinationLayout;

    auto outputCaps = adoptGRef(gst_audio_info_to_caps(&destinationInfo));
    GST_TRACE("Output caps: %" GST_PTR_FORMAT, outputCaps.get());

    GUniquePtr<GstAudioInfo> sourceInfo(gst_audio_info_copy(self.info()));
    GUniquePtr<char> key(gst_info_strdup_printf("%" GST_PTR_FORMAT ";%" GST_PTR_FORMAT, gst_sample_get_caps(self.sample()), outputCaps.get()));
    auto converter = getAudioConverterForFormat(StringView { span(key.get()) }, *sourceInfo.get(), destinationInfo);

    auto inFrames = gst_buffer_get_size(gst_sample_get_buffer(self.sample())) / GST_AUDIO_INFO_BPF(sourceInfo.get());
    auto outFrames = gst_audio_converter_get_out_frames(converter, inFrames);
    auto destinationBuffer = adoptGRef(gst_buffer_new_and_alloc(outFrames * GST_AUDIO_INFO_BPF(&destinationInfo)));
    if (destinationLayout == GST_AUDIO_LAYOUT_NON_INTERLEAVED)
        gst_buffer_add_audio_meta(destinationBuffer.get(), &destinationInfo, self.numberOfFrames(), nullptr);

    GstMappedAudioBuffer mappedDestinationBuffer(destinationBuffer.get(), destinationInfo, GST_MAP_WRITE);
    auto outputBuffer = mappedDestinationBuffer.get();
    gst_audio_converter_samples(converter, GST_AUDIO_CONVERTER_FLAG_NONE, inputBuffer->planes, inputBuffer->n_samples, outputBuffer->planes, outputBuffer->n_samples);

    auto planeSize = computeBytesPerSample(format) * outputBuffer->n_samples;
    WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // GLib port
    memcpy(destination.data(), static_cast<uint8_t*>(outputBuffer->planes[planeIndex]) + sourceOffset, planeSize);
    WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
#endif
}

} // namespace WebCore

#undef GST_CAT_DEFAULT

#endif // ENABLE(WEB_CODECS) && USE(GSTREAMER)
