/*
 *  Copyright (C) 2023 Igalia S.L. All rights reserved.
 *  Copyright (C) 2023 Metrological Group B.V.
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

#include "GStreamerRtpTransformableFrame.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"

namespace WebCore {

GStreamerRtpTransformableFrame::GStreamerRtpTransformableFrame(GRefPtr<GstBuffer>&& buffer, bool isAudioSenderFrame)
    : m_buffer(WTFMove(buffer))
    , m_isAudioSenderFrame(isAudioSenderFrame)
{
}

GStreamerRtpTransformableFrame::~GStreamerRtpTransformableFrame()
{
}

GRefPtr<GstBuffer> GStreamerRtpTransformableFrame::takeBuffer()
{
    auto buffer = WTFMove(m_buffer);
    m_buffer = nullptr;
    return buffer;
}

std::span<const uint8_t> GStreamerRtpTransformableFrame::data() const
{
    if (!m_buffer)
        return { };

    GstMappedBuffer buffer(m_buffer.get(), GST_MAP_READ);
    auto bufferData = buffer.createVector();
    return bufferData.span();
}

void GStreamerRtpTransformableFrame::setData(std::span<const uint8_t> data)
{
    auto buffer = adoptGRef(gst_buffer_new_allocate(nullptr, data.size_bytes(), nullptr));
    {
        GstMappedBuffer mappedBuffer(buffer.get(), GST_MAP_WRITE);
        memcpy(mappedBuffer.data(), data.data(), data.size_bytes());
    }

    if (m_buffer) {
        GST_BUFFER_PTS(buffer.get()) = GST_BUFFER_PTS(m_buffer.get());
        GST_BUFFER_DTS(buffer.get()) = GST_BUFFER_DTS(m_buffer.get());
        GST_BUFFER_DURATION(buffer.get()) = GST_BUFFER_DURATION(m_buffer.get());
        GST_BUFFER_FLAGS(buffer.get()) = GST_BUFFER_FLAGS(m_buffer.get());
    }
    m_buffer = WTFMove(buffer);
}

bool GStreamerRtpTransformableFrame::isKeyFrame() const
{
    ASSERT(m_buffer);
    return !GST_BUFFER_FLAG_IS_SET(m_buffer.get(), GST_BUFFER_FLAG_DELTA_UNIT);
}

uint64_t GStreamerRtpTransformableFrame::timestamp() const
{
    return m_buffer ? GST_BUFFER_PTS(m_buffer.get()) : 0;
}

RTCEncodedAudioFrameMetadata GStreamerRtpTransformableFrame::audioMetadata() const
{
    if (!m_buffer)
        return { };

    return {};
    // Vector<uint32_t> cssrcs;
    // if (!m_isAudioSenderFrame) {
    //     auto* audioFrame = static_cast<webrtc::TransformableAudioFrameInterface*>(m_buffer.get());
    //     auto& header = audioFrame->GetHeader();
    //     if (header.numCSRCs) {
    //         cssrcs.reserveInitialCapacity(header.numCSRCs);
    //         for (size_t cptr = 0; cptr < header.numCSRCs; ++cptr)
    //             cssrcs.uncheckedAppend(header.arrOfCSRCs[cptr]);
    //     }
    // }
    // return { m_buffer->GetSsrc(), WTFMove(cssrcs) };
}

RTCEncodedVideoFrameMetadata GStreamerRtpTransformableFrame::videoMetadata() const
{
    if (!m_buffer)
        return { };

    return {};
    // auto* videoFrame = static_cast<webrtc::TransformableVideoFrameInterface*>(m_buffer.get());
    // auto& metadata = videoFrame->GetMetadata();

    // std::optional<int64_t> frameId;
    // if (metadata.GetFrameId())
    //     frameId = *metadata.GetFrameId();

    // Vector<int64_t> dependencies;
    // for (auto value : metadata.GetFrameDependencies())
    //     dependencies.append(value);

    // return { frameId, WTFMove(dependencies), metadata.GetWidth(), metadata.GetHeight(), metadata.GetSpatialIndex(), metadata.GetTemporalIndex(), m_buffer->GetSsrc() };
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
