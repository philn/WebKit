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
#include "GStreamerAudioRTPPacketizer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"
#include <gst/rtp/rtp.h>
#include <wtf/text/StringToIntegerConversion.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_webrtc_audio_rtp_packetizer_debug);
#define GST_CAT_DEFAULT webkit_webrtc_audio_rtp_packetizer_debug

RefPtr<GStreamerAudioRTPPacketizer> GStreamerAudioRTPPacketizer::create(RefPtr<UniqueSSRCGenerator> ssrcGenerator, const GstStructure* codecParameters, GUniquePtr<GstStructure>&& encodingParameters)
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_audio_rtp_packetizer_debug, "webkitwebrtcrtppacketizeraudio", 0, "WebKit WebRTC Audio RTP Packetizer");
    });

    GST_DEBUG("Creating packetizer for codec: %" GST_PTR_FORMAT " and encoding parameters %" GST_PTR_FORMAT, codecParameters, encodingParameters.get());
    const char* encodingName = gst_structure_get_string(codecParameters, "encoding-name");
    if (!encodingName)
        return nullptr;

    auto encoding = String(WTF::span(encodingName)).convertToASCIILowercase();
    GRefPtr<GstElement> payloader = makeGStreamerElement(makeString("rtp"_s, encoding, "pay"_s).ascii().data(), nullptr);
    if (UNLIKELY(!payloader)) {
        GST_ERROR("RTP payloader not found for encoding %s", encodingName);
        return nullptr;
    }

    auto inputCaps = adoptGRef(gst_caps_new_any());
    GUniquePtr<GstStructure> structure(gst_structure_copy(codecParameters));

    auto ssrc = ssrcGenerator->generateSSRC();
    if (ssrc != std::numeric_limits<uint32_t>::max())
        gst_structure_set(structure.get(), "ssrc", G_TYPE_UINT, ssrc, nullptr);

    GRefPtr<GstElement> encoder;
    if (encoding == "opus"_s) {
        encoder = makeGStreamerElement("opusenc", nullptr);
        if (!encoder)
            return nullptr;

        gst_structure_set(structure.get(), "encoding-name", G_TYPE_STRING, "OPUS", nullptr);

        // FIXME: Enable dtx too?
        gst_util_set_object_arg(G_OBJECT(encoder.get()), "audio-type", "voice");
        g_object_set(encoder.get(), "perfect-timestamp", TRUE, nullptr);

        if (const char* useInbandFec = gst_structure_get_string(structure.get(), "useinbandfec")) {
            if (!g_strcmp0(useInbandFec, "1"))
                g_object_set(encoder.get(), "inband-fec", true, nullptr);
            gst_structure_remove_field(structure.get(), "useinbandfec");
        }

        if (const char* isStereo = gst_structure_get_string(structure.get(), "stereo")) {
            if (!g_strcmp0(isStereo, "1"))
                inputCaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT, 2, nullptr));
            gst_structure_remove_field(structure.get(), "stereo");
        }

        if (gst_caps_is_any(inputCaps.get())) {
            if (const char* encodingParameters = gst_structure_get_string(structure.get(), "encoding-params")) {
                if (auto channels = parseIntegerAllowingTrailingJunk<int>(StringView::fromLatin1(encodingParameters)))
                    inputCaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT, *channels, nullptr));
            }
        }
    } else if (encoding == "g722"_s)
        encoder = makeGStreamerElement("avenc_g722", nullptr);
    else if (encoding == "pcma"_s)
        encoder = makeGStreamerElement("alawenc", nullptr);
    else if (encoding == "pcmu"_s)
        encoder = makeGStreamerElement("mulawenc", nullptr);
    else {
        GST_ERROR("Unsupported outgoing audio encoding: %s", encodingName);
        return nullptr;
    }

    if (!encoder) {
        GST_ERROR("Encoder not found for encoding %s", encodingName);
        return nullptr;
    }

    // Align MTU with libwebrtc implementation, also helping to reduce packet fragmentation.
    g_object_set(payloader.get(), "auto-header-extension", TRUE, "mtu", 1200, nullptr);

    int payloadType;
    if (gst_structure_get_int(codecParameters, "payload", &payloadType))
        g_object_set(payloader.get(), "pt", payloadType, nullptr);
    else if (gst_structure_get_int(encodingParameters.get(), "payload", &payloadType))
        g_object_set(payloader.get(), "pt", payloadType, nullptr);

    // TODO(philn): Restore payloader states, if any.
#if 0
    if (m_payloaderState) {
        g_object_set(m_payloader.get(), "seqnum-offset", m_payloaderState->seqnum, nullptr);
        m_payloaderState.reset();
    }
#endif

    auto rtpCaps = adoptGRef(gst_caps_new_empty());

    // When not present in caps, the vad support of the ssrc-audio-level extension should be
    // enabled. In order to prevent caps negotiation issues with downstream, explicitely set it.
    unsigned totalFields = gst_structure_n_fields(structure.get());
    for (unsigned i = 0; i < totalFields; i++) {
        auto fieldName = String(WTF::span(gst_structure_nth_field_name(structure.get(), i)));
        if (!fieldName.startsWith("extmap-"_s))
            continue;

        const auto value = gst_structure_get_value(structure.get(), fieldName.ascii().data());
        if (!G_VALUE_HOLDS_STRING(value))
            continue;

        const char* uri = g_value_get_string(value);
        if (!g_str_equal(uri, GST_RTP_HDREXT_BASE "ssrc-audio-level"))
            continue;

        GValue arrayValue G_VALUE_INIT;
        gst_value_array_init(&arrayValue, 3);

        GValue stringValue G_VALUE_INIT;
        g_value_init(&stringValue, G_TYPE_STRING);

        g_value_set_static_string(&stringValue, "");
        gst_value_array_append_value(&arrayValue, &stringValue);

        g_value_set_string(&stringValue, uri);
        gst_value_array_append_value(&arrayValue, &stringValue);

        g_value_set_static_string(&stringValue, "vad=on");
        gst_value_array_append_and_take_value(&arrayValue, &stringValue);

        gst_structure_remove_field(structure.get(), fieldName.ascii().data());
        gst_structure_take_value(structure.get(), fieldName.ascii().data(), &arrayValue);
    }

    gst_caps_append_structure(rtpCaps.get(), structure.release());
    return adoptRef(*new GStreamerAudioRTPPacketizer(WTFMove(inputCaps), WTFMove(encoder), WTFMove(payloader), WTFMove(encodingParameters), WTFMove(rtpCaps)));
}

GStreamerAudioRTPPacketizer::GStreamerAudioRTPPacketizer(GRefPtr<GstCaps>&& inputCaps, GRefPtr<GstElement>&& encoder, GRefPtr<GstElement>&& payloader, GUniquePtr<GstStructure>&& encodingParameters, GRefPtr<GstCaps>&& rtpCaps)
    : GStreamerRTPPacketizer(WTFMove(encoder), WTFMove(payloader), WTFMove(encodingParameters))
{
    g_object_set(m_capsFilter.get(), "caps", rtpCaps.get(), nullptr);
    GST_DEBUG_OBJECT(m_bin.get(), "RTP caps: %" GST_PTR_FORMAT, rtpCaps.get());

    m_audioconvert = makeGStreamerElement("audioconvert", nullptr);
    m_audioresample = makeGStreamerElement("audioresample", nullptr);
    m_inputCapsFilter = gst_element_factory_make("capsfilter", nullptr);
    g_object_set(m_inputCapsFilter.get(), "caps", inputCaps.get(), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_bin.get()), m_audioconvert.get(), m_audioresample.get(), m_inputCapsFilter.get(), nullptr);

    gst_element_link_many(m_inputQueue.get(), m_audioconvert.get(), m_audioresample.get(), m_inputCapsFilter.get(), m_encoder.get(), m_payloader.get(), m_capsFilter.get(), m_outputQueue.get(), nullptr);

    // TODO: Set some audio params from m_encodingParameters?
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
