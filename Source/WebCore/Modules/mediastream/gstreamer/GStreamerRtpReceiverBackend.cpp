/*
 *  Copyright (C) 2019-2022 Igalia S.L. All rights reserved.
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
#include "GStreamerRtpReceiverBackend.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerDtlsTransportBackend.h"
#include "GStreamerRtpReceiverTransformBackend.h"
#include "GStreamerWebRTCUtils.h"
#include "NotImplemented.h"
#include "RealtimeIncomingAudioSourceGStreamer.h"
#include "RealtimeIncomingVideoSourceGStreamer.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/StringToIntegerConversion.h>

GST_DEBUG_CATEGORY(webkit_webrtc_rtp_receiver_debug);
#define GST_CAT_DEFAULT webkit_webrtc_rtp_receiver_debug

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(GStreamerRtpReceiverBackend);

GStreamerRtpReceiverBackend::GStreamerRtpReceiverBackend(GRefPtr<GstWebRTCRTPTransceiver>&& rtcTransceiver)
    : m_rtcTransceiver(WTFMove(rtcTransceiver))
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_rtp_receiver_debug, "webkitwebrtcrtpreceiver", 0, "WebKit WebRTC RTP Receiver");
    });

    g_object_get(m_rtcTransceiver.get(), "receiver", &m_rtcReceiver.outPtr(), nullptr);
}

RTCRtpParameters GStreamerRtpReceiverBackend::getParameters()
{
    RTCRtpParameters parameters;
    parameters.rtcp.reducedSize = true;

    // FIXME: Get this from transceiver codec-preferences?
    // parameters.codecs =
    //parameters.headerExtensions.append()

    GRefPtr<GstCaps> caps;
    g_object_get(m_rtcTransceiver.get(), "codec-preferences", &caps.outPtr(), nullptr);
    GST_DEBUG("phil -> %" GST_PTR_FORMAT, caps.get());
    if (!caps || gst_caps_is_any(caps.get()))
        return parameters;

    unsigned totalCodecs = gst_caps_get_size(caps.get());
    for (unsigned i = 0; i < totalCodecs; i++) {
        auto structure = gst_caps_get_structure(caps.get(), i);
        RTCRtpCodecParameters codec;
        if (auto pt = gstStructureGet<int>(structure, "payload"_s))
            codec.payloadType = *pt;

        auto media = gstStructureGetString(structure, "media"_s);
        auto encodingName = gstStructureGetString(structure, "encoding-name"_s);
        if (media && encodingName)
            codec.mimeType = makeString(media, '/', encodingName.convertToASCIILowercase());

        if (auto clockRate = gstStructureGet<uint64_t>(structure, "clock-rate"_s))
            codec.clockRate = *clockRate;

        if (auto channels = gstStructureGet<unsigned>(structure, "channels"_s))
            codec.channels = *channels;

        if (auto fmtpLine = gstStructureGetString(structure, "fmtp-line"_s))
            codec.sdpFmtpLine = fmtpLine.toString();

        parameters.codecs.append(WTFMove(codec));

        gst_structure_foreach(structure, [](GQuark quark, const GValue* value, gpointer userData) -> gboolean {
            auto name = StringView::fromLatin1(g_quark_to_string(quark));
            if (!name.startsWith("extmap-"_s))
                return TRUE;

            auto id = parseInteger<unsigned short>(name.toStringWithoutCopying().substring(7));
            if (!id)
                return TRUE;

            auto uri = String::fromLatin1(g_value_get_string(value));
            auto parameters = static_cast<RTCRtpParameters*>(userData);
            parameters->headerExtensions.append({ uri, *id });

            return TRUE;
        }, &parameters);
    }

    return parameters;
}

Vector<RTCRtpContributingSource> GStreamerRtpReceiverBackend::getContributingSources() const
{
    notImplemented();
    return { };
}

Vector<RTCRtpSynchronizationSource> GStreamerRtpReceiverBackend::getSynchronizationSources() const
{
    notImplemented();
    return { };
}

Ref<RealtimeMediaSource> GStreamerRtpReceiverBackend::createSource(const String& trackKind, const String& trackId)
{
    if (trackKind == "video"_s)
        return RealtimeIncomingVideoSourceGStreamer::create(AtomString { trackId });

    RELEASE_ASSERT(trackKind == "audio"_s);
    return RealtimeIncomingAudioSourceGStreamer::create(AtomString { trackId });
}

Ref<RTCRtpTransformBackend> GStreamerRtpReceiverBackend::rtcRtpTransformBackend()
{
    return GStreamerRtpReceiverTransformBackend::create(m_rtcReceiver);
}

std::unique_ptr<RTCDtlsTransportBackend> GStreamerRtpReceiverBackend::dtlsTransportBackend()
{
    GRefPtr<GstWebRTCDTLSTransport> transport;
    g_object_get(m_rtcReceiver.get(), "transport", &transport.outPtr(), nullptr);
    if (!transport)
        return nullptr;
    return makeUnique<GStreamerDtlsTransportBackend>(WTFMove(transport));
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
