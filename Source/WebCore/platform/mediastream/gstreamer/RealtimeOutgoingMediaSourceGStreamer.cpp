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
#include "RealtimeOutgoingMediaSourceGStreamer.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"
#include "GStreamerMediaStreamSource.h"
#include "MediaStreamTrack.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API

#include <wtf/UUID.h>
#include <wtf/text/StringToIntegerConversion.h>

GST_DEBUG_CATEGORY(webkit_webrtc_outgoing_media_debug);
#define GST_CAT_DEFAULT webkit_webrtc_outgoing_media_debug

namespace WebCore {

RealtimeOutgoingMediaSourceGStreamer::RealtimeOutgoingMediaSourceGStreamer(Type type, const RefPtr<UniqueSSRCGenerator>& ssrcGenerator, const String& mediaStreamId, MediaStreamTrack& track)
    : m_type(type)
    , m_mediaStreamId(mediaStreamId)
    , m_trackId(track.id())
    , m_ssrcGenerator(ssrcGenerator)
{
    initialize();

    // TODO: Move this to setSourceFromTrack().
    if (track.isCanvas()) {
        m_liveSync = makeGStreamerElement("livesync", nullptr);
        if (!m_liveSync) {
            GST_WARNING_OBJECT(m_bin.get(), "GStreamer element livesync not found. Canvas streaming to PeerConnection will not work as expected, falling back to identity element.");
            m_liveSync = gst_element_factory_make("identity", nullptr);
        }
    } else
        m_liveSync = gst_element_factory_make("identity", nullptr);
    gst_bin_add(GST_BIN_CAST(m_bin.get()), m_liveSync.get());

    setSourceFromTrack(track);

    m_outgoingSource = webkitMediaStreamSrcNew();
    GST_DEBUG_OBJECT(m_bin.get(), "Created outgoing source %" GST_PTR_FORMAT, m_outgoingSource.get());
    gst_bin_add(GST_BIN_CAST(m_bin.get()), m_outgoingSource.get());
    webkitMediaStreamSrcAddTrack(WEBKIT_MEDIA_STREAM_SRC(m_outgoingSource.get()), m_source->ptr());
}

RealtimeOutgoingMediaSourceGStreamer::RealtimeOutgoingMediaSourceGStreamer(Type type, const RefPtr<UniqueSSRCGenerator>& ssrcGenerator)
    : m_type(type)
    , m_mediaStreamId(createVersion4UUIDString())
    , m_trackId(emptyString())
    , m_ssrcGenerator(ssrcGenerator)
{
    initialize();
    m_liveSync = gst_element_factory_make("identity", nullptr);
    gst_bin_add(GST_BIN_CAST(m_bin.get()), m_liveSync.get());
}

RealtimeOutgoingMediaSourceGStreamer::~RealtimeOutgoingMediaSourceGStreamer()
{
    teardown();
}

void RealtimeOutgoingMediaSourceGStreamer::initialize()
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_outgoing_media_debug, "webkitwebrtcoutgoingmedia", 0, "WebKit WebRTC outgoing media");
    });

    m_bin = gst_bin_new(nullptr);

    m_inputSelector = gst_element_factory_make("input-selector", nullptr);
    m_tee = gst_element_factory_make("tee", nullptr);
    m_rtpFunnel = gst_element_factory_make("rtpfunnel", nullptr);
    m_rtpCapsfilter = gst_element_factory_make("capsfilter", nullptr);
    gst_bin_add_many(GST_BIN_CAST(m_bin.get()), m_inputSelector.get(), m_tee.get(), m_rtpFunnel.get(), m_rtpCapsfilter.get(), nullptr);
    gst_element_link(m_rtpFunnel.get(), m_rtpCapsfilter.get());

    auto srcPad = adoptGRef(gst_element_get_static_pad(m_rtpCapsfilter.get(), "src"));
    gst_element_add_pad(m_bin.get(), gst_ghost_pad_new("src", srcPad.get()));
}

const GRefPtr<GstCaps>& RealtimeOutgoingMediaSourceGStreamer::allowedCaps() const
{
    if (m_allowedCaps)
        return m_allowedCaps;

    auto sdpMsIdLine = makeString(m_mediaStreamId, ' ', m_trackId);
    m_allowedCaps = capsFromRtpCapabilities(rtpCapabilities(), [&sdpMsIdLine](GstStructure* structure) {
        gst_structure_set(structure, "a-msid", G_TYPE_STRING, sdpMsIdLine.utf8().data(), nullptr);
    });

    GST_DEBUG_OBJECT(m_bin.get(), "Allowed caps: %" GST_PTR_FORMAT, m_allowedCaps.get());
    return m_allowedCaps;
}

GRefPtr<GstCaps> RealtimeOutgoingMediaSourceGStreamer::rtpCaps() const
{
    GRefPtr<GstCaps> caps;
    g_object_get(m_rtpCapsfilter.get(), "caps", &caps.outPtr(), nullptr);
    return caps;
}

void RealtimeOutgoingMediaSourceGStreamer::setSourceFromTrack(MediaStreamTrack& track)
{
    Ref newSource = track.privateTrack();
    if (m_source && !m_initialSettings)
        m_initialSettings = m_source.value()->settings();

    GST_DEBUG_OBJECT(m_bin.get(), "Setting source to %s", newSource->id().utf8().data());

    if (m_source.has_value())
        stopOutgoingSource();

    // Both livesync and identity have a single-segment property, so no need for checks here.
    g_object_set(m_liveSync.get(), "single-segment", TRUE, nullptr);

    m_source = WTFMove(newSource);
    initializeSourceFromTrackPrivate();
}

void RealtimeOutgoingMediaSourceGStreamer::start()
{
    if (!m_isStopped) {
        GST_DEBUG_OBJECT(m_bin.get(), "Source already started");
        return;
    }

    GST_DEBUG_OBJECT(m_bin.get(), "Starting outgoing source");
    if (m_source)
        m_source.value()->addObserver(*this);
    m_isStopped = false;

    if (m_transceiver) {
        auto selectorSrcPad = adoptGRef(gst_element_get_static_pad(m_inputSelector.get(), "src"));
        if (!gst_pad_is_linked(selectorSrcPad.get())) {
            GST_DEBUG_OBJECT(m_bin.get(), "Codec preferences haven't changed before startup, ensuring source is linked");
            codecPreferencesChanged();
        }
    }

    if (m_outgoingSource)
        linkOutgoingSource();
    gst_element_sync_state_with_parent(m_bin.get());
}

void RealtimeOutgoingMediaSourceGStreamer::stop()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Stopping outgoing source");
    m_isStopped = true;
    if (!m_source)
        return;

    connectFallbackSource();

    stopOutgoingSource();
    m_source.reset();
}

void RealtimeOutgoingMediaSourceGStreamer::flush()
{
    gst_element_send_event(m_outgoingSource.get(), gst_event_new_flush_start());
    gst_element_send_event(m_outgoingSource.get(), gst_event_new_flush_stop(FALSE));
}

void RealtimeOutgoingMediaSourceGStreamer::stopOutgoingSource()
{
    if (!m_source)
        return;

    GST_DEBUG_OBJECT(m_bin.get(), "Stopping outgoing source %" GST_PTR_FORMAT, m_outgoingSource.get());
    m_source.value()->removeObserver(*this);

    if (!m_outgoingSource)
        return;

    webkitMediaStreamSrcSignalEndOfStream(WEBKIT_MEDIA_STREAM_SRC(m_outgoingSource.get()));

    gst_element_set_locked_state(m_outgoingSource.get(), TRUE);

    unlinkOutgoingSource();

    gst_element_set_state(m_outgoingSource.get(), GST_STATE_NULL);
    gst_bin_remove(GST_BIN_CAST(m_bin.get()), m_outgoingSource.get());
    gst_element_set_locked_state(m_outgoingSource.get(), FALSE);
    m_outgoingSource.clear();
}

void RealtimeOutgoingMediaSourceGStreamer::sourceMutedChanged()
{
    if (!m_source)
        return;
    ASSERT(m_muted != m_source.value()->muted());
    m_muted = m_source.value()->muted();
    GST_DEBUG_OBJECT(m_bin.get(), "Mute state changed to %s", boolForPrinting(m_muted));
}

void RealtimeOutgoingMediaSourceGStreamer::sourceEnabledChanged()
{
    if (!m_source)
        return;

    m_enabled = m_source.value()->enabled();
    GST_DEBUG_OBJECT(m_bin.get(), "Enabled state changed to %s", boolForPrinting(m_enabled));
}

void RealtimeOutgoingMediaSourceGStreamer::initializeSourceFromTrackPrivate()
{
    ASSERT(m_source);
    m_muted = m_source.value()->muted();
    m_enabled = m_source.value()->enabled();
    GST_DEBUG_OBJECT(m_bin.get(), "Initialized from track, muted: %s, enabled: %s", boolForPrinting(m_muted), boolForPrinting(m_enabled));
}

void RealtimeOutgoingMediaSourceGStreamer::link()
{
    GST_DEBUG_OBJECT(m_bin.get(), "Linking webrtcbin pad %" GST_PTR_FORMAT, m_webrtcSinkPad.get());

    auto srcPad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
    gst_pad_link(srcPad.get(), m_webrtcSinkPad.get());
}

void RealtimeOutgoingMediaSourceGStreamer::setSinkPad(GRefPtr<GstPad>&& pad)
{
    GST_DEBUG_OBJECT(m_bin.get(), "Associating with webrtcbin pad %" GST_PTR_FORMAT, pad.get());
    m_webrtcSinkPad = WTFMove(pad);

    if (m_transceiver)
        g_signal_handlers_disconnect_by_data(m_transceiver.get(), this);

    g_object_get(m_webrtcSinkPad.get(), "transceiver", &m_transceiver.outPtr(), nullptr);
    g_signal_connect_swapped(m_transceiver.get(), "notify::codec-preferences", G_CALLBACK(+[](RealtimeOutgoingMediaSourceGStreamer* source) {
        source->codecPreferencesChanged();
    }), this);
    g_object_get(m_transceiver.get(), "sender", &m_sender.outPtr(), nullptr);
}

GUniquePtr<GstStructure> RealtimeOutgoingMediaSourceGStreamer::parameters()
{
#if 0
    if (!m_parameters) {
        auto transactionId = createVersion4UUIDString();
        m_parameters.reset(gst_structure_new("send-parameters", "transaction-id", G_TYPE_STRING, transactionId.ascii().data(), nullptr));

        GUniquePtr<GstStructure> encodingParameters(gst_structure_new("encoding-parameters", "active", G_TYPE_BOOLEAN, TRUE, nullptr));

        if (m_payloader) {
            uint32_t ssrc;
            g_object_get(m_payloader.get(), "ssrc", &ssrc, nullptr);
            gst_structure_set(encodingParameters.get(), "ssrc", G_TYPE_UINT, ssrc, nullptr);
        }
        fillEncodingParameters(encodingParameters);

        GValue encodingsValue = G_VALUE_INIT;
        g_value_init(&encodingsValue, GST_TYPE_LIST);
        GValue value = G_VALUE_INIT;
        g_value_init(&value, GST_TYPE_STRUCTURE);
        gst_value_set_structure(&value, encodingParameters.get());
        gst_value_list_append_value(&encodingsValue, &value);
        g_value_unset(&value);
        gst_structure_take_value(m_parameters.get(), "encodings", &encodingsValue);
    }
 #endif
    return GUniquePtr<GstStructure>(gst_structure_copy(m_parameters.get()));
}

void RealtimeOutgoingMediaSourceGStreamer::teardown()
{
    if (m_transceiver)
        g_signal_handlers_disconnect_by_data(m_transceiver.get(), this);

    if (m_fallbackSource) {
        gst_element_set_locked_state(m_fallbackSource.get(), TRUE);
        gst_element_set_state(m_fallbackSource.get(), GST_STATE_READY);
        gst_element_unlink(m_fallbackSource.get(), m_inputSelector.get());
        gst_element_set_state(m_fallbackSource.get(), GST_STATE_NULL);
        gst_element_release_request_pad(m_inputSelector.get(), m_fallbackPad.get());
        gst_element_set_locked_state(m_fallbackSource.get(), FALSE);
    }

    stopOutgoingSource();

    if (GST_IS_PAD(m_webrtcSinkPad.get())) {
        auto srcPad = adoptGRef(gst_element_get_static_pad(m_bin.get(), "src"));
        if (gst_pad_unlink(srcPad.get(), m_webrtcSinkPad.get())) {
            GST_DEBUG_OBJECT(m_bin.get(), "Removing webrtcbin pad %" GST_PTR_FORMAT, m_webrtcSinkPad.get());
            if (auto parent = adoptGRef(gst_pad_get_parent_element(m_webrtcSinkPad.get())))
                gst_element_release_request_pad(parent.get(), m_webrtcSinkPad.get());
        }
    }

    gst_element_set_locked_state(m_bin.get(), TRUE);
    gst_element_set_state(m_bin.get(), GST_STATE_NULL);
    if (auto pipeline = adoptGRef(gst_element_get_parent(m_bin.get())))
        gst_bin_remove(GST_BIN_CAST(pipeline.get()), m_bin.get());
    gst_element_set_locked_state(m_bin.get(), FALSE);

    m_bin.clear();
    m_liveSync.clear();
    m_preProcessor.clear();
    m_inputSelector.clear();
    m_fallbackPad.clear();
    m_allowedCaps.clear();
    m_transceiver.clear();
    m_sender.clear();
    m_webrtcSinkPad.clear();
    m_parameters.reset();
    m_fallbackSource.clear();
}

void RealtimeOutgoingMediaSourceGStreamer::codecPreferencesChanged()
{
    GRefPtr<GstCaps> codecPreferences;
    g_object_get(m_transceiver.get(), "codec-preferences", &codecPreferences.outPtr(), nullptr);
    GST_DEBUG_OBJECT(m_bin.get(), "Codec preferences changed on transceiver %" GST_PTR_FORMAT " to: %" GST_PTR_FORMAT, m_transceiver.get(), codecPreferences.get());

    while (!m_packetizers.isEmpty()) {
        RefPtr packetizer = m_packetizers.takeLast();
        packetizer->stop();
        // TODO(philn):
        // - aggregate payloader states
        auto bin = packetizer->bin();
        gst_element_set_state(bin, GST_STATE_NULL);
        gst_element_unlink_many(m_tee.get(), bin, m_rtpFunnel.get(), nullptr);
        gst_bin_remove(GST_BIN_CAST(m_bin.get()), bin);
    }

    if (!configurePacketizers(WTFMove(codecPreferences))) {
        GST_ERROR_OBJECT(m_bin.get(), "Unable to link encoder to webrtcbin");
        return;
    }

    // TODO(philn): Restore payloader states, if any.
#if 0
    if (m_payloaderState) {
        g_object_set(payloader.get(), "seqnum-offset", m_payloaderState->seqnum, nullptr);
        m_payloaderState.reset();
    }
#endif

    gst_bin_sync_children_states(GST_BIN_CAST(m_bin.get()));
    gst_element_sync_state_with_parent(m_bin.get());
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_bin.get()), GST_DEBUG_GRAPH_SHOW_ALL, "outgoing-media-new-codec-prefs");
    m_isStopped = false;
}

void RealtimeOutgoingMediaSourceGStreamer::setInitialParameters(GUniquePtr<GstStructure>&& parameters)
{
    m_parameters = WTFMove(parameters);
    GST_DEBUG_OBJECT(m_bin.get(), "Initial encoding parameters: %" GST_PTR_FORMAT, m_parameters.get());

    const auto encodingsValue = gst_structure_get_value(m_parameters.get(), "encodings");
    RELEASE_ASSERT(GST_VALUE_HOLDS_LIST(encodingsValue));
    unsigned encodingsSize = gst_value_list_get_size(encodingsValue);
    if (UNLIKELY(!encodingsSize)) {
        GST_WARNING_OBJECT(m_bin.get(), "Encodings list is empty, cancelling configuration");
        return;
    }

    GRefPtr<GstCaps> allowedCaps(this->allowedCaps());
    configurePacketizers(WTFMove(allowedCaps));
}

bool RealtimeOutgoingMediaSourceGStreamer::linkPacketizer(RefPtr<GStreamerRTPPacketizer>&& packetizer)
{
    auto packetizerBin = packetizer->bin();
    gst_bin_add(GST_BIN_CAST(m_bin.get()), packetizerBin);

    if (!gst_element_link_many(m_tee.get(), packetizerBin, m_rtpFunnel.get(), nullptr)) {
        GST_ERROR_OBJECT(m_bin.get(), "Unable to link outgoing source and packetizer to RTP funnel");
        gst_bin_remove(GST_BIN_CAST(m_bin.get()), packetizerBin);
        return false;
    }
    m_packetizers.append(WTFMove(packetizer));
    return true;
}

bool RealtimeOutgoingMediaSourceGStreamer::configurePacketizers(GRefPtr<GstCaps>&& codecPreferences)
{
    if (UNLIKELY(gst_caps_is_empty(codecPreferences.get()) || gst_caps_is_any(codecPreferences.get())))
        return false;

    if (m_outgoingSource) {
        auto srcPad = outgoingSourcePad();
        if (!gst_pad_is_linked(srcPad.get())) {
            gst_element_link(m_outgoingSource.get(), m_inputSelector.get());
            auto sinkPad = adoptGRef(gst_pad_get_peer(srcPad.get()));
            g_object_set(m_inputSelector.get(), "active-pad", sinkPad.get(), nullptr);
        }
    }

    auto teeSinkPad = adoptGRef(gst_element_get_static_pad(m_tee.get(), "sink"));
    if (!gst_pad_is_linked(teeSinkPad.get()) && !gst_element_link_many(m_inputSelector.get(), m_liveSync.get(), m_preProcessor.get(), m_tee.get(), nullptr)) {
        GST_ERROR_OBJECT(m_bin.get(), "Unable to link input selector to tee");
        return false;
    }

    auto rtpCaps = adoptGRef(gst_caps_new_empty());
    unsigned totalCodecs = gst_caps_get_size(codecPreferences.get());
    for (unsigned i = 0; i < totalCodecs; i++) {
        const auto codecParameters = gst_caps_get_structure(codecPreferences.get(), i);

        if (m_parameters) {
            const auto encodingsValue = gst_structure_get_value(m_parameters.get(), "encodings");
            RELEASE_ASSERT(GST_VALUE_HOLDS_LIST(encodingsValue));
            auto totalEncodings = gst_value_list_get_size(encodingsValue);
            if (UNLIKELY(!totalEncodings)) {
                auto packetizer = createPacketizer(m_ssrcGenerator, codecParameters, nullptr);
                if (!packetizer)
                    continue;

                if (linkPacketizer(WTFMove(packetizer))) {
                    gst_caps_append_structure(rtpCaps.get(), gst_structure_copy(codecParameters));
                    break;
                }
            }

            bool codecIsValid = false;
            for (unsigned j = 0; j < totalEncodings; j++) {
                auto encoding = gst_value_list_get_value(encodingsValue, j);
                RELEASE_ASSERT(GST_VALUE_HOLDS_STRUCTURE(encoding));
                GUniquePtr<GstStructure> encodingParameters(gst_structure_copy(gst_value_get_structure(encoding)));
                auto packetizer = createPacketizer(m_ssrcGenerator, codecParameters, WTFMove(encodingParameters));
                if (!packetizer)
                    continue;

                codecIsValid = linkPacketizer(WTFMove(packetizer));
                if (!codecIsValid)
                    break;
            }

            // TODO: Check optional "codecs" field.

            if (codecIsValid) {
                gst_caps_append_structure(rtpCaps.get(), gst_structure_copy(codecParameters));
                break;
            }
        } else {
            auto packetizer = createPacketizer(m_ssrcGenerator, codecParameters, nullptr);
            if (!packetizer)
                continue;

            if (linkPacketizer(WTFMove(packetizer))) {
                gst_caps_append_structure(rtpCaps.get(), gst_structure_copy(codecParameters));
                break;
            }
        }
    }
    if (m_packetizers.isEmpty()) {
        GST_ERROR_OBJECT(m_bin.get(), "Unable to link any packetizer");
        return false;
    }

    auto structure = gst_caps_get_structure(rtpCaps.get(), 0);

    int payloadType;
    if (!gst_structure_get_int(structure, "payload", &payloadType)) {
        auto& firstPacketizer = m_packetizers.first();
        gst_structure_set(structure, "payload", G_TYPE_INT, firstPacketizer->payloadType(), nullptr);
    }

    StringBuilder simulcastBuilder;
    const char* direction = "send";
    simulcastBuilder.append(span(direction));
    simulcastBuilder.append(' ');
    unsigned totalStreams = 0;
    for (auto& packetizer : m_packetizers) {
        auto rtpStreamId = packetizer->rtpStreamId();
        if (rtpStreamId.isEmpty())
            continue;

        if (totalStreams > 0)
            simulcastBuilder.append(';');
        simulcastBuilder.append(rtpStreamId);
        gst_structure_set(structure, makeString("rid-"_s, rtpStreamId).ascii().data(), G_TYPE_STRING, direction, nullptr);
        totalStreams++;
    }

    if (totalStreams) {
        struct ExtensionLookupResults {
            bool hasRtpStreamIdExtension { false };
            bool hasRtpRepairedStreamIdExtension { false };
            int lastIdentifier { 0 };
        };
        ExtensionLookupResults lookupResults;
        gst_structure_foreach(structure, [](GQuark quark, const GValue* value, gpointer userData) -> gboolean {
            auto name = makeString(span(g_quark_to_string(quark)));
            if (!name.startsWith("extmap-"_s))
                return TRUE;

            auto identifier = WTF::parseInteger<int>(name.substring(8));
            if (UNLIKELY(!identifier))
                return TRUE;

            auto* lookupResults = reinterpret_cast<ExtensionLookupResults*>(userData);
            lookupResults->lastIdentifier = *identifier;

            String uri;
            if (G_VALUE_HOLDS_STRING(value))
                uri = makeString(span(g_value_get_string(value)));
            else if (GST_VALUE_HOLDS_ARRAY(value)) {
                const auto uriValue = gst_value_array_get_value(value, 1);
                uri = makeString(span(g_value_get_string(uriValue)));
            } else
                return TRUE;

            if (uri == makeString(span(GST_RTP_HDREXT_BASE "sdes:rtp-stream-id")))
                lookupResults->hasRtpStreamIdExtension = true;
            if (uri == makeString(span(GST_RTP_HDREXT_BASE "sdes:repaired-rtp-stream-id")))
                lookupResults->hasRtpRepairedStreamIdExtension = true;

            return TRUE;
        }, &lookupResults);

        if (!lookupResults.hasRtpStreamIdExtension) {
            lookupResults.lastIdentifier++;
            auto extensionIdentifier = makeString("extmap-"_s, lookupResults.lastIdentifier);
            gst_structure_set(structure, extensionIdentifier.ascii().data(), G_TYPE_STRING, GST_RTP_HDREXT_BASE "sdes:rtp-stream-id", nullptr);
        }
        if (!lookupResults.hasRtpRepairedStreamIdExtension) {
            lookupResults.lastIdentifier++;
            auto extensionIdentifier = makeString("extmap-"_s, lookupResults.lastIdentifier);
            gst_structure_set(structure, extensionIdentifier.ascii().data(), G_TYPE_STRING, GST_RTP_HDREXT_BASE "sdes:repaired-rtp-stream-id", nullptr);
        }

        //FIXME? "a-mid", G_TYPE_STRING, "video0", "extmap-2", G_TYPE_STRING, GST_RTP_HDREXT_BASE "sdes:mid",
        gst_structure_set(structure, "a-simulcast", G_TYPE_STRING, simulcastBuilder.toString().ascii().data(), nullptr);
        GST_DEBUG_OBJECT(m_bin.get(), "Setting simulcast caps: %" GST_PTR_FORMAT, rtpCaps.get());
    }
    GST_DEBUG_OBJECT(m_bin.get(), "Setting RTP funnel caps to %" GST_PTR_FORMAT, rtpCaps.get());
    g_object_set(m_rtpCapsfilter.get(), "caps", rtpCaps.get(), nullptr);
    return true;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
