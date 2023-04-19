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

#pragma once

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GRefPtrGStreamer.h"
#include "RTCDTMFSenderBackend.h"
#include "RealtimeOutgoingAudioSourceGStreamer.h"
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

// Use eager initialization for the WeakPtrFactory since we call makeWeakPtr() from another thread.
class GStreamerDTMFSenderBackend final : public RTCDTMFSenderBackend, public CanMakeWeakPtr<GStreamerDTMFSenderBackend, WeakPtrFactoryInitialization::Eager> {
    WTF_MAKE_TZONE_ALLOCATED(GStreamerDTMFSenderBackend);
public:
    explicit GStreamerDTMFSenderBackend(ThreadSafeWeakPtr<RealtimeOutgoingAudioSourceGStreamer>&&);
    ~GStreamerDTMFSenderBackend();

private:
    // RTCDTMFSenderBackend
    bool canInsertDTMF() final;
    void playTone(const char tone, size_t duration, size_t interToneGap) final;
    void onTonePlayed(Function<void()>&&) final;
    String tones() const final;
    size_t duration() const final;
    size_t interToneGap() const final;

    void sendEvent(const GRefPtr<GstPad>&, int number, int volume, bool start);

    ThreadSafeWeakPtr<RealtimeOutgoingAudioSourceGStreamer> m_source;
    Function<void()> m_onTonePlayed;
    bool m_canInsertDTMF { false };
    GRefPtr<GstElement> m_pipeline;
    GRefPtr<GstElement> m_dtmfSrc;
    StringBuilder m_tones;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
