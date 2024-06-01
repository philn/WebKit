/*
 *  Copyright (C) 2024 Igalia S.L. All rights reserved.
 *  Copyright (C) 2024 Metrological Group B.V.
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
#include "GStreamerRTPPacketizer.h"

#if USE(GSTREAMER_WEBRTC)

#include <wtf/text/WTFString.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_webrtc_rtp_packetizer_debug);
#define GST_CAT_DEFAULT webkit_webrtc_rtp_packetizer_debug

GStreamerRTPPacketizer::GStreamerRTPPacketizer(GRefPtr<GstElement>&& encoder, GRefPtr<GstElement>&& payloader, GUniquePtr<GstStructure>&& encodingParameters)
    : m_encoder(WTFMove(encoder))
    , m_payloader(WTFMove(payloader))
    , m_encodingParameters(WTFMove(encodingParameters))
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_rtp_packetizer_debug, "webkitwebrtcrtppacketizer", 0, "WebKit WebRTC RTP Packetizer");
    });

    static Atomic<uint64_t> counter = 0;
    m_bin = gst_bin_new(makeString("rtp-packetizer-"_s, counter.exchangeAdd(1)).ascii().data());

    m_inputQueue = gst_element_factory_make("queue", nullptr);
    m_outputQueue = gst_element_factory_make("queue", nullptr);
    m_capsFilter = gst_element_factory_make("capsfilter", nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_bin.get()), m_inputQueue.get(), m_encoder.get(), m_payloader.get(), m_capsFilter.get(), m_outputQueue.get(), nullptr);
    auto sinkPad = adoptGRef(gst_element_get_static_pad(m_inputQueue.get(), "sink"));
    gst_element_add_pad(m_bin.get(), gst_ghost_pad_new("sink", sinkPad.get()));
    auto srcPad = adoptGRef(gst_element_get_static_pad(m_outputQueue.get(), "src"));
    gst_element_add_pad(m_bin.get(), gst_ghost_pad_new("src", srcPad.get()));
}

GStreamerRTPPacketizer::~GStreamerRTPPacketizer()
{
    // TODO: cleanup bin?
}

void GStreamerRTPPacketizer::stop()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Stopping");
    Locker locker { m_eosLock };
    // We have a linked encoder/payloader, so to replace them we need to block upstream data flow,
    // send an EOS event to the first element we want to remove (the encoder) and wait it reaches
    // the payloader source pad. Then we can unlink/clean-up elements.
    auto srcPad = adoptGRef(gst_element_get_static_pad(m_inputQueue.get(), "src"));
    m_padBlockedProbe = gst_pad_add_probe(srcPad.get(), GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, reinterpret_cast<GstPadProbeCallback>(+[](GstPad* pad, GstPadProbeInfo* info, gpointer userData) -> GstPadProbeReturn {
        gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));

        auto self = reinterpret_cast<GStreamerRTPPacketizer*>(userData);
        auto srcPad = adoptGRef(gst_element_get_static_pad(self->m_payloader.get(), "src"));
        gst_pad_add_probe(srcPad.get(), static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM), reinterpret_cast<GstPadProbeCallback>(+[](GstPad* pad, GstPadProbeInfo* info, gpointer userData) -> GstPadProbeReturn {
            if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_DATA(info)) != GST_EVENT_EOS)
                return GST_PAD_PROBE_OK;

            gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));

            auto self = reinterpret_cast<GStreamerRTPPacketizer*>(userData);
            GST_DEBUG_OBJECT(self->m_bin.get(), "EOS event received");

            PayloaderState state;
            g_object_get(self->m_payloader.get(), "seqnum", &state.seqnum, nullptr);
            if (state.seqnum < 65535)
                state.seqnum++;
            self->m_payloaderState = state;

            self->m_padBlockedProbe = 0;
            {
                Locker locker { self->m_eosLock };
                self->m_eosCondition.notifyAll();
            }
            return GST_PAD_PROBE_DROP;
        }), userData, nullptr);

        auto sinkPad = adoptGRef(gst_element_get_static_pad(self->m_encoder.get(), "sink"));
        gst_pad_send_event(sinkPad.get(), gst_event_new_eos());
        return GST_PAD_PROBE_OK;
    }), this, nullptr);
    m_eosCondition.wait(m_eosLock);
    GST_DEBUG_OBJECT(m_bin.get(), "Stopped");
}

String GStreamerRTPPacketizer::rtpStreamId() const
{
    if (!m_encodingParameters)
        return emptyString();

    if (const char* rid = gst_structure_get_string(m_encodingParameters.get(), "rid"))
        return String(WTF::span(rid));

    return emptyString();
}

int GStreamerRTPPacketizer::payloadType() const
{
    int payloadType;
    g_object_get(m_payloader.get(), "pt", &payloadType, nullptr);
    return payloadType;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
