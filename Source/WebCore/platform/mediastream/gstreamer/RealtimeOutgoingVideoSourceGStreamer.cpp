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
#include <wtf/text/MakeString.h>
#include <wtf/text/StringToIntegerConversion.h>

GST_DEBUG_CATEGORY(webkit_webrtc_outgoing_video_debug);
#define GST_CAT_DEFAULT webkit_webrtc_outgoing_video_debug

namespace WebCore {

RealtimeOutgoingVideoSourceGStreamer::RealtimeOutgoingVideoSourceGStreamer(const RefPtr<UniqueSSRCGenerator>& ssrcGenerator, const String& mediaStreamId, MediaStreamTrack& track)
    : RealtimeOutgoingMediaSourceGStreamer(RealtimeOutgoingMediaSourceGStreamer::Type::Video, ssrcGenerator, mediaStreamId, track)
{
    initializePreProcessor();
}

RealtimeOutgoingVideoSourceGStreamer::RealtimeOutgoingVideoSourceGStreamer(const RefPtr<UniqueSSRCGenerator>& ssrcGenerator)
    : RealtimeOutgoingMediaSourceGStreamer(RealtimeOutgoingMediaSourceGStreamer::Type::Video, ssrcGenerator)
{
    initializePreProcessor();
}

RealtimeOutgoingVideoSourceGStreamer::~RealtimeOutgoingVideoSourceGStreamer() = default;

void RealtimeOutgoingVideoSourceGStreamer::initializePreProcessor()
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_outgoing_video_debug, "webkitwebrtcoutgoingvideo", 0, "WebKit WebRTC outgoing video");
    });
    registerWebKitGStreamerElements();

    static Atomic<uint64_t> sourceCounter = 0;
    gst_element_set_name(m_bin.get(), makeString("outgoing-video-source-"_s, sourceCounter.exchangeAdd(1)).ascii().data());

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

GRefPtr<GstPad> RealtimeOutgoingVideoSourceGStreamer::outgoingSourcePad() const
{
    return adoptGRef(gst_element_get_static_pad(m_outgoingSource.get(), "video_src0"));
}

RefPtr<GStreamerRTPPacketizer> RealtimeOutgoingVideoSourceGStreamer::createPacketizer(RefPtr<UniqueSSRCGenerator> ssrcGenerator, const GstStructure* codecParameters, GUniquePtr<GstStructure>&& encodingParameters)
{
    return GStreamerVideoRTPPacketizer::create(ssrcGenerator, codecParameters, WTFMove(encodingParameters));
}

void RealtimeOutgoingVideoSourceGStreamer::flush()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Requesting key-frame");
    gst_element_send_event(m_outgoingSource.get(), gst_video_event_new_downstream_force_key_unit(GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, FALSE, 1));
}

void RealtimeOutgoingVideoSourceGStreamer::appendExtraRTPHeaderExtensions(GstStructure* structure, int lastExtensionId)
{
    auto extensionIdentifier = makeString("extmap-"_s, lastExtensionId + 1);
    gst_structure_set(structure, extensionIdentifier.ascii().data(), G_TYPE_STRING, "urn:3gpp:video-orientation", nullptr);
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
