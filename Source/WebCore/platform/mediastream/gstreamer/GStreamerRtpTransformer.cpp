/*
 * Copyright (C) 2023 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "GStreamerRtpTransformer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerWebRTCUtils.h"
#include "RealtimeIncomingSourceGStreamer.h"
#include <wtf/glib/WTFGType.h>

using namespace WebCore;

static GstStaticPadTemplate sinkTemplate = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("ANY"));

static GstStaticPadTemplate srcTemplate = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("ANY"));

GST_DEBUG_CATEGORY_STATIC(gstRtpTransformerDebug);
#define GST_CAT_DEFAULT gstRtpTransformerDebug

enum {
    PROP_0,
    PROP_LAST
};

struct _GStreamerRtpTransformerPrivate {
    RealtimeIncomingSourceGStreamer* incomingSource;
};

#define gstreamer_rtp_transformer_parent_class parent_class
WEBKIT_DEFINE_TYPE_WITH_CODE(GStreamerRtpTransformer, gstreamer_rtp_transformer, GST_TYPE_RTP_BASE_DEPAYLOAD, GST_DEBUG_CATEGORY_INIT(gstRtpTransformerDebug, "webkitrtptransformer", 0, "RTP transformer"));

static void gstreamerRtpTransformerConstructed(GObject* object)
{
    GST_CALL_PARENT(G_OBJECT_CLASS, constructed, (object));
    auto* self = GSTREAMER_RTP_TRANSFORMER_CAST(object);
    auto* priv = self->priv;

    // GST_OBJECT_FLAG_SET(GST_OBJECT_CAST(self), static_cast<GstElementFlags>(GST_ELEMENT_FLAG_SOURCE | static_cast<GstElementFlags>(GST_BIN_FLAG_STREAMS_AWARE)));
    // gst_bin_set_suppressed_flags(GST_BIN_CAST(self), static_cast<GstElementFlags>(GST_ELEMENT_FLAG_SOURCE | GST_ELEMENT_FLAG_SINK));

    // priv->mediaStreamObserver = makeUnique<WebKitMediaStreamObserver>(GST_ELEMENT_CAST(self));
    // priv->flowCombiner = GUniquePtr<GstFlowCombiner>(gst_flow_combiner_new());
}

static GstBuffer* gstreamerRtpTransformerProcessRtpPacket(GstRTPBaseDepayload* depayload, GstRTPBuffer* inputRtpBuffer)
{
    auto* self = GSTREAMER_RTP_TRANSFORMER_CAST(depayload);
    auto* payload = gst_rtp_buffer_get_payload_buffer(inputRtpBuffer);
    auto payloadLength = gst_rtp_buffer_get_payload_len(inputRtpBuffer);
    auto csrcCount = gst_rtp_buffer_get_csrc_count(inputRtpBuffer);
    auto timestamp = gst_rtp_buffer_get_timestamp(inputRtpBuffer);

    auto writablePayload = adoptGRef(gst_buffer_make_writable(payload));
    if (auto transformedPayload = self->priv->incomingSource->transform(WTFMove(writablePayload))) {
        auto transformedPacket = adoptGRef(gst_rtp_buffer_new_allocate(payloadLength, 0, csrcCount));
        {
            GstMappedRtpBuffer writableBuffer(transformedPacket.get(), GST_MAP_WRITE);
            auto* outputRtpBuffer = writableBuffer.mappedData();
            gst_rtp_buffer_set_marker(outputRtpBuffer, gst_rtp_buffer_get_marker(inputRtpBuffer));
            gst_rtp_buffer_set_payload_type(outputRtpBuffer, gst_rtp_buffer_get_payload_type(inputRtpBuffer));
            gst_rtp_buffer_set_seq(outputRtpBuffer, gst_rtp_buffer_get_seq(inputRtpBuffer));
            gst_rtp_buffer_set_timestamp(outputRtpBuffer, timestamp);
            gst_rtp_buffer_set_ssrc(outputRtpBuffer, gst_rtp_buffer_get_ssrc(inputRtpBuffer));
            for (auto i = 0; i < csrcCount; ++i)
                gst_rtp_buffer_set_csrc(outputRtpBuffer, i, gst_rtp_buffer_get_csrc((inputRtpBuffer), i));

            // TODO: copy header extensions data.
        }
        transformedPacket = adoptGRef(gst_buffer_append(transformedPacket.get(), transformedPayload.leakRef()));
        return transformedPacket.leakRef();
        // mappedBuffer.unmapEarly();
        // GST_PAD_PROBE_INFO_DATA(info) = transformedPacket.leakRef();
        // GST_PAD_PROBE_INFO_DATA(info) = transformedPayload.leakRef();
        // GstMappedBuffer mappedBuffer(GST_BUFFER_CAST(GST_PAD_PROBE_INFO_DATA(info)), GST_MAP_READ);
        // GST_MEMDUMP_OBJECT(source.m_bin.get(), "Transformed buffer", mappedBuffer.data(), mappedBuffer.size());
    }

    return nullptr;
}

static void gstreamer_rtp_transformer_class_init(GStreamerRtpTransformerClass* klass)
{
    GObjectClass* gobjectClass = G_OBJECT_CLASS(klass);
    GstElementClass* gstElementClass = GST_ELEMENT_CLASS(klass);
    GstRTPBaseDepayloadClass* gstRtpBaseDepayloadClass = GST_RTP_BASE_DEPAYLOAD_CLASS(klass);

    gobjectClass->constructed = gstreamerRtpTransformerConstructed;
    // gobjectClass->dispose = webkitMediaStreamSrcDispose;
    // gobjectClass->get_property = webkitMediaStreamSrcGetProperty;
    // gobjectClass->set_property = webkitMediaStreamSrcSetProperty;

    // g_object_class_install_property(gobjectClass, PROP_IS_LIVE, g_param_spec_boolean("is-live", nullptr, nullptr,
    //     TRUE, static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    // gstElementClass->change_state = GST_DEBUG_FUNCPTR(webkitMediaStreamSrcChangeState);

    gstRtpBaseDepayloadClass->process_rtp_packet = GST_DEBUG_FUNCPTR(gstreamerRtpTransformerProcessRtpPacket);

    gst_element_class_add_pad_template(gstElementClass, gst_static_pad_template_get(&sinkTemplate));
    gst_element_class_add_pad_template(gstElementClass, gst_static_pad_template_get(&srcTemplate));
}

GstElement* gstreamerRtpTransformerNew(RealtimeIncomingSourceGStreamer& incomingSource)
{
    auto* element = GST_ELEMENT_CAST(g_object_new(gstreamer_rtp_transformer_get_type(), nullptr));
    auto* self = GSTREAMER_RTP_TRANSFORMER_CAST(element);
    self->priv->incomingSource = &incomingSource;
    return element;
}

#undef GST_CAT_DEFAULT

#endif // USE(GSTREAMER_WEBRTC)
