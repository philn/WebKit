/*
 * Copyright (C) 2024 Igalia S.L
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
#include "GStreamerSourceBufferParser.h"

#if USE(GSTREAMER) && ENABLE(MEDIA_SOURCE)

#include "AudioTrackPrivateGStreamer.h"
#include "GStreamerCommon.h"
#include "GStreamerMediaDescription.h"
#include "InbandTextTrackPrivateGStreamer.h"
#include "MediaSampleGStreamer.h"
#include "VideoTrackPrivateGStreamer.h"
#include <gst/base/gsttypefindhelper.h>
#include <wtf/glib/RunLoopSourcePriority.h>

GST_DEBUG_CATEGORY(webkit_source_buffer_parser_debug);
#define GST_CAT_DEFAULT webkit_source_buffer_parser_debug

namespace WebCore {

GStreamerSourceBufferParser::GStreamerSourceBufferParser(SourceBufferPrivateGStreamer& sourceBufferPrivate, const RefPtr<MediaPlayerPrivateGStreamerMSE>& mediaPlayerPrivate)
    : m_sourceBufferPrivate(sourceBufferPrivate)
    , m_playerPrivate(mediaPlayerPrivate)
    , m_workQueue(WorkQueue::create("GStreamer MSE SourceBuffer Parser"_s))
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_source_buffer_parser_debug, "webkitmseparser", 0, "WebKit MSE SourceBuffer Parser");
    });
    initializeParserHarness();
}

void GStreamerSourceBufferParser::initializeParserHarness()
{
    static Atomic<uint64_t> parserCounter;
    auto elementName = makeString("sb-parser-"_s, makeStringByReplacingAll(m_sourceBufferPrivate.type().containerType(), '/', '-'), '-', parserCounter.exchangeAdd(1));
    GRefPtr<GstElement> parsebin = makeGStreamerElement("parsebin", elementName.ascii().data());

    // We don't want parsebin to autoplug decryptors, those will be used by the player pipeline instead.
    g_signal_connect(parsebin.get(), "autoplug-continue", G_CALLBACK(+[](GstElement*, GstPad*, GstCaps* caps, gpointer) -> gboolean {
        return !areEncryptedCaps(caps);
    }), nullptr);

    g_signal_connect(GST_BIN_CAST(parsebin.get()), "element-added", G_CALLBACK(+[](GstBin*, GstElement* element, gpointer) {
        GUniquePtr<char> name(gst_element_get_name(element));
        if (g_str_has_prefix(name.get(), "matroskademux")) {
            g_signal_connect(element, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer) {
                gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, reinterpret_cast<GstPadProbeCallback>(+[](GstPad*, GstPadProbeInfo* info, gpointer) -> GstPadProbeReturn {
                    // matroskademux sets GstSegment.start to the PTS of the first frame.
                    //
                    // This way in the unlikely case a user made a .mkv or .webm file where a certain portion of the movie is skipped
                    // (e.g. by concatenating a MSE initialization segment with any MSE media segment other than the first) and opened
                    // it with a regular player, playback would start immediately. GstSegment.duration is not modified in any case.
                    //
                    // Leaving the usefulness of that feature aside, the fact that it uses GstSegment.start is problematic for MSE.
                    // In MSE is not unusual to process unordered MSE media segments. In this case, a frame may have
                    // PTS <<< GstSegment.start and be discarded by downstream. This happens for instance in elements derived from
                    // audiobasefilter, such as opusparse.
                    //
                    // This probe remedies the problem by setting GstSegment.start to 0 in all cases, not only when the PTS of the first
                    // frame is zero.
                    ASSERT(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM);
                    auto event = GST_PAD_PROBE_INFO_EVENT(info);
                    if (GST_EVENT_TYPE(event) != GST_EVENT_SEGMENT)
                        return GST_PAD_PROBE_OK;

                    GstSegment segment;
                    gst_event_copy_segment(event, &segment);

                    segment.start = 0;

                    auto newEvent = adoptGRef(gst_event_new_segment(&segment));
                    gst_event_replace(reinterpret_cast<GstEvent**>(&GST_PAD_PROBE_INFO_DATA(info)), newEvent.get());
                    return GST_PAD_PROBE_OK;
                }) , nullptr, nullptr);
            }), nullptr);
        }
    }), nullptr);

    // Relay need-context messages from the internal demuxer to the player.
    m_bus = adoptGRef(gst_bus_new());
    gst_element_set_bus(parsebin.get(), m_bus.get());
    gst_bus_enable_sync_message_emission(m_bus.get());
    g_signal_connect(m_bus.get(), "sync-message::need-context", G_CALLBACK(+[](GstBus*, GstMessage* message, GStreamerSourceBufferParser* parser) {
        parser->m_workQueue->dispatch([weakPlayer = parser->m_playerPrivate, message = GRefPtr(message)] {
            RefPtr player = weakPlayer.get();
            if (!player)
                return;
            player->handleNeedContextMessage(message.get());
        });
    }), this);

    m_harness = GStreamerElementHarness::create(WTFMove(parsebin), [this](auto&, auto&& outputSample) {
        handleSample(WTFMove(outputSample));
    });
}

Ref<MediaPromise> GStreamerSourceBufferParser::pushNewBuffer(GRefPtr<GstBuffer>&& buffer)
{
    MediaPromise::Producer promise;

    if (!m_harness->inputCaps()) {
        const auto& containerType = m_sourceBufferPrivate.type().containerType();
        GRefPtr<GstCaps> caps;
        if (containerType.endsWith("mp4"_s) || containerType.endsWith("aac"_s))
            caps = adoptGRef(gst_caps_new_simple("video/quicktime", "variant", G_TYPE_STRING, "mse-bytestream", nullptr));
        else if (containerType == "audio/flac"_s)
            caps = adoptGRef(gst_caps_new_empty_simple("audio/x-flac"));
        else {
            caps = adoptGRef(gst_type_find_helper_for_buffer(GST_OBJECT_CAST(m_harness->element()), buffer.get(), nullptr));
            if (!caps) {
                GST_WARNING_OBJECT(m_harness->element(), "Unable to determine buffer media type");
                promise.reject(PlatformMediaError::ParsingError);
                return promise;
            }
        }
        m_harness->start(WTFMove(caps));
    }

    m_harness->pushBuffer(WTFMove(buffer));
    if (!processOutputEvents()) {
        promise.reject(PlatformMediaError::ParsingError);
        return promise;
    }

    m_harness->processOutputSamples();
    promise.resolve();
    return promise;
}

static void fixupStreamCollection(GstStreamCollection* collection)
{
    // Workaround for a parsebin bug, mislabelling encrypted streams as unknown ones. Fixed by:
    // https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/6138
    unsigned collectionSize = gst_stream_collection_get_size(collection);
    for (unsigned i = 0; i < collectionSize; i++) {
        auto stream = gst_stream_collection_get_stream(collection, i);
        if (gst_stream_get_stream_type(stream) != GST_STREAM_TYPE_UNKNOWN)
            continue;

        auto caps = adoptGRef(gst_stream_get_caps(stream));
        auto structure = gst_caps_get_structure(caps.get(), 0);
        if (const char* originalMediaType = gst_structure_get_string(structure, "original-media-type")) {
            if (g_str_has_prefix(originalMediaType, "audio"))
                gst_stream_set_stream_type(stream, GST_STREAM_TYPE_AUDIO);
            else if (g_str_has_prefix(originalMediaType, "video"))
                gst_stream_set_stream_type(stream, GST_STREAM_TYPE_VIDEO);
        }
    }
}

bool GStreamerSourceBufferParser::processOutputEvents()
{
    while (auto ownedMessage = adoptGRef(gst_bus_pop_filtered(m_bus.get(), GST_MESSAGE_ERROR)))
        return false;

    for (auto& stream : m_harness->outputStreams()) {
        while (auto event = stream->pullEvent()) {
            if (m_initializationSegment && GST_EVENT_TYPE(event.get()) == GST_EVENT_EOS) {
                GST_WARNING_OBJECT(m_harness->element(), "Stream topology changed");
                return false;
            }
            // FIXME: Process stream-collection updates?
            if (!m_initializationSegment && GST_EVENT_TYPE(event.get()) == GST_EVENT_STREAM_COLLECTION) {
                GstStreamCollection* collection = nullptr;
                gst_event_parse_stream_collection(event.get(), &collection);
                if (!webkitGstCheckVersion(1, 23, 0))
                    fixupStreamCollection(collection);
                notifyInitializationSegment(*collection);
            }
#if ENABLE(ENCRYPTED_MEDIA)
            if (GST_EVENT_TYPE(event.get()) == GST_EVENT_PROTECTION) {
                RefPtr player = m_playerPrivate.get();
                if (player)
                    player->handleProtectionEvent(event.get());
            }
#endif
        }
    }
    return true;
}

void GStreamerSourceBufferParser::resetParserState()
{
    GST_DEBUG_OBJECT(m_harness->element(), "Resetting parser state");
    initializeParserHarness();
}

void GStreamerSourceBufferParser::stopParser()
{
    GST_DEBUG_OBJECT(m_harness->element(), "Stopping");
    m_harness->reset();
}

void GStreamerSourceBufferParser::notifyInitializationSegment(GstStreamCollection& collection)
{
    SourceBufferPrivateClient::InitializationSegment initializationSegment;
    gint64 timeLength = 0;
    if (gst_element_query_duration(m_harness->element(), GST_FORMAT_TIME, &timeLength)
        && static_cast<guint64>(timeLength) != GST_CLOCK_TIME_NONE)
        initializationSegment.duration = MediaTime(GST_TIME_AS_USECONDS(timeLength), G_USEC_PER_SEC);
    else
        initializationSegment.duration = MediaTime::positiveInfiniteTime();

    unsigned collectionSize = gst_stream_collection_get_size(&collection);
    for (unsigned i = 0; i < collectionSize; i++) {
        auto stream = gst_stream_collection_get_stream(&collection, i);
        GST_DEBUG_OBJECT(m_harness->element(), "Creating new track for stream %" GST_PTR_FORMAT, stream);

        auto caps = adoptGRef(gst_stream_get_caps(stream));
        auto description = GStreamerMediaDescription::create(caps);
        switch (gst_stream_get_stream_type(stream)) {
        case GST_STREAM_TYPE_VIDEO: {
            RefPtr player = m_playerPrivate.get();
            if (player && doCapsHaveType(caps.get(), GST_VIDEO_CAPS_TYPE_PREFIX))
                player->setInitialVideoSize(getVideoResolutionFromCaps(caps.get()).value_or(FloatSize()));

            auto track = VideoTrackPrivateGStreamer::create(m_playerPrivate.get(), i, stream);
            track->setInitialCaps(WTFMove(caps));
            initializationSegment.videoTracks.append({ .description = WTFMove(description), .track = WTFMove(track) });
            break;
        }
        case GST_STREAM_TYPE_AUDIO: {
            auto track = AudioTrackPrivateGStreamer::create(m_playerPrivate.get(), i, stream);
            track->setInitialCaps(WTFMove(caps));
            initializationSegment.audioTracks.append({ .description = WTFMove(description), .track = WTFMove(track) });
            break;
        }
        case GST_STREAM_TYPE_TEXT: {
            auto track = InbandTextTrackPrivateGStreamer::create(m_playerPrivate.get(), i, stream);
            track->setInitialCaps(WTFMove(caps));
            initializationSegment.textTracks.append({ .description = WTFMove(description), .track = WTFMove(track) });
            break;
        }
        default:
            break;
        };
    }
    m_initializationSegment = WTFMove(initializationSegment);
    // FIXME: Relay entire stream-collection to SourceBufferPrivateGStreamer so that it can be
    // directly re-used by the msesrc?
    SourceBufferPrivateClient::InitializationSegment segment = m_initializationSegment.value();
    m_sourceBufferPrivate.didReceiveInitializationSegment(WTFMove(segment));
}

void GStreamerSourceBufferParser::handleSample(GRefPtr<GstSample>&& outputSample)
{
    auto outputBuffer = gst_sample_get_buffer(outputSample.get());
    auto outputCaps = gst_sample_get_caps(outputSample.get());
    if (doCapsHaveType(outputCaps, "audio/x-vorbis") && !GST_BUFFER_PTS_IS_VALID(outputBuffer)) {
        // When demuxing Vorbis, matroskademux creates several PTS-less frames with header information. We don't need those.
        GST_DEBUG_OBJECT(m_harness->element(), "Ignoring sample without PTS: %" GST_PTR_FORMAT, outputBuffer);
        return;
    }

    FloatSize presentationSize;
    String videoDebugInfo;
    if (doCapsHaveType(outputCaps, GST_VIDEO_CAPS_TYPE_PREFIX)) {
        presentationSize = getVideoResolutionFromCaps(outputCaps).value_or(FloatSize());
        videoDebugInfo = makeString("presentationSize="_s, presentationSize.width(), 'x', presentationSize.height());
    }

    // Workaround for lack of stream-collection updates, specially when switching from/to (un)encrypted content.
    TrackID trackId;
    if (doCapsHaveType(outputCaps, GST_AUDIO_CAPS_TYPE_PREFIX))
        trackId = m_initializationSegment->audioTracks[0].track->id();
    else if (doCapsHaveType(outputCaps, GST_VIDEO_CAPS_TYPE_PREFIX))
        trackId = m_initializationSegment->videoTracks[0].track->id();
    else
        trackId = m_initializationSegment->textTracks[0].track->id();

    auto mediaSample = MediaSampleGStreamer::create(WTFMove(outputSample), presentationSize, trackId);
    GST_TRACE_OBJECT(m_harness->element(), "append: trackId=%" PRIu64 " PTS=%" GST_TIME_FORMAT " DUR=%s %s",
        mediaSample->trackID(), GST_TIME_ARGS(toGstClockTime(mediaSample->presentationTime())),
        mediaSample->duration().toString().utf8().data(), videoDebugInfo.ascii().data());
    m_sourceBufferPrivate.didReceiveSample(WTFMove(mediaSample));
}

} // namespace WebCore

#undef GST_CAT_DEFAULT

#endif // USE(GSTREAMER) && ENABLE(MEDIA_SOURCE)
