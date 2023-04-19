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

#include "NotImplemented.h"
#include <wtf/MainThread.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_webrtc_dtmf_sender_debug);
#define GST_CAT_DEFAULT webkit_webrtc_dtmf_sender_debug

GStreamerDTMFSenderBackend::GStreamerDTMFSenderBackend(const GRefPtr<GstElement>& senderBin)
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_dtmf_sender_debug, "webkitwebrtcdtmfsender", 0, "WebKit WebRTC DTMF Sender");
    });
    m_element = senderBin;
    GST_DEBUG_OBJECT(m_element.get(), "DTMF sender backend created");
}

GStreamerDTMFSenderBackend::~GStreamerDTMFSenderBackend()
{
    notImplemented();
}

bool GStreamerDTMFSenderBackend::canInsertDTMF()
{
    return true;
}

void GStreamerDTMFSenderBackend::playTone(const char tone, size_t duration, size_t interToneGap)
{
    static HashMap<char, int> tones = {
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
        { 'S', 10 },
        { 'P', 11 },
        { 'A', 12 },
        { 'B', 13 },
        { 'C', 14 },
        { 'D', 15 }
    };
    // FIXME: comma event : 2 seconds silence

    auto toneNumber = tones.get(tone);
    GST_DEBUG_OBJECT(m_element.get(), "Playing tone %c for %zu milliseconds", tone, duration);
    gst_element_send_event(m_element.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new("dtmf-event", "type", G_TYPE_INT, 1, "number", G_TYPE_INT, toneNumber, "volume", G_TYPE_INT, 25, "start", G_TYPE_BOOLEAN, TRUE, nullptr)));
    sleep(Seconds::fromMilliseconds(duration));
    gst_element_send_event(m_element.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new("dtmf-event", "type", G_TYPE_INT, 1, "start", G_TYPE_BOOLEAN, FALSE, nullptr)));
    m_onTonePlayed();
    GST_DEBUG_OBJECT(m_element.get(), "Playing tone %c DONE", tone);
}

String GStreamerDTMFSenderBackend::tones() const
{
    notImplemented();
    return { };
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
