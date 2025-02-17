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
#include "GStreamerIceTransportBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerWebRTCUtils.h"
#include "NotImplemented.h"
#include <JavaScriptCore/ArrayBuffer.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/glib/GUniquePtr.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_webrtc_ice_transport_debug);
#define GST_CAT_DEFAULT webkit_webrtc_ice_transport_debug

WTF_MAKE_TZONE_ALLOCATED_IMPL(GStreamerIceTransportBackend);

GStreamerIceTransportBackend::GStreamerIceTransportBackend(GRefPtr<GstWebRTCDTLSTransport>&& transport)
    : m_backend(WTFMove(transport))
{
    ASSERT(m_backend);

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_ice_transport_debug, "webkitwebrtcice", 0, "WebKit WebRTC ICE Transport");
    });

    iceTransportChanged();
    g_signal_connect_swapped(m_backend.get(), "notify::transport", G_CALLBACK(+[](GStreamerIceTransportBackend* backend) {
        backend->iceTransportChanged();
    }), this);
}

GStreamerIceTransportBackend::~GStreamerIceTransportBackend()
{
    if (G_IS_OBJECT(m_iceTransport.get()))
        g_signal_handlers_disconnect_by_data(m_iceTransport.get(), this);
    g_signal_handlers_disconnect_by_data(m_backend.get(), this);
}

void GStreamerIceTransportBackend::iceTransportChanged()
{
    if (G_IS_OBJECT(m_iceTransport.get()))
        g_signal_handlers_disconnect_by_data(m_iceTransport.get(), this);

    g_object_get(m_backend.get(), "transport", &m_iceTransport.outPtr(), nullptr);

    // Setting same libnice socket size options as LibWebRTC. 1MB for incoming streams and 256Kb for outgoing streams.
    if (gstObjectHasProperty(GST_OBJECT_CAST(m_iceTransport.get()), "receive-buffer-size"))
        g_object_set(m_iceTransport.get(), "receive-buffer-size", 1048576, nullptr);

    if (gstObjectHasProperty(GST_OBJECT_CAST(m_iceTransport.get()), "send-buffer-size"))
        g_object_set(m_iceTransport.get(), "send-buffer-size", 262144, nullptr);

    g_signal_connect_swapped(m_iceTransport.get(), "notify::state", G_CALLBACK(+[](GStreamerIceTransportBackend* backend) {
        backend->stateChanged();
    }), this);
    g_signal_connect_swapped(m_iceTransport.get(), "notify::gathering-state", G_CALLBACK(+[](GStreamerIceTransportBackend* backend) {
        backend->gatheringStateChanged();
    }), this);
    g_signal_connect_swapped(m_iceTransport.get(), "on-selected-candidate-pair-change", G_CALLBACK(+[](GStreamerIceTransportBackend* backend) {
        backend->selectedCandidatePairChanged();
    }), this);
}

void GStreamerIceTransportBackend::registerClient(RTCIceTransportBackendClient& client)
{
    ASSERT(!m_client);
    m_client = client;

    GstWebRTCICEConnectionState transportState;
    GstWebRTCICEGatheringState gatheringState;
    g_object_get(m_iceTransport.get(), "state", &transportState, "gathering-state", &gatheringState, nullptr);

    callOnMainThread([weakThis = WeakPtr { *this }, transportState, gatheringState] {
        if (!weakThis || !weakThis->m_client)
            return;

#ifndef GST_DISABLE_GST_DEBUG
        GUniquePtr<char> desc(g_enum_to_string(GST_TYPE_WEBRTC_ICE_CONNECTION_STATE, transportState));
        GST_DEBUG_OBJECT(weakThis->m_backend.get(), "Initial ICE transport state: %s", desc.get());
#endif

        // We start observing a bit late and might miss the checking state. Synthesize it as needed.
        if (transportState > GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING && transportState != GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED)
            weakThis->m_client->onStateChanged(RTCIceTransportState::Checking);

        weakThis->m_client->onStateChanged(toRTCIceTransportState(transportState));
        weakThis->m_client->onGatheringStateChanged(toRTCIceGatheringState(gatheringState));
    });
}

void GStreamerIceTransportBackend::unregisterClient()
{
    ASSERT(m_client);
    m_client.clear();
}

void GStreamerIceTransportBackend::stateChanged() const
{
    if (!m_client)
        return;

    GstWebRTCICEConnectionState transportState;
    g_object_get(m_iceTransport.get(), "state", &transportState, nullptr);

#ifndef GST_DISABLE_GST_DEBUG
    GUniquePtr<char> desc(g_enum_to_string(GST_TYPE_WEBRTC_ICE_CONNECTION_STATE, transportState));
    GST_DEBUG_OBJECT(m_backend.get(), "ICE transport state changed to %s", desc.get());
#endif

    callOnMainThread([weakThis = WeakPtr { *this }, transportState] {
        if (!weakThis || !weakThis->m_client)
            return;
        weakThis->m_client->onStateChanged(toRTCIceTransportState(transportState));
    });
}

void GStreamerIceTransportBackend::gatheringStateChanged() const
{
    if (!m_client)
        return;

    GstWebRTCICEGatheringState gatheringState;
    g_object_get(m_iceTransport.get(), "gathering-state", &gatheringState, nullptr);
    callOnMainThread([weakThis = WeakPtr { *this }, gatheringState] {
        if (!weakThis || !weakThis->m_client)
            return;
        weakThis->m_client->onGatheringStateChanged(toRTCIceGatheringState(gatheringState));
    });
}

#if 1 // GST_CHECK_VERSION(1, 25, 0)
static Ref<RTCIceCandidate> candidateFromGstWebRTC(const GstWebRTCICECandidate* c)
{
    RTCIceCandidate::Fields fields;

    fields.foundation = String::fromUTF8(c->foundation);

    fields.component = toRTCIceComponent(c->component);

    fields.priority = c->priority;

    fields.address = String::fromUTF8(c->address);

    auto protocol = StringView::fromLatin1(c->protocol);
    fields.protocol = toRTCIceProtocol(protocol.toStringWithoutCopying());

    fields.port = c->port;

    auto type = StringView::fromLatin1(c->type);
    fields.type = toRTCIceCandidateType(type.toStringWithoutCopying());

    fields.usernameFragment = String::fromUTF8(c->username_fragment);

    auto tcpType = StringView::fromLatin1(c->tcp_type);
    if (!tcpType.isNull())
        fields.tcpType = toRTCIceTcpCandidateType(tcpType.toStringWithoutCopying());

    fields.address = String::fromUTF8(c->address);

    auto relatedAddress = StringView::fromLatin1(c->related_address);
    if (!relatedAddress.isNull())
        fields.relatedAddress = relatedAddress.toString();

    if (c->related_port != -1)
        fields.relatedPort = c->related_port;

    // FIXME: relayProtocol is not exposed yet.

    auto sdpMid = emptyString();
    auto candidate = String::fromUTF8(c->candidate);
    return RTCIceCandidate::create(candidate, sdpMid, WTFMove(fields));
}
#endif

void GStreamerIceTransportBackend::selectedCandidatePairChanged()
{
    // https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8484
#if 1 //  GST_CHECK_VERSION(1, 25, 0)
    GUniquePtr<GstWebRTCICECandidatePair> selectedPair(gst_webrtc_ice_transport_get_selected_candidate_pair(m_iceTransport.get()));
    if (!selectedPair)
        return;

    auto localCandidate = candidateFromGstWebRTC(selectedPair->local);
    auto remoteCandidate = candidateFromGstWebRTC(selectedPair->remote);
    WTF::callOnMainThreadAndWait([weakThis = WeakPtr { *this }, localCandidate = WTFMove(localCandidate), remoteCandidate = WTFMove(remoteCandidate)] mutable {
        if (!weakThis || !weakThis->m_client)
            return;
        weakThis->m_client->onSelectedCandidatePairChanged(WTFMove(localCandidate), WTFMove(remoteCandidate));
    });
#endif
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
