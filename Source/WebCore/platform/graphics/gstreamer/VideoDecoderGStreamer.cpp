/*
 * Copyright (C) 2022 Igalia S.L
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

#include "GStreamerCommon.h"
#include "VideoDecoderGStreamer.h"
#include "VideoFrameGStreamer.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#if ENABLE(WEB_CODECS) && USE(GSTREAMER)

using namespace WebCore;

GST_DEBUG_CATEGORY(webkit_video_decoder_debug);
#define GST_CAT_DEFAULT webkit_video_decoder_debug

GStreamerVideoDecoder::GStreamerVideoDecoder(const String& codecName, OutputCallback&& outputCallback, PostTaskCallback&& postTaskCallback)
    : m_outputCallback(WTFMove(outputCallback))
    , m_postTaskCallback(WTFMove(postTaskCallback))
{
    ensureGStreamerInitialized();

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_video_decoder_debug, "webkitvideodecoder", 0, "WebKit WebCodecs Video Decoder");
    });

    static uint32_t nPipeline = 0;
    auto pipelineName = makeString("webkit-video-decoder-pipeline-", nPipeline);
    m_pipeline = gst_pipeline_new(pipelineName.ascii().data());
    GST_DEBUG_OBJECT(m_pipeline.get(), "Initializing");

    auto bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_enable_sync_message_emission(bus.get());

    connectSimpleBusMessageCallback(m_pipeline.get(), [this](GstMessage* message) {
        handleMessage(message);
    });

    g_signal_connect_swapped(bus.get(), "sync-message::stream-collection", G_CALLBACK(+[](GStreamerVideoDecoder* decoder, GstMessage* message) {
        auto* decodebin = decoder->m_decodebin.get();
        if (!decodebin)
            return;

        GRefPtr<GstStreamCollection> collection;
        gst_message_parse_stream_collection(message, &collection.outPtr());
        if (!collection || GST_MESSAGE_SRC(message) != GST_OBJECT_CAST(decodebin))
            return;

        unsigned size = gst_stream_collection_get_size(collection.get());
        GST_DEBUG("Received STREAM_COLLECTION message with upstream id \"%s\" defining the following streams:", gst_stream_collection_get_upstream_id(collection.get()));

        GList* streams = nullptr;
        for (unsigned i = 0; i < size; i++) {
            auto* stream = gst_stream_collection_get_stream(collection.get(), i);
            auto streamType = gst_stream_get_stream_type(stream);
            const char* streamId = gst_stream_get_stream_id(stream);
            GST_DEBUG("#%u %s track with ID %s", i, gst_stream_type_get_name(streamType), streamId);
            if (streamType == GST_STREAM_TYPE_VIDEO) {
                streams = g_list_append(streams, const_cast<char*>(gst_stream_get_stream_id(stream)));
                break;
            }
        }
        if (streams) {
            gst_element_send_event(decodebin, gst_event_new_select_streams(streams));
            g_list_free(streams);
        }
    }), this);

    m_src = makeGStreamerElement("appsrc", nullptr);

    g_object_set(m_src.get(), "max-buffers", 2, nullptr);
    // if (codecName.startsWith("avc1"_s)) {
    //     auto caps = adoptGRef(gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", nullptr));
    //     gst_app_src_set_caps(GST_APP_SRC(m_src.get()), caps.get());
    // }

    // m_decodebin = makeGStreamerElement("decodebin3", nullptr);
    // g_signal_connect_swapped(m_decodebin.get(), "pad-added", G_CALLBACK(+[](GStreamerVideoDecoder* decoder, GstPad* pad) {
    //     decoder->connectPad(pad);
    // }), this);

    auto* parser = makeGStreamerElement("h264parse", nullptr);

    m_decodebin = makeGStreamerElement("avdec_h264", nullptr);

    m_videoconvert = makeGStreamerElement("videoconvert", nullptr);

    m_sink = makeGStreamerElement("appsink", nullptr);

    static GstAppSinkCallbacks callbacks = {
        nullptr,
        [](GstAppSink* sink, gpointer userData) -> GstFlowReturn {
            if (auto sample = adoptGRef(gst_app_sink_pull_preroll(sink)))
                static_cast<GStreamerVideoDecoder*>(userData)->processSample(WTFMove(sample));
            return gst_app_sink_is_eos(sink) ? GST_FLOW_EOS : GST_FLOW_OK;
        },
        [](GstAppSink* sink, gpointer userData) -> GstFlowReturn {
            if (auto sample = adoptGRef(gst_app_sink_pull_sample(sink)))
                static_cast<GStreamerVideoDecoder*>(userData)->processSample(WTFMove(sample));
            return gst_app_sink_is_eos(sink) ? GST_FLOW_EOS : GST_FLOW_OK;
        },
        [](GstAppSink* sink, gpointer userData) -> gboolean {
            if (auto event = adoptGRef(gst_app_sink_pull_object(sink)))
                return static_cast<GStreamerVideoDecoder*>(userData)->processEvent(WTFMove(event));
            return FALSE;
        },
        { nullptr }
    };
    gst_app_sink_set_callbacks(GST_APP_SINK(m_sink.get()), &callbacks, this, nullptr);

    auto caps = adoptGRef(gst_caps_from_string("video/x-raw, format=(string)RGBA"));
    g_object_set(m_sink.get(), "enable-last-sample", FALSE, "max-buffers", 1, "sync", false, "caps", caps.get(), nullptr);

    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_src.get(), parser, m_decodebin.get(), m_videoconvert.get(), m_sink.get(), nullptr);
    gst_element_link_many(m_src.get(), parser, m_decodebin.get(), m_videoconvert.get(), m_sink.get(),  nullptr);
    // gst_element_link(m_src.get(), m_decodebin.get());
    // gst_element_link(m_videoconvert.get(), m_sink.get());
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
}

GStreamerVideoDecoder::~GStreamerVideoDecoder()
{
    close();
}

void GStreamerVideoDecoder::decode(EncodedFrame&& frame, DecodeCallback&& callback)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Decoding frame");

    // if (frame.isKeyFrame) {
    //     gst_element_send_event(m_src.get(), gst_video_event_new_downstream_force_key_unit(toGstClockTime()));
    // }

    m_timestamp = frame.timestamp;
    m_duration = frame.duration;
    Vector<uint8_t> data { frame.data };
    gst_app_src_push_buffer(GST_APP_SRC_CAST(m_src.get()), gstBufferNewWrappedFast(fastMemDup(data.data(), data.sizeInBytes()), data.sizeInBytes()));

    // {
    //     LockHolder lock(m_sampleLock);
    //     m_sampleCondition.wait(m_sampleLock);
    // }

    m_postTaskCallback([protectedThis = Ref { *this }, callback = WTFMove(callback)]() mutable {
        if (protectedThis->m_isClosed)
            return;

        String result;
        // FIXME: fill result in case of error.
        callback(WTFMove(result));
    });
}

void GStreamerVideoDecoder::flush(Function<void()>&& callback)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Flushing");
    gst_element_send_event(m_src.get(), gst_event_new_flush_start());
    gst_element_send_event(m_src.get(), gst_event_new_flush_stop(FALSE));

    {
        LockHolder lock(m_flushLock);
        m_flushCondition.wait(m_flushLock);
    }

    m_postTaskCallback(WTFMove(callback));
}

void GStreamerVideoDecoder::reset()
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Reseting");
}

void GStreamerVideoDecoder::close()
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Closing");
    // FIXME: send EOS?
    gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    m_isClosed = true;
}

bool GStreamerVideoDecoder::handleMessage(GstMessage* message)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Handling message %" GST_PTR_FORMAT, message);
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN_CAST(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "eos");
        break;
    default:
        break;
    }
    return true;
}

void GStreamerVideoDecoder::connectPad(GstPad* pad)
{
    auto padCaps = adoptGRef(gst_pad_query_caps(pad, nullptr));
    GST_DEBUG_OBJECT(m_pipeline.get(), "New decodebin pad %" GST_PTR_FORMAT " caps: %" GST_PTR_FORMAT, pad, padCaps.get());

    bool isVideo = doCapsHaveType(padCaps.get(), "video");
    RELEASE_ASSERT(isVideo);

    auto sinkPad = adoptGRef(gst_element_get_static_pad(m_videoconvert.get(), "sink"));
    gst_pad_link(pad, sinkPad.get());
    gst_element_sync_state_with_parent(m_videoconvert.get());
    gst_element_sync_state_with_parent(m_sink.get());
}

void GStreamerVideoDecoder::processSample(GRefPtr<GstSample>&& sample)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Got sample with caps %" GST_PTR_FORMAT, gst_sample_get_caps(sample.get()));
    m_postTaskCallback([protectedThis = Ref { *this }, this, sample = WTFMove(sample), timestamp = m_timestamp, duration = m_duration]() mutable {
        if (protectedThis->m_isClosed)
            return;

        auto* buffer = gst_sample_get_buffer(sample.get());
        auto videoFrame = VideoFrameGStreamer::createWrappedSample(sample, fromGstClockTime(GST_BUFFER_PTS(buffer)));
        m_outputCallback({ WTFMove(videoFrame), timestamp, duration });

        {
            LockHolder lock(m_sampleLock);
            m_sampleCondition.notifyOne();
        }
    });
}

gboolean GStreamerVideoDecoder::processEvent(GRefPtr<GstMiniObject>&& object)
{
    GST_DEBUG_OBJECT(m_pipeline.get(), "Processing %" GST_PTR_FORMAT, object.get());
    if (GST_EVENT_TYPE(GST_EVENT_CAST(object.get())) == GST_EVENT_FLUSH_STOP) {
        LockHolder lock(m_flushLock);
        m_flushCondition.notifyOne();
        // return TRUE;
    }

    return FALSE;
}

#endif // ENABLE(WEB_CODECS) && USE(GSTREAMER)
