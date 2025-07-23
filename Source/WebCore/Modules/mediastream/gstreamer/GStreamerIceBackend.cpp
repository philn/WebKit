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
#include "GStreamerIceBackend.h"

#if USE(GSTREAMER_WEBRTC)

#include "ScriptExecutionContext.h"
#include "SocketProvider.h"
#include <gst/webrtc/ice.h>
#include <gst/webrtc/webrtc.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/text/WTFString.h>

using namespace WTF;
using namespace WebCore;

typedef struct _WebKitGstIceBackendPrivate {
    RefPtr<GStreamerIceBackend> iceBackend;
} WebKitGstIceBackendPrivate;

typedef struct _WebKitGstIceBackend {
    GstWebRTCICE parent;
    WebKitGstIceBackendPrivate* priv;
} WebKitGstIceBackend;

typedef struct _WebKitGstIceBackendClass {
    GstWebRTCICEClass parentClass;
} WebKitGstIceBackendClass;

GST_DEBUG_CATEGORY(webkit_webrtc_ice_backend_debug);
#define GST_CAT_DEFAULT webkit_webrtc_ice_backend_debug

WEBKIT_DEFINE_TYPE_WITH_CODE(WebKitGstIceBackend, webkit_gst_webrtc_ice_backend, GST_TYPE_WEBRTC_ICE, GST_DEBUG_CATEGORY_INIT(webkit_webrtc_ice_backend_debug, "webkitwebrtcicebackend", 0, "WebRTC ICE backend"))

static void webkitGstWebRTCIceBackendSetOnIceCandidate(GstWebRTCICE* ice,
    GstWebRTCICEOnCandidateFunc func,
    gpointer user_data,
    GDestroyNotify notify)
{
    // TODO
}

static void webkitGstWebRTCIceBackendSetForceRelay(GstWebRTCICE* ice, gboolean forceRelay)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    if (!backend->priv->iceBackend)
        return;

    backend->priv->iceBackend->setForceRelay(forceRelay);

}

static gboolean webkitGstWebRTCIceBackendAddTurnServer(GstWebRTCICE* ice, const gchar* uri)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    if (!backend->priv->iceBackend)
        return FALSE;

    backend->priv->iceBackend->addTurnServer(String::fromUTF8(uri));
    return TRUE;
}

static void webkitGstWebRTCIceBackendFinalize(GObject* object)
{
    G_OBJECT_CLASS(webkit_gst_webrtc_ice_backend_parent_class)->finalize(object);
}

static void webkitGstWebRTCIceBackendConstructed(GObject* object)
{
    G_OBJECT_CLASS(webkit_gst_webrtc_ice_backend_parent_class)->constructed(object);
}

static void webkit_gst_webrtc_ice_backend_class_init(WebKitGstIceBackendClass* klass)
{
    auto gobjectClass = G_OBJECT_CLASS(klass);
    gobjectClass->constructed = webkitGstWebRTCIceBackendConstructed;
    gobjectClass->finalize = webkitGstWebRTCIceBackendFinalize;

    auto iceClass = GST_WEBRTC_ICE_CLASS(klass);
    iceClass->set_on_ice_candidate = webkitGstWebRTCIceBackendSetOnIceCandidate;
    iceClass->set_force_relay = webkitGstWebRTCIceBackendSetForceRelay;
    iceClass->add_turn_server = webkitGstWebRTCIceBackendAddTurnServer;
}

RefPtr<GStreamerIceBackend> GStreamerIceBackend::create(SocketProvider& provider)
{
    return provider.createGStreamerIceBackend();
}

WebKitGstIceBackend* webkitGstWebRTCCreateIceBackend(ASCIILiteral name, ScriptExecutionContext* context)
{
    if (!context)
        return nullptr;

    RefPtr socketProvider = context->socketProvider();
    if (!socketProvider)
        return nullptr;

    auto backend = reinterpret_cast<WebKitGstIceBackend*>(g_object_new(WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, "name", name.characters(), nullptr));
    backend->priv->iceBackend = GStreamerIceBackend::create(*socketProvider);
    return backend;
}

#undef GST_CAT_DEFAULT

#endif // USE(GSTREAMER_WEBRTC)
