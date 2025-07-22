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
#include <wtf/Forward.h>
#include <wtf/Identified.h>
#include <wtf/Noncopyable.h>
#include <wtf/ObjectIdentifier.h>

typedef struct _WebKitGstIceBackend WebKitGstIceBackend;
typedef struct _WebKitGstIceBackendClass WebKitGstIceBackendClass;

namespace WebCore {
class ScriptExecutionContext;
class SocketProvider;
class GStreamerIce;
using GStreamerIceBackendIdentifier = AtomicObjectIdentifier<GStreamerIce>;

class GStreamerIceBackend : public Identified<GStreamerIceBackendIdentifier> {
    WTF_MAKE_NONCOPYABLE(GStreamerIceBackend);
public:
    static RefPtr<GStreamerIceBackend> create(SocketProvider&);

    void ref() { refGStreamerIceBackend(); }
    void deref() { derefGStreamerIceBackend(); }

    virtual void addTurnServer(const String&) = 0;

protected:
    GStreamerIceBackend() = default;
    virtual ~GStreamerIceBackend() = default;
    virtual void refGStreamerIceBackend() = 0;
    virtual void derefGStreamerIceBackend() = 0;
};

} // namespace WebCore

#define WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND (webkit_gst_webrtc_ice_backend_get_type())
#define WEBKIT_GST_WEBRTC_ICE_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, WebKitGstIceBackend))
#define WEBKIT_GST_WEBRTC_ICE_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, WebKitGstIceBackendClass))
#define WEBKIT_IS_GST_WEBRTC_ICE_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND))
#define WEBKIT_IS_GST_WEBRTC_ICE_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND))

GType webkit_gst_webrtc_ice_backend_get_type();

WebKitGstIceBackend* webkitGstWebRTCCreateIceBackend(ASCIILiteral, WebCore::ScriptExecutionContext*);



#endif // USE(GSTREAMER_WEBRTC)
