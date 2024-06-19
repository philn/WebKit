/*
 * Copyright (C) 2020, 2021 Metrological Group B.V.
 * Copyright (C) 2020, 2021 Igalia, S.L
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(MEDIA_SOURCE) && USE(GSTREAMER)

#include "TrackPrivateBase.h"
#include "TrackQueue.h"
#include <wtf/DataMutex.h>

namespace WebCore {

class MediaSourceTrackGStreamer final: public ThreadSafeRefCounted<MediaSourceTrackGStreamer> {
public:
    static Ref<MediaSourceTrackGStreamer> create(RefPtr<TrackPrivateBase>&&, GRefPtr<GstCaps>&& initialCaps);
    virtual ~MediaSourceTrackGStreamer();

    TrackPrivateBaseGStreamer::TrackType type() const;
    unsigned index() const;
    TrackID id() const { return m_track->id(); }
    const AtomString& stringId() const;
    GRefPtr<GstCaps>& initialCaps() { return m_initialCaps; }
    DataMutex<TrackQueue>& queueDataMutex() { return m_queueDataMutex; }

    GRefPtr<GstStream> stream() const;
    RefPtr<TrackPrivateBase> trackPrivate() const { return m_track; }

    bool isReadyForMoreSamples();

    void notifyWhenReadyForMoreSamples(TrackQueue::LowLevelHandler&&);

    void enqueueObject(GRefPtr<GstMiniObject>&&);

    // This method is provided to clear the TrackQueue in cases where the stream hasn't been started (e.g. because
    // another SourceBuffer hasn't received the necessary initalization segment for playback).
    // Otherwise, webKitMediaSrcFlush() should be used instead, which will also do a GStreamer pipeline flush where
    // necessary.
    void clearQueue();

    void remove();

private:
    explicit MediaSourceTrackGStreamer(RefPtr<TrackPrivateBase>&&, GRefPtr<GstCaps>&& initialCaps);

    RefPtr<TrackPrivateBase> m_track;
    GRefPtr<GstCaps> m_initialCaps;
    DataMutex<TrackQueue> m_queueDataMutex;

    bool m_isRemoved { false };
};

} // namespace WebCore

#endif // ENABLE(MEDIA_SOURCE) && USE(GSTREAMER)
