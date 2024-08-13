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

#pragma once

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "RTCRtpTransformableFrame.h"

#include "GRefPtrGStreamer.h"
#include <wtf/Ref.h>

namespace WebCore {

class GStreamerRtpTransformableFrame final : public RTCRtpTransformableFrame {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static Ref<GStreamerRtpTransformableFrame> create(GRefPtr<GstBuffer>&& buffer, bool isAudioSenderFrame) { return adoptRef(*new GStreamerRtpTransformableFrame(WTFMove(buffer), isAudioSenderFrame)); }
    ~GStreamerRtpTransformableFrame();

    GRefPtr<GstBuffer> takeBuffer();

private:
    GStreamerRtpTransformableFrame(GRefPtr<GstBuffer>&&, bool isAudioSenderFrame);

    // RTCRtpTransformableFrame
    std::span<const uint8_t> data() const final;
    void setData(std::span<const uint8_t>) final;
    bool isKeyFrame() const final;
    uint64_t timestamp() const final;
    RTCEncodedAudioFrameMetadata audioMetadata() const final;
    RTCEncodedVideoFrameMetadata videoMetadata() const final;

    GRefPtr<GstBuffer> m_buffer;
    bool m_isAudioSenderFrame;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
