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

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "NotImplemented.h"
#include <wtf/MainThread.h>

namespace WebCore {

GStreamerDTMFSenderBackend::GStreamerDTMFSenderBackend(const GRefPtr<GstElement>& senderBin)
{
    m_element = senderBin;
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
    static HashMap<String, int> tones = {
        { "0"_s, 0 },
        { "1"_s, 1 },
        { "2"_s, 2 },
        { "3"_s, 3 },
        { "4"_s, 4 },
        { "5"_s, 5 },
        { "6"_s, 6 },
        { "7"_s, 7 },
        { "8"_s, 8 },
        { "9"_s, 9 },
        { "S"_s, 10 },
        { "P"_s, 11 },
        { "A"_s, 12 },
        { "B"_s, 13 },
        { "C"_s, 14 },
        { "D"_s, 15 }
    };
    // FIXME: comma event : 2 seconds silence

    auto toneNumber = tones.get(makeString(tone));
    gst_element_send_event(m_element.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new("dtmf-event", "type", G_TYPE_INT, 1, "number", G_TYPE_INT, toneNumber, "volume", G_TYPE_INT, 25, "start", G_TYPE_BOOLEAN, TRUE, nullptr)));
    sleep(Seconds::fromMilliseconds(duration));
    gst_element_send_event(m_element.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new("dtmf-event", "type", G_TYPE_INT, 1, "start", G_TYPE_BOOLEAN, FALSE, nullptr)));
    m_onTonePlayed();
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

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
