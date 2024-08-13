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
#include "GStreamerWebRTCUtils.h"

namespace WebCore {

GStreamerRtpTransformableFrame::GStreamerRtpTransformableFrame(GRefPtr<GstBuffer>&& buffer, bool isAudioSenderFrame)
    : m_buffer(WTFMove(buffer))
    , m_isAudioSenderFrame(isAudioSenderFrame)
{
}

GStreamerRtpTransformableFrame::~GStreamerRtpTransformableFrame() = default;

GRefPtr<GstBuffer> GStreamerRtpTransformableFrame::takeBuffer()
{
    return WTFMove(m_buffer);
}

std::span<const uint8_t> GStreamerRtpTransformableFrame::data() const
{
    if (!m_buffer)
        return { };

    GstMappedRtpBuffer rtpBuffer(m_buffer.get(), GST_MAP_READ);
    auto payloadBuffer = adoptGRef(gst_rtp_buffer_get_payload_buffer(rtpBuffer.mappedData()));
    GstMappedBuffer payload(payloadBuffer, GST_MAP_READ);
    auto bufferData = payload.createVector();
    return bufferData.span();
}

void GStreamerRtpTransformableFrame::setData(std::span<const uint8_t> data)
{
    GstMappedRtpBuffer rtpBuffer(m_buffer.get(), GST_MAP_WRITE);
    auto payload = gst_rtp_buffer_get_payload(rtpBuffer.mappedData());
    memcpy(payload, data.data(), data.size());
}

bool GStreamerRtpTransformableFrame::isKeyFrame() const
{
    ASSERT(m_buffer);
    return !GST_BUFFER_FLAG_IS_SET(m_buffer.get(), GST_BUFFER_FLAG_DELTA_UNIT);
}

uint64_t GStreamerRtpTransformableFrame::timestamp() const
{
    if (!m_buffer)
        return 0;

    GstMappedRtpBuffer rtpBuffer(m_buffer.get(), GST_MAP_READ);
    return gst_rtp_buffer_get_timestamp(rtpBuffer.mappedData());
}

RTCEncodedAudioFrameMetadata GStreamerRtpTransformableFrame::audioMetadata() const
{
    if (!m_buffer)
        return { };

    GstMappedRtpBuffer rtpBuffer(m_buffer.get(), GST_MAP_READ);
    Vector<uint32_t> csrcs;
    if (auto csrcCount = gst_rtp_buffer_get_csrc_count(rtpBuffer.mappedData())) {
        csrcs.reserveInitialCapacity(csrcCount);
        for (uint8_t i = 0; i < csrcCount; i++)
            csrcs.append(gst_rtp_buffer_get_csrc(rtpBuffer.mappedData(), i));
    }

    return { gst_rtp_buffer_get_ssrc(rtpBuffer.mappedData()), WTFMove(csrcs) };
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
