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
#include "RealtimeOutgoingAudioSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerAudioRTPPacketizer.h"
#include "GStreamerCommon.h"
#include "GStreamerRegistryScanner.h"
#include "MediaStreamTrack.h"

GST_DEBUG_CATEGORY(webkit_webrtc_outgoing_audio_debug);
#define GST_CAT_DEFAULT webkit_webrtc_outgoing_audio_debug

namespace WebCore {

RealtimeOutgoingAudioSourceGStreamer::RealtimeOutgoingAudioSourceGStreamer(const RefPtr<UniqueSSRCGenerator>& ssrcGenerator, const String& mediaStreamId, MediaStreamTrack& track)
    : RealtimeOutgoingMediaSourceGStreamer(RealtimeOutgoingMediaSourceGStreamer::Type::Audio, ssrcGenerator, mediaStreamId, track)
{
    initializePreProcessor();
}

RealtimeOutgoingAudioSourceGStreamer::RealtimeOutgoingAudioSourceGStreamer(const RefPtr<UniqueSSRCGenerator>& ssrcGenerator)
    : RealtimeOutgoingMediaSourceGStreamer(RealtimeOutgoingMediaSourceGStreamer::Type::Audio, ssrcGenerator)
{
    initializePreProcessor();
    // connectFallbackSource();
}

void RealtimeOutgoingAudioSourceGStreamer::initializePreProcessor()
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_outgoing_audio_debug, "webkitwebrtcoutgoingaudio", 0, "WebKit WebRTC outgoing audio");
    });
    static Atomic<uint64_t> sourceCounter = 0;
    gst_element_set_name(m_bin.get(), makeString("outgoing-audio-source-"_s, sourceCounter.exchangeAdd(1)).ascii().data());
    m_preProcessor = gst_element_factory_make("identity", nullptr);
    gst_bin_add(GST_BIN_CAST(m_bin.get()), m_preProcessor.get());
}

RTCRtpCapabilities RealtimeOutgoingAudioSourceGStreamer::rtpCapabilities() const
{
    auto& registryScanner = GStreamerRegistryScanner::singleton();
    return registryScanner.audioRtpCapabilities(GStreamerRegistryScanner::Configuration::Encoding);
}

GRefPtr<GstPad> RealtimeOutgoingAudioSourceGStreamer::outgoingSourcePad() const
{
    auto srcPad = adoptGRef(gst_element_get_static_pad(m_outgoingSource.get(), "audio_src0"));
    return srcPad;
}

RefPtr<GStreamerRTPPacketizer> RealtimeOutgoingAudioSourceGStreamer::createPacketizer(RefPtr<UniqueSSRCGenerator> ssrcGenerator, const GstStructure* codecParameters, GUniquePtr<GstStructure>&& encodingParameters)
{
    return GStreamerAudioRTPPacketizer::create(ssrcGenerator, codecParameters, WTFMove(encodingParameters));
}

void RealtimeOutgoingAudioSourceGStreamer::connectFallbackSource()
{
    if (!m_fallbackPad) {
        m_fallbackSource = makeGStreamerElement("audiotestsrc", nullptr);
        if (!m_fallbackSource) {
            WTFLogAlways("Unable to connect fallback audiotestsrc element, expect broken behavior. Please install gst-plugins-base.");
            return;
        }

        gst_util_set_object_arg(G_OBJECT(m_fallbackSource.get()), "wave", "silence");

        gst_bin_add(GST_BIN_CAST(m_bin.get()), m_fallbackSource.get());

        m_fallbackPad = adoptGRef(gst_element_request_pad_simple(m_inputSelector.get(), "sink_%u"));

        auto srcPad = adoptGRef(gst_element_get_static_pad(m_fallbackSource.get(), "src"));
        gst_pad_link(srcPad.get(), m_fallbackPad.get());
        gst_element_sync_state_with_parent(m_fallbackSource.get());
    }

    g_object_set(m_inputSelector.get(), "active-pad", m_fallbackPad.get(), nullptr);
}

void RealtimeOutgoingAudioSourceGStreamer::unlinkOutgoingSource()
{
    auto srcPad = outgoingSourcePad();
    auto peerPad = adoptGRef(gst_pad_get_peer(srcPad.get()));
    if (!peerPad)
        return;

    gst_pad_unlink(srcPad.get(), peerPad.get());
    gst_element_release_request_pad(m_inputSelector.get(), peerPad.get());
}

void RealtimeOutgoingAudioSourceGStreamer::linkOutgoingSource()
{
    auto srcPad = outgoingSourcePad();
    if (gst_pad_is_linked(srcPad.get()))
        return;
    auto sinkPad = adoptGRef(gst_element_request_pad_simple(m_inputSelector.get(), "sink_%u"));
    gst_pad_link(srcPad.get(), sinkPad.get());
    g_object_set(m_inputSelector.get(), "active-pad", sinkPad.get(), nullptr);
}

void RealtimeOutgoingAudioSourceGStreamer::setParameters(GUniquePtr<GstStructure>&& parameters)
{
    m_parameters = WTFMove(parameters);
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
