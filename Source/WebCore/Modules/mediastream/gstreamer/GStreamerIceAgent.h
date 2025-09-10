/*
 *  Copyright (C) 2025 Igalia S.L. All rights reserved.
 *  Copyright (C) 2025 Metrological Group B.V.
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

#if USE(GSTREAMER_WEBRTC) && USE(LIBRICE)

#include "ExceptionData.h"
#include "ExceptionOr.h"
#include "GRefPtrGStreamer.h"
#include "RTCIceComponent.h"
#include "RTCIceConnectionState.h"
#include "RTCIceProtocol.h"

#include <glib-object.h>
#include <rice-proto.h>
#include <wtf/Expected.h>
#include <wtf/Forward.h>
#include <wtf/Forward.h>
#include <wtf/Function.h>
#include <wtf/Identified.h>
#include <wtf/Noncopyable.h>
#include <wtf/ObjectIdentifier.h>
#include <wtf/RefCounted.h>
#include <wtf/glib/GThreadSafeWeakPtr.h>

typedef struct _WebKitGstIceAgent WebKitGstIceAgent;
typedef struct _WebKitGstIceAgentClass WebKitGstIceAgentClass;

typedef struct _WebKitGstIceStream WebKitGstIceStream;
typedef struct _WebKitGstIceStreamClass WebKitGstIceStreamClass;

#define WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND (webkit_gst_webrtc_ice_backend_get_type())
#define WEBKIT_GST_WEBRTC_ICE_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, WebKitGstIceAgent))
#define WEBKIT_GST_WEBRTC_ICE_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, WebKitGstIceAgentClass))
#define WEBKIT_IS_GST_WEBRTC_ICE_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND))
#define WEBKIT_IS_GST_WEBRTC_ICE_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND))

GType webkit_gst_webrtc_ice_backend_get_type();

namespace WebCore {
class ScriptExecutionContext;
class SocketProvider;

class GStreamerIceBackendClient : public RefCounted<GStreamerIceBackendClient> {
    WTF_MAKE_NONCOPYABLE(GStreamerIceBackendClient);
public:
    static Ref<GStreamerIceBackendClient> create() { return adoptRef(*new GStreamerIceBackendClient); }
    void ref() const { RefCounted::ref(); }
    void deref() const { RefCounted::deref(); }

    using IncomingDataCallback = WTF::Function<void(unsigned, RTCIceProtocol, String, String, std::span<const uint8_t>&&)>;
    void setIncomingDataCallback(IncomingDataCallback&& callback) { m_incomingDataCallback = WTFMove(callback); }
    void notifyIncomingData(unsigned streamId, RTCIceProtocol protocol, String from, String to, std::span<const uint8_t>&& data) { m_incomingDataCallback(streamId, protocol, from, to, WTFMove(data)); }

private:
    GStreamerIceBackendClient() = default;

    IncomingDataCallback m_incomingDataCallback;
};

using GStreamerIceBackendIdentifier = AtomicObjectIdentifier<GStreamerIceBackendClient>;

class GStreamerIceBackend : public Identified<GStreamerIceBackendIdentifier> {
    WTF_MAKE_NONCOPYABLE(GStreamerIceBackend);
public:
    void ref() { refGStreamerIceBackend(); }
    void deref() { derefGStreamerIceBackend(); }

    using ResolveAddressCallback = CompletionHandler<void(ExceptionOr<String>&&)>;
    virtual void resolveAddress(const String&, ResolveAddressCallback&&) = 0;

    virtual void send(unsigned, RTCIceProtocol, String, String, std::span<const uint8_t>) = 0;

    virtual Vector<String> gatherSocketAddresses(unsigned) = 0;
    virtual void finalizeStream(unsigned) = 0;

protected:
    GStreamerIceBackend() = default;
    virtual ~GStreamerIceBackend() = default;
    virtual void refGStreamerIceBackend() = 0;
    virtual void derefGStreamerIceBackend() = 0;
};

} // namespace WebCore

WebKitGstIceAgent* webkitGstWebRTCCreateIceAgent(WTF::StringView, WebCore::ScriptExecutionContext*);

const GRefPtr<RiceAgent>& webkitGstWebRTCIceAgentGetRiceAgent(WebKitGstIceAgent*);
Vector<String> webkitGstWebRTCIceAgentGatherSocketAddresses(WebKitGstIceAgent*, unsigned);

GstWebRTCICETransport *webkitGstWebRTCIceAgentCreateTransport(WebKitGstIceAgent*, GThreadSafeWeakPtr<WebKitGstIceStream>&&, WebCore::RTCIceComponent);

void webkitGstWebRTCIceAgentSend(WebKitGstIceAgent*, unsigned, WebCore::RTCIceProtocol, String from, String to, std::span<const uint8_t>);

void webkitGstWebRTCIceAgentWakeup(WebKitGstIceAgent*);
void webkitGstWebRTCIceAgentGatheringDoneForStream(WebKitGstIceAgent*, unsigned);
void webkitGstWebRTCIceAgentLocalCandidateGatheredForStream(WebKitGstIceAgent*, unsigned, RiceAgentGatheredCandidate&);
void webkitGstWebRTCIceAgentComponentStateChangedForStream(WebKitGstIceAgent*, unsigned, RiceAgentComponentStateChange&);
void webkitGstWebRTCIceAgentNewSelectedPairForStream(WebKitGstIceAgent*, unsigned, RiceAgentSelectedPair&);
void webkitGstWebRTCIceAgentClosed(WebKitGstIceAgent*);
void webkitGstWebRTCIceAgentFinalizeStream(WebKitGstIceAgent*, unsigned);

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBRICE)
