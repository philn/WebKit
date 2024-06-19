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

#include "config.h"
#include "MediaSourceTrackGStreamer.h"

#if ENABLE(MEDIA_SOURCE) && USE(GSTREAMER)

#include "AudioTrackPrivateGStreamer.h"
#include "InbandTextTrackPrivateGStreamer.h"
#include "TrackPrivateBase.h"
#include "TrackPrivateBaseGStreamer.h"
#include "VideoTrackPrivateGStreamer.h"

GST_DEBUG_CATEGORY_EXTERN(webkit_mse_debug);
#define GST_CAT_DEFAULT webkit_mse_debug

namespace WebCore {

Ref<MediaSourceTrackGStreamer> MediaSourceTrackGStreamer::create(RefPtr<TrackPrivateBase>&& track, GRefPtr<GstCaps>&& initialCaps)
{
    return adoptRef(*new MediaSourceTrackGStreamer(WTFMove(track), WTFMove(initialCaps)));
}

MediaSourceTrackGStreamer::MediaSourceTrackGStreamer(RefPtr<TrackPrivateBase>&& track, GRefPtr<GstCaps>&& initialCaps)
    : m_track(WTFMove(track))
    , m_initialCaps(WTFMove(initialCaps))
    , m_queueDataMutex(stringId())
{ }

MediaSourceTrackGStreamer::~MediaSourceTrackGStreamer()
{
    ASSERT(m_isRemoved);
}

TrackPrivateBaseGStreamer::TrackType MediaSourceTrackGStreamer::type() const
{
    switch (m_track->type()) {
    case WebCore::TrackPrivateBase::Type::Audio:
        return TrackPrivateBaseGStreamer::TrackType::Audio;
    case WebCore::TrackPrivateBase::Type::Video:
        return TrackPrivateBaseGStreamer::TrackType::Video;
    case WebCore::TrackPrivateBase::Type::Text:
        return TrackPrivateBaseGStreamer::TrackType::Text;
    }
    ASSERT_NOT_REACHED();
    return TrackPrivateBaseGStreamer::TrackType::Unknown;
}

unsigned MediaSourceTrackGStreamer::index() const
{
    switch (m_track->type()) {
    case WebCore::TrackPrivateBase::Type::Audio:
        return static_cast<AudioTrackPrivateGStreamer*>(m_track.get())->index();
    case WebCore::TrackPrivateBase::Type::Video:
        return static_cast<VideoTrackPrivateGStreamer*>(m_track.get())->index();
    case WebCore::TrackPrivateBase::Type::Text:
        return static_cast<InbandTextTrackPrivateGStreamer*>(m_track.get())->index();
    }
    ASSERT_NOT_REACHED();
    return -1;
}

const AtomString& MediaSourceTrackGStreamer::stringId() const
{
    switch (m_track->type()) {
    case WebCore::TrackPrivateBase::Type::Audio:
        return static_cast<AudioTrackPrivateGStreamer*>(m_track.get())->stringId();
    case WebCore::TrackPrivateBase::Type::Video:
        return static_cast<VideoTrackPrivateGStreamer*>(m_track.get())->stringId();
    case WebCore::TrackPrivateBase::Type::Text:
        return static_cast<InbandTextTrackPrivateGStreamer*>(m_track.get())->stringId();
    }
    ASSERT_NOT_REACHED();
    return emptyAtom();
}

GRefPtr<GstStream> MediaSourceTrackGStreamer::stream() const
{
    switch (m_track->type()) {
    case WebCore::TrackPrivateBase::Type::Audio:
        return static_cast<AudioTrackPrivateGStreamer*>(m_track.get())->stream();
    case WebCore::TrackPrivateBase::Type::Video:
        return static_cast<VideoTrackPrivateGStreamer*>(m_track.get())->stream();
    case WebCore::TrackPrivateBase::Type::Text:
        return static_cast<InbandTextTrackPrivateGStreamer*>(m_track.get())->stream();
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

bool MediaSourceTrackGStreamer::isReadyForMoreSamples()
{
    ASSERT(isMainThread());
    DataMutexLocker queue { m_queueDataMutex };
    return !queue->isFull();
}

void MediaSourceTrackGStreamer::notifyWhenReadyForMoreSamples(TrackQueue::LowLevelHandler&& handler)
{
    ASSERT(isMainThread());
    DataMutexLocker queue { m_queueDataMutex };
    queue->notifyWhenLowLevel(WTFMove(handler));
}

void MediaSourceTrackGStreamer::enqueueObject(GRefPtr<GstMiniObject>&& object)
{
    ASSERT(isMainThread());
    DataMutexLocker queue { m_queueDataMutex };
    queue->enqueueObject(WTFMove(object));
}

void MediaSourceTrackGStreamer::clearQueue()
{
    ASSERT(isMainThread());
    DataMutexLocker queue { m_queueDataMutex };
    queue->clear();
}

void MediaSourceTrackGStreamer::remove()
{
    ASSERT(isMainThread());
    m_isRemoved = true;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(MEDIA_SOURCE) && USE(GSTREAMER)
