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
#include "GStreamerDTMFSenderBackend.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerCommon.h"
#include "NotImplemented.h"
#include <wtf/HashFunctions.h>
#include <wtf/HashTraits.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(GStreamerDTMFSenderBackend);

GST_DEBUG_CATEGORY(webkit_webrtc_dtmf_sender_debug);
#define GST_CAT_DEFAULT webkit_webrtc_dtmf_sender_debug

GStreamerDTMFSenderBackend::GStreamerDTMFSenderBackend(ThreadSafeWeakPtr<RealtimeOutgoingAudioSourceGStreamer>&& source)
    : m_source(WTFMove(source))
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_dtmf_sender_debug, "webkitwebrtcdtmfsender", 0, "WebKit WebRTC DTMF Sender");
    });

    RefPtr strongSource = m_source.get();
    if (!strongSource) {
        m_canInsertDTMF = false;
        return;
    }

    m_dtmfSrc = makeGStreamerElement("dtmfsrc"_s);
    if (!m_dtmfSrc) {
        m_canInsertDTMF = false;
        return;
    }

    m_canInsertDTMF = true;

    static uint32_t nPipeline = 0;
    auto pipelineName = makeString("webkit-dtmf-playout-pipeline-"_s, nPipeline);
    m_pipeline = gst_pipeline_new(pipelineName.ascii().data());
    registerActivePipeline(m_pipeline);
    connectSimpleBusMessageCallback(m_pipeline.get());
    GST_DEBUG_OBJECT(m_pipeline.get(), "DTMF sender backend created");

    auto audioconvert = makeGStreamerElement("audioconvert"_s);
    auto audioresample = makeGStreamerElement("audioresample"_s);
    auto queue = gst_element_factory_make("queue", nullptr);
    auto sink = createPlatformAudioSink("dtmf"_s);
    gst_bin_add_many(GST_BIN_CAST(m_pipeline.get()), m_dtmfSrc.get(), audioconvert, audioresample, queue, sink, nullptr);
    gst_element_link_many(m_dtmfSrc.get(), audioconvert, audioresample, queue, sink, nullptr);
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
}

GStreamerDTMFSenderBackend::~GStreamerDTMFSenderBackend()
{
    if (!m_pipeline)
        return;

    unregisterPipeline(m_pipeline);
    disconnectSimpleBusMessageCallback(m_pipeline.get());
    gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
}

bool GStreamerDTMFSenderBackend::canInsertDTMF()
{
    RefPtr source = m_source.get();
    if (!source)
        return false;
    return m_canInsertDTMF;
}

void GStreamerDTMFSenderBackend::playTone(const char tone, size_t duration, size_t interToneGap)
{
    static HashMap<char, int, WTF::IntHash<char>, WTF::UnsignedWithZeroKeyHashTraits<char>> tones = {
        { '0', 0 },
        { '1', 1 },
        { '2', 2 },
        { '3', 3 },
        { '4', 4 },
        { '5', 5 },
        { '6', 6 },
        { '7', 7 },
        { '8', 8 },
        { '9', 9 },
        { 'S', 10 }, // Star
        { 'P', 11 }, // Pound
        { 'A', 12 },
        { 'B', 13 },
        { 'C', 14 },
        { 'D', 15 }
    };

    RefPtr source = m_source.get();
    if (!source)
        return;

    auto scopeExit = makeScopeExit([&] {
        m_tones.append(tone);
        sleep(Seconds::fromMilliseconds(interToneGap));
    });

    auto element = adoptGRef(gst_bin_get_by_name(GST_BIN_CAST(source->bin().get()), "dtmfSource"));
    auto pad = adoptGRef(gst_element_get_static_pad(element.get(), "src"));

    if (tone == ',') {
        GST_DEBUG_OBJECT(element.get(), "Playing silence for 2 seconds");
        sleep(Seconds::fromMilliseconds(2000));
        m_onTonePlayed();
        GST_DEBUG_OBJECT(element.get(), "Playing tone %c DONE", tone);
        return;
    }

    auto toneNumber = tones.get(tone);
    GST_DEBUG_OBJECT(element.get(), "Playing tone %c for %zu milliseconds", tone, duration);
    sendEvent(pad, toneNumber, 25, true);
    sleep(Seconds::fromMilliseconds(duration));
    sendEvent(pad, toneNumber, 0, false);
    m_onTonePlayed();
    GST_DEBUG_OBJECT(element.get(), "Playing tone %c DONE", tone);
}

void GStreamerDTMFSenderBackend::sendEvent(const GRefPtr<GstPad>& pad, int number, int volume, bool start)
{
    auto event = adoptGRef(gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new("dtmf-event", "type", G_TYPE_INT, 1, "number", G_TYPE_INT, number, "volume", G_TYPE_INT, volume, "start", G_TYPE_BOOLEAN, start, nullptr)));
    gst_pad_send_event(pad.get(), gst_event_ref(event.get()));
    gst_element_send_event(m_dtmfSrc.get(), event.leakRef());
}

String GStreamerDTMFSenderBackend::tones() const
{
    return m_tones.toStringPreserveCapacity();
}

size_t GStreamerDTMFSenderBackend::duration() const
{
    notImplemented();
    return 0;
}

size_t GStreamerDTMFSenderBackend::interToneGap() const
{
    notImplemented();
    return 0;
}

void GStreamerDTMFSenderBackend::onTonePlayed(Function<void()>&& onTonePlayed)
{
    m_onTonePlayed = WTFMove(onTonePlayed);
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
