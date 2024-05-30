/*
 *  Copyright (C) 2017-2022 Igalia S.L. All rights reserved.
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
#include "RealtimeOutgoingVideoSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"
#include "GStreamerRegistryScanner.h"
#include "GStreamerVideoRTPPacketizer.h"
#include "MediaStreamTrack.h"
#include <wtf/glib/WTFGType.h>
#include <wtf/text/StringToIntegerConversion.h>

GST_DEBUG_CATEGORY(webkit_webrtc_outgoing_video_debug);
#define GST_CAT_DEFAULT webkit_webrtc_outgoing_video_debug

namespace WebCore {

struct RealtimeOutgoingVideoSourceHolder {
    RefPtr<RealtimeOutgoingVideoSourceGStreamer> source;
};
WEBKIT_DEFINE_ASYNC_DATA_STRUCT(RealtimeOutgoingVideoSourceHolder)

RealtimeOutgoingVideoSourceGStreamer::RealtimeOutgoingVideoSourceGStreamer(const RefPtr<UniqueSSRCGenerator>& ssrcGenerator, const String& mediaStreamId, MediaStreamTrack& track)
    : RealtimeOutgoingMediaSourceGStreamer(RealtimeOutgoingMediaSourceGStreamer::Type::Video, ssrcGenerator, mediaStreamId, track)
{
    initializePreProcessor();
}

RealtimeOutgoingVideoSourceGStreamer::RealtimeOutgoingVideoSourceGStreamer(const RefPtr<UniqueSSRCGenerator>& ssrcGenerator)
    : RealtimeOutgoingMediaSourceGStreamer(RealtimeOutgoingMediaSourceGStreamer::Type::Video, ssrcGenerator)
{
    initializePreProcessor();
    // connectFallbackSource();
}

void RealtimeOutgoingVideoSourceGStreamer::initializePreProcessor()
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_outgoing_video_debug, "webkitwebrtcoutgoingvideo", 0, "WebKit WebRTC outgoing video");
    });
    registerWebKitGStreamerElements();

    static Atomic<uint64_t> sourceCounter = 0;
    gst_element_set_name(m_bin.get(), makeString("outgoing-video-source-"_s, sourceCounter.exchangeAdd(1)).ascii().data());

    m_stats.reset(gst_structure_new_empty("webrtc-outgoing-video-stats"));
    startUpdatingStats();

    auto videoConvert = makeGStreamerElement("videoconvert", nullptr);

    m_preProcessor = gst_bin_new(nullptr);

    auto videoFlip = makeGStreamerElement("videoflip", nullptr);
    gst_util_set_object_arg(G_OBJECT(videoFlip), "method", "automatic");
    gst_bin_add_many(GST_BIN_CAST(m_preProcessor.get()), videoFlip, videoConvert, nullptr);
    gst_element_link(videoFlip, videoConvert);

    if (auto pad = adoptGRef(gst_bin_find_unlinked_pad(GST_BIN_CAST(m_preProcessor.get()), GST_PAD_SRC)))
        gst_element_add_pad(GST_ELEMENT_CAST(m_preProcessor.get()), gst_ghost_pad_new("src", pad.get()));
    if (auto pad = adoptGRef(gst_bin_find_unlinked_pad(GST_BIN_CAST(m_preProcessor.get()), GST_PAD_SINK)))
        gst_element_add_pad(GST_ELEMENT_CAST(m_preProcessor.get()), gst_ghost_pad_new("sink", pad.get()));

    gst_bin_add(GST_BIN_CAST(m_bin.get()), m_preProcessor.get());
}

RTCRtpCapabilities RealtimeOutgoingVideoSourceGStreamer::rtpCapabilities() const
{
    auto& registryScanner = GStreamerRegistryScanner::singleton();
    return registryScanner.videoRtpCapabilities(GStreamerRegistryScanner::Configuration::Encoding);
}

void RealtimeOutgoingVideoSourceGStreamer::updateStats(GstBuffer*)
{
    uint64_t framesSent = 0;
    gst_structure_get_uint64(m_stats.get(), "frames-sent", &framesSent);
    framesSent++;
#if 0
    if (m_encoder) {
        uint32_t bitrate;
        g_object_get(m_encoder.get(), "bitrate", &bitrate, nullptr);
        gst_structure_set(m_stats.get(), "bitrate", G_TYPE_DOUBLE, static_cast<double>(bitrate * 1000), nullptr);
    }
#endif
    gst_structure_set(m_stats.get(), "frames-sent", G_TYPE_UINT64, framesSent, "frames-encoded", G_TYPE_UINT64, framesSent, nullptr);
}

void RealtimeOutgoingVideoSourceGStreamer::teardown()
{
    RealtimeOutgoingMediaSourceGStreamer::teardown();
    stopUpdatingStats();
    m_stats.reset();
}

GRefPtr<GstPad> RealtimeOutgoingVideoSourceGStreamer::outgoingSourcePad() const
{
    auto srcPad = adoptGRef(gst_element_get_static_pad(m_outgoingSource.get(), "video_src0"));
    return srcPad;
}

RefPtr<GStreamerRTPPacketizer> RealtimeOutgoingVideoSourceGStreamer::createPacketizer(RefPtr<UniqueSSRCGenerator> ssrcGenerator, const GstStructure* codecParameters, GUniquePtr<GstStructure>&& encodingParameters)
{
    return GStreamerVideoRTPPacketizer::create(ssrcGenerator, codecParameters, WTFMove(encodingParameters));
}

void RealtimeOutgoingVideoSourceGStreamer::connectFallbackSource()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Connecting fallback video source");
    if (!m_fallbackPad) {
        m_fallbackSource = makeGStreamerElement("videotestsrc", nullptr);
        if (!m_fallbackSource) {
            WTFLogAlways("Unable to connect fallback videotestsrc element, expect broken behavior. Please install gst-plugins-base.");
            return;
        }

        gst_util_set_object_arg(G_OBJECT(m_fallbackSource.get()), "pattern", "black");

        gst_bin_add(GST_BIN_CAST(m_bin.get()), m_fallbackSource.get());

        m_fallbackPad = adoptGRef(gst_element_request_pad_simple(m_inputSelector.get(), "sink_%u"));

        auto srcPad = adoptGRef(gst_element_get_static_pad(m_fallbackSource.get(), "src"));
        gst_pad_link(srcPad.get(), m_fallbackPad.get());
        gst_element_sync_state_with_parent(m_fallbackSource.get());
    }

    g_object_set(m_inputSelector.get(), "active-pad", m_fallbackPad.get(), nullptr);
}

void RealtimeOutgoingVideoSourceGStreamer::unlinkOutgoingSource()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Unlinking outgoing video source");
    if (m_statsPadProbeId) {
        auto binSrcPad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
        gst_pad_remove_probe(binSrcPad.get(), m_statsPadProbeId);
        m_statsPadProbeId = 0;
    }

    auto srcPad = outgoingSourcePad();
    auto peerPad = adoptGRef(gst_pad_get_peer(srcPad.get()));
    if (!peerPad) {
        GST_DEBUG_OBJECT(m_bin.get(), "Outgoing video source not linked");
        return;
    }

    gst_pad_unlink(srcPad.get(), peerPad.get());
    gst_element_release_request_pad(m_inputSelector.get(), peerPad.get());
}

void RealtimeOutgoingVideoSourceGStreamer::linkOutgoingSource()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Linking outgoing video source");
    auto srcPad = outgoingSourcePad();
    if (gst_pad_is_linked(srcPad.get())) {
        return;
    }
    auto sinkPad = adoptGRef(gst_element_request_pad_simple(m_inputSelector.get(), "sink_%u"));
    gst_pad_link(srcPad.get(), sinkPad.get());
    g_object_set(m_inputSelector.get(), "active-pad", sinkPad.get(), nullptr);

    flush();
}

void RealtimeOutgoingVideoSourceGStreamer::startUpdatingStats()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Starting buffer monitoring for stats gathering");
    auto holder = createRealtimeOutgoingVideoSourceHolder();
    holder->source = this;
    auto pad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
    m_statsPadProbeId = gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER, [](GstPad*, GstPadProbeInfo* info, gpointer userData) -> GstPadProbeReturn {
        auto* holder = static_cast<RealtimeOutgoingVideoSourceHolder*>(userData);
        auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        holder->source->updateStats(buffer);
        return GST_PAD_PROBE_OK;
    }, holder, reinterpret_cast<GDestroyNotify>(destroyRealtimeOutgoingVideoSourceHolder));
}

void RealtimeOutgoingVideoSourceGStreamer::stopUpdatingStats()
{
    if (!m_statsPadProbeId)
        return;

    GST_DEBUG_OBJECT(m_bin.get(), "Stopping buffer monitoring for stats gathering");
    auto binSrcPad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
    gst_pad_remove_probe(binSrcPad.get(), m_statsPadProbeId);
    m_statsPadProbeId = 0;
}

void RealtimeOutgoingVideoSourceGStreamer::sourceEnabledChanged()
{
    RealtimeOutgoingMediaSourceGStreamer::sourceEnabledChanged();
    if (m_enabled)
        startUpdatingStats();
    else
        stopUpdatingStats();
}

void RealtimeOutgoingVideoSourceGStreamer::flush()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Requesting key-frame");
    gst_element_send_event(m_outgoingSource.get(), gst_video_event_new_downstream_force_key_unit(GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, FALSE, 1));
}

void RealtimeOutgoingVideoSourceGStreamer::setParameters(GUniquePtr<GstStructure>&& parameters)
{
    m_parameters = WTFMove(parameters);
    GST_DEBUG_OBJECT(m_bin.get(), "New encoding parameters: %" GST_PTR_FORMAT, m_parameters.get());
}
#if 0
void RealtimeOutgoingVideoSourceGStreamer::fillEncodingParameters(const GUniquePtr<GstStructure>& encodingParameters)
{
    if (m_videoRate) {
        GRefPtr<GstCaps> caps;
        g_object_get(m_frameRateCapsFilter.get(), "caps", &caps.outPtr(), nullptr);
        double maxFrameRate = 30.0;
        if (!gst_caps_is_any(caps.get())) {
            if (auto* structure = gst_caps_get_structure(caps.get(), 0)) {
                int numerator, denominator;
                if (gst_structure_get_fraction(structure, "framerate", &numerator, &denominator))
                    gst_util_fraction_to_double(numerator, denominator, &maxFrameRate);
            }
        }

        gst_structure_set(encodingParameters.get(), "max-framerate", G_TYPE_DOUBLE, maxFrameRate, nullptr);
    }

    unsigned long maxBitrate = 2048 * 1000;
    if (m_encoder) {
        uint32_t bitrate;
        g_object_get(m_encoder.get(), "bitrate", &bitrate, nullptr);
        maxBitrate = bitrate * 1000;
    }

    gst_structure_set(encodingParameters.get(), "max-bitrate", G_TYPE_ULONG, maxBitrate, nullptr);
}
#endif

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
