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

#pragma once

#if USE(GSTREAMER_WEBRTC)

#include "RealtimeIncomingSourceGStreamer.h"
#include <gst/gst.h>
#include <gst/rtp/rtp.h>

/* namespace WebCore { */
/* class RealtimeIncomingSourceGStreamer; */
/* } */

#define GSTREAMER_RTP_TRANSFORMER(o) (G_TYPE_CHECK_INSTANCE_CAST((o), GSTREAMER_TYPE_RTP_TRANSFORMER, GStreamerRtpTransformer))
#define WEBKIT_IS_MEDIA_STREAM_SRC(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GSTREAMER_TYPE_RTP_TRANSFORMER))
#define GSTREAMER_RTP_TRANSFORMER_CAST(o) ((GStreamerRtpTransformer*)o)

#define GSTREAMER_TYPE_RTP_TRANSFORMER (gstreamer_rtp_transformer_get_type())
GType gstreamer_rtp_transformer_get_type(void);

typedef struct _GStreamerRtpTransformer GStreamerRtpTransformer;
typedef struct _GStreamerRtpTransformerClass GStreamerRtpTransformerClass;
typedef struct _GStreamerRtpTransformerPrivate GStreamerRtpTransformerPrivate;

struct _GStreamerRtpTransformer {
    GstRTPBaseDepayload parent;
    GStreamerRtpTransformerPrivate* priv;
};

struct _GStreamerRtpTransformerClass {
    GstRTPBaseDepayloadClass parentClass;
};

GstElement* gstreamerRtpTransformerNew(WebCore::RealtimeIncomingSourceGStreamer&);

#endif // USE(GSTREAMER_WEBRTC)
