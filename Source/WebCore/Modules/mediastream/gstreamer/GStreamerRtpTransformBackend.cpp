/*
 *  Copyright (C) 2021-2022 Igalia S.L. All rights reserved.
 *  Copyright (C) 2022 Metrological Group B.V.
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
#include "GStreamerRtpTransformBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerRtpTransformableFrame.h"
#include <wtf/text/StringConcatenateNumbers.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_webrtc_transform_debug);
#define GST_CAT_DEFAULT webkit_webrtc_transform_debug

#if GST_CHECK_VERSION(1, 22, 0)
#define TRANSFORM_DEBUG(...) GST_DEBUG_ID(m_backendId.ascii().data(), __VA_ARGS__)
#define TRANSFORM_WARNING(...) GST_WARNING_ID(m_backendId.ascii().data(), __VA_ARGS__)
#define TRANSFORM_TRACE(...) GST_TRACE_ID(m_backendId.ascii().data(), __VA_ARGS__)
#else
#define TRANSFORM_DEBUG(...) GST_DEBUG(__VA_ARGS__)
#define TRANSFORM_WARNING(...) GST_WARNING(__VA_ARGS__)
#define TRANSFORM_TRACE(...) GST_TRACE(__VA_ARGS__)
#endif

GStreamerRtpTransformBackend::GStreamerRtpTransformBackend(MediaType mediaType, Side side)
    : m_mediaType(mediaType)
    , m_side(side)
{
    static Atomic<uint64_t> nBackend = 0;
    m_backendId = makeString("webkit-webrtc-"_s, mediaType == MediaType::Audio ? "audio"_s : "video"_s, '-', side == Side::Receiver ? "receiver"_s : "sender"_s, "-transform-"_s, nBackend.exchangeAdd(1));

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_transform_debug, "webkitwebrtctransform", 0, "WebKit WebRTC Transforms");
    });
}

void GStreamerRtpTransformBackend::setInputCallback(Callback&& callback)
{
    Locker holdLock { m_inputCallbackLock };
    TRANSFORM_DEBUG("Setting %s input callback", callback ? "non-empty" : "empty");
    m_inputCallback = WTFMove(callback);
}

void GStreamerRtpTransformBackend::clearTransformableFrameCallback()
{
    TRANSFORM_DEBUG("Clearing input callback");
    //setInputCallback({ });
}

void GStreamerRtpTransformBackend::processTransformedFrame(RTCRtpTransformableFrame& frame)
{
    Locker transformLock { m_transformLock };

    TRANSFORM_TRACE("Notifying transformed frame");
    auto gstFrame = static_cast<GStreamerRtpTransformableFrame&>(frame).takeBuffer();
    if (!gstFrame) {
        TRANSFORM_WARNING("No frame");
        return;
    }

    m_transformedBuffer = WTFMove(gstFrame);
    m_transformCondition.notifyAll();
}

GRefPtr<GstBuffer> GStreamerRtpTransformBackend::transform(GRefPtr<GstBuffer>&& buffer)
{
    TRANSFORM_TRACE("Transforming frame");
    Locker holdLock { m_inputCallbackLock };
    if (!m_inputCallback) {
        TRANSFORM_TRACE("No input callback, doing pass-through transform");
        return buffer;
    }

    m_inputCallback(GStreamerRtpTransformableFrame::create(WTFMove(buffer), m_mediaType == MediaType::Audio && m_side == Side::Sender));

    Locker transformLock { m_transformLock };
    // m_transformCondition.wait(m_transformLock);
    TRANSFORM_TRACE("Frame transformed, passing to call site");
    return m_transformedBuffer;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
