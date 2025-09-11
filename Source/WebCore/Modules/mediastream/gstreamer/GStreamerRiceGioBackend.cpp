/*
 *  Copyright (C) 2025 Igalia S.L. All rights reserved.
 *  Copyright (C) 2025 Metrological Group B.V.
 *  Copyright (C) 2024 Matthew Waters <matthew@centricular.com>
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

#if USE(GSTREAMER_WEBRTC) && USE(LIBRICE)

#include "GStreamerRiceGioBackend.h"
#include "GStreamerIceUtilities.h"
#include <gst/gst.h>
#include <rice-proto.h>
#include <wtf/MonotonicTime.h>

#define GST_CAT_DEFAULT gst_webrtc_rice_gio_debug
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);

using namespace::WebCore;

static void init_debug()
{
    static gsize _init = 0;

    if (g_once_init_enter(&_init)) {
        GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "webkitwebrtcricegio", 0, "webkitwebrtcricegio");
        g_once_init_leave(&_init, 1);
    }
}

struct _AgentSource
{
    GSource source;

    GThreadSafeWeakPtr<WebKitGstIceAgent> agent;
    gboolean complete;
};

static gboolean agent_source_prepare(GSource* base, gint* timeout)
{
    auto source = reinterpret_cast<AgentSource*>(base);
    auto iceAgent = source->agent.get();
    if (!iceAgent)
        return FALSE;

    const auto& agent = webkitGstWebRTCIceAgentGetRiceAgent(iceAgent.get());
    auto now = WTF::MonotonicTime::now().secondsSinceEpoch();

    gboolean result = FALSE;
    while (true) {
        RiceAgentPoll ret;
        rice_agent_poll_init(&ret);
        GST_TRACE_OBJECT(iceAgent.get(), "Polling");
        rice_agent_poll(agent.get(), now.nanoseconds(), &ret);
        GST_TRACE_OBJECT(iceAgent.get(), "Polling DONE");
        switch (ret.tag) {
        case RICE_AGENT_POLL_CLOSED:
            GST_TRACE_OBJECT(iceAgent.get(), "Agent closed!");
            source->complete = TRUE;
            rice_agent_poll_clear(&ret);
            webkitGstWebRTCIceAgentClosed(iceAgent.get());
            return TRUE;
        case RICE_AGENT_POLL_COMPONENT_STATE_CHANGE:
            GST_TRACE_OBJECT(iceAgent.get(), "Component state changed");
            webkitGstWebRTCIceAgentComponentStateChangedForStream(iceAgent.get(), ret.component_state_change.stream_id, ret.component_state_change);
            result = TRUE;
            break;
        case RICE_AGENT_POLL_ALLOCATE_SOCKET:
            /* FIXME: handle */
            g_assert_not_reached();
            result = TRUE;
            break;
        case RICE_AGENT_POLL_REMOVE_SOCKET:
            GST_FIXME("remove socket is not handled");
            result = TRUE;
            break;
        case RICE_AGENT_POLL_WAIT_UNTIL_NANOS:
            *timeout = static_cast<int>(ret.wait_until_nanos - now.nanoseconds());
            // gst_printerrln("-> agent %p %zu now: %f timeout: %d", agent.get(), ret.wait_until_nanos, now.nanoseconds(), *timeout);
            GST_TRACE_OBJECT(iceAgent.get(), "Waiting for %d", *timeout);
            ASSERT(*timeout >= 0);
            // if (*timeout < 0)
            //     *timeout = 0;
            result = FALSE;
            break;
        case RICE_AGENT_POLL_GATHERING_COMPLETE:
            GST_TRACE_OBJECT(iceAgent.get(), "Gathering complete");
            webkitGstWebRTCIceAgentGatheringDoneForStream(iceAgent.get(), ret.gathering_complete.stream_id);
            break;
        case RICE_AGENT_POLL_GATHERED_CANDIDATE:
            GST_TRACE_OBJECT(iceAgent.get(), "Gathered candidate");
            webkitGstWebRTCIceAgentLocalCandidateGatheredForStream(iceAgent.get(), ret.gathered_candidate.stream_id, ret.gathered_candidate);
            result = TRUE;
            break;
        case RICE_AGENT_POLL_SELECTED_PAIR:
            webkitGstWebRTCIceAgentNewSelectedPairForStream(iceAgent.get(), ret.selected_pair.stream_id, ret.selected_pair);
            result = TRUE;
            break;
        };
        rice_agent_poll_clear(&ret);

        RiceTransmit transmit;
        rice_transmit_init(&transmit);
        rice_agent_poll_transmit(agent.get(), now.nanoseconds(), &transmit);
        if (transmit.from && transmit.to) {
            std::span<const uint8_t> data { transmit.data.ptr, transmit.data.size };
            auto from = riceAddressToString(transmit.from);
            auto to = riceAddressToString(transmit.to);
            webkitGstWebRTCIceAgentSend(iceAgent.get(), transmit.stream_id, RTCIceProtocol::Udp, from, to, data);
            result = TRUE;
        }
        rice_transmit_clear(&transmit);
        if (!result)
            break;
    }

    return result;
}

static gboolean agent_source_check(GSource*)
{
    return TRUE;
}

static gboolean agent_source_dispatch(GSource* base, GSourceFunc callback, gpointer data)
{
    auto source = reinterpret_cast<AgentSource*>(base);

    if (callback)
        callback(data);

    return !source->complete;
}

static void agent_source_finalize(GSource*)
{
}

static GSourceFuncs agent_event_source_funcs = {
    agent_source_prepare,
    agent_source_check,
    agent_source_dispatch,
    agent_source_finalize,
    nullptr, nullptr
};

GSource* agent_source_new(GThreadSafeWeakPtr<WebKitGstIceAgent>&& agent)
{
  AgentSource* source;

  init_debug();

  source = reinterpret_cast<AgentSource*>(g_source_new(&agent_event_source_funcs, sizeof(AgentSource)));
  if (auto iceAgent = agent.get()) [[likely]]
      source->agent.reset(iceAgent.get());
  source->complete = FALSE;

  return reinterpret_cast<GSource*>(source);
}

#undef GST_CAT_DEFAULT

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBRICE)
