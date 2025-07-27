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

#if USE(GSTREAMER_WEBRTC)

#include <glib-object.h>
#include <gst/webrtc/webrtc_fwd.h>
#include <wtf/Forward.h>
#include <wtf/Function.h>
#include <wtf/Identified.h>
#include <wtf/Noncopyable.h>
#include <wtf/ObjectIdentifier.h>
#include <wtf/RefCounted.h>

typedef struct _WebKitGstIceAgent WebKitGstIceAgent;
typedef struct _WebKitGstIceAgentClass WebKitGstIceAgentClass;

namespace WebCore {
class ScriptExecutionContext;
class SocketProvider;
class GStreamerIce;
using GStreamerIceBackendIdentifier = AtomicObjectIdentifier<GStreamerIce>;

class GStreamerIceBackendClient : public RefCounted<GStreamerIceBackendClient> {
    WTF_MAKE_NONCOPYABLE(GStreamerIceBackendClient);
public:
    static Ref<GStreamerIceBackendClient> create() { return adoptRef(*new GStreamerIceBackendClient); }
    void ref() const { RefCounted::ref(); }
    void deref() const { RefCounted::deref(); }

    using OnIceCandidateCallback = Function<void(unsigned, const String&)>;
    void setOnIceCandidateCallback(OnIceCandidateCallback&& callback) { m_onIceCandidateCallback = WTFMove(callback); }
    void notifyIceCandidate(unsigned sessionId, const String& candidate) { m_onIceCandidateCallback(sessionId, candidate); }

private:
    GStreamerIceBackendClient() = default;

    OnIceCandidateCallback m_onIceCandidateCallback;
};

class GStreamerIceBackend : public Identified<GStreamerIceBackendIdentifier> {
    WTF_MAKE_NONCOPYABLE(GStreamerIceBackend);
public:
    void ref() { refGStreamerIceBackend(); }
    void deref() { derefGStreamerIceBackend(); }

    virtual void setForceRelay(bool) = 0;
    virtual void setStunServer(const String&) = 0;
    virtual void addTurnServer(const String&) = 0;
    virtual void setIsController(bool) = 0;

    virtual std::optional<unsigned> addStream(unsigned) = 0;
    virtual bool gatherCandidatesForStream(unsigned) = 0;

protected:
    GStreamerIceBackend() = default;
    virtual ~GStreamerIceBackend() = default;
    virtual void refGStreamerIceBackend() = 0;
    virtual void derefGStreamerIceBackend() = 0;
};

} // namespace WebCore

#define WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND (webkit_gst_webrtc_ice_backend_get_type())
#define WEBKIT_GST_WEBRTC_ICE_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, WebKitGstIceAgent))
#define WEBKIT_GST_WEBRTC_ICE_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, WebKitGstIceAgentClass))
#define WEBKIT_IS_GST_WEBRTC_ICE_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND))
#define WEBKIT_IS_GST_WEBRTC_ICE_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND))

GType webkit_gst_webrtc_ice_backend_get_type();

WebKitGstIceAgent* webkitGstWebRTCCreateIceAgent(const String&, WebCore::ScriptExecutionContext*);

bool webkitGstWebRTCIceAgentGatherCandidates(WebKitGstIceAgent*, unsigned);
GstWebRTCICETransport* webkitGstWebRTCIceAgentCreateTransport(WebKitGstIceAgent*, unsigned, GstWebRTCICEComponent);

#endif // USE(GSTREAMER_WEBRTC)
