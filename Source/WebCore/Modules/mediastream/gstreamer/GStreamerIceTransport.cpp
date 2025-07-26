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

#include "config.h"
#include "GStreamerIceTransport.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"
#include <gst/webrtc/ice.h>
#include <gst/webrtc/webrtc.h>
#include <wtf/glib/GThreadSafeWeakPtr.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/text/WTFString.h>

using namespace WTF;
using namespace WebCore;

typedef struct _WebKitGstIceTransportPrivate {
    GThreadSafeWeakPtr<WebKitGstIceAgent> agent;
    unsigned streamId;
} WebKitGstIceTransportPrivate;

typedef struct _WebKitGstIceTransport {
    GstWebRTCICETransport parent;
    WebKitGstIceTransportPrivate* priv;
} WebKitGstIceTransport;

typedef struct _WebKitGstIceTransportClass {
    GstWebRTCICETransportClass parentClass;
} WebKitGstIceTransportClass;

GST_DEBUG_CATEGORY(webkit_webrtc_ice_transport_debug);
#define GST_CAT_DEFAULT webkit_webrtc_ice_transport_debug

WEBKIT_DEFINE_TYPE_WITH_CODE(WebKitGstIceTransport, webkit_gst_webrtc_ice_transport, GST_TYPE_WEBRTC_ICE_TRANSPORT, GST_DEBUG_CATEGORY_INIT(webkit_webrtc_ice_transport_debug, "webkitwebrtcicetransport", 0, "WebRTC ICE transport"))

static void webkitGstWebRTCIceTransportFinalize(GObject* object)
{
    G_OBJECT_CLASS(webkit_gst_webrtc_ice_transport_parent_class)->finalize(object);
}

static void webkitGstWebRTCIceTransportConstructed(GObject* object)
{
    G_OBJECT_CLASS(webkit_gst_webrtc_ice_transport_parent_class)->constructed(object);

    // auto self = WEBKIT_GST_WEBRTC_ICE_TRANSPORT(object);
    auto ice = GST_WEBRTC_ICE_TRANSPORT(object);

    ice->sink = makeGStreamerElement("appsink"_s);
    ice->src = makeGStreamerElement("appsrc"_s);
}

static void webkit_gst_webrtc_ice_transport_class_init(WebKitGstIceTransportClass* klass)
{
    auto gobjectClass = G_OBJECT_CLASS(klass);
    gobjectClass->constructed = webkitGstWebRTCIceTransportConstructed;
    gobjectClass->finalize = webkitGstWebRTCIceTransportFinalize;
}

WebKitGstIceTransport* webkitGstWebRTCCreateIceTransport(WebKitGstIceAgent* agent, unsigned streamId, GstWebRTCICEComponent component)
{
    auto transport = reinterpret_cast<WebKitGstIceTransport*>(g_object_new(WEBKIT_TYPE_GST_WEBRTC_ICE_TRANSPORT, "component", component, nullptr));
    transport->priv->agent.reset(agent);
    transport->priv->streamId = streamId;
    return transport;
}

#undef GST_CAT_DEFAULT

#endif // USE(GSTREAMER_WEBRTC)
