/*
 *  Copyright (C) 2019-2022 Igalia S.L. All rights reserved.
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
#include "GStreamerRtpReceiverBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerDtlsTransportBackend.h"
#include "GStreamerRtpReceiverTransformBackend.h"
#include "GStreamerWebRTCUtils.h"
#include "NotImplemented.h"
#include "RealtimeIncomingAudioSourceGStreamer.h"
#include "RealtimeIncomingVideoSourceGStreamer.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/glib/GUniquePtr.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(GStreamerRtpReceiverBackend);

GST_DEBUG_CATEGORY(webkit_webrtc_receiver_debug);
#define GST_CAT_DEFAULT webkit_webrtc_receiver_debug

GStreamerRtpReceiverBackend::GStreamerRtpReceiverBackend(GRefPtr<GstWebRTCRTPReceiver>&& rtcReceiver)
    : m_rtcReceiver(WTFMove(rtcReceiver))
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_receiver_debug, "webkitwebrtcreceiver", 0, "WebKit WebRTC RTP Receiver");
    });
}

RTCRtpParameters GStreamerRtpReceiverBackend::getParameters()
{
    notImplemented();
    return { };
}

Vector<RTCRtpContributingSource> GStreamerRtpReceiverBackend::getContributingSources() const
{
    notImplemented();
    return { };
}

Vector<RTCRtpSynchronizationSource> GStreamerRtpReceiverBackend::getSynchronizationSources() const
{
    notImplemented();
    return { };
}

Ref<RealtimeMediaSource> GStreamerRtpReceiverBackend::createSource(const String& trackKind, const String& trackId)
{
    // FIXME: This looks fishy, a single receiver can create multiple sources, so keeping track of m_incomingSource is odd.
    if (trackKind == "video"_s) {
        auto source = RealtimeIncomingVideoSourceGStreamer::create(AtomString { trackId });
        m_incomingSource = &source.get();
        return source;
    }

    RELEASE_ASSERT(trackKind == "audio"_s);
    auto source = RealtimeIncomingAudioSourceGStreamer::create(AtomString { trackId });
    m_incomingSource = &source.get();
    return source;
}

Ref<RTCRtpTransformBackend> GStreamerRtpReceiverBackend::rtcRtpTransformBackend()
{
    GST_DEBUG("phil %s", __PRETTY_FUNCTION__);

    if (m_incomingSource->isIncomingVideoSource()) {
        auto backend = GStreamerRtpReceiverTransformBackend::create(m_rtcReceiver, GStreamerRtpReceiverTransformBackend::MediaType::Video);
        auto& source = static_cast<RealtimeIncomingVideoSourceGStreamer&>(*m_incomingSource.get());

        source.setTransformCallback([backend = RefPtr { &backend.get() }](GRefPtr<GstBuffer>&& buffer) -> GRefPtr<GstBuffer> {
            return backend->transform(WTFMove(buffer));
        });
        return backend;
    }

    auto backend = GStreamerRtpReceiverTransformBackend::create(m_rtcReceiver, GStreamerRtpReceiverTransformBackend::MediaType::Audio);
    auto& source = static_cast<RealtimeIncomingAudioSourceGStreamer&>(*m_incomingSource.get());

    source.setTransformCallback([backend = RefPtr { &backend.get() }](GRefPtr<GstBuffer>&& buffer) -> GRefPtr<GstBuffer> {
        return backend->transform(WTFMove(buffer));
    });
    return backend;
}

std::unique_ptr<RTCDtlsTransportBackend> GStreamerRtpReceiverBackend::dtlsTransportBackend()
{
    GRefPtr<GstWebRTCDTLSTransport> transport;
    g_object_get(m_rtcReceiver.get(), "transport", &transport.outPtr(), nullptr);
    if (!transport)
        return nullptr;
    return makeUnique<GStreamerDtlsTransportBackend>(WTFMove(transport));
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
