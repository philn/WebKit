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

#pragma once

#if USE(GSTREAMER) && ENABLE(MEDIA_SOURCE)

#include "GStreamerElementHarness.h"
#include "MediaPlayerPrivateGStreamerMSE.h"
#include "SourceBufferPrivateGStreamer.h"
#include <wtf/ThreadSafeWeakPtr.h>
#include <wtf/WorkQueue.h>

namespace WebCore {

class GStreamerSourceBufferParser : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<GStreamerSourceBufferParser> {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static Ref<GStreamerSourceBufferParser> create(SourceBufferPrivateGStreamer& sourceBufferPrivate, const RefPtr<MediaPlayerPrivateGStreamerMSE>& mediaPlayerPrivate)
    {
        return adoptRef(*new GStreamerSourceBufferParser(sourceBufferPrivate, mediaPlayerPrivate));
    }
    virtual ~GStreamerSourceBufferParser() = default;

    void pushNewBuffer(GRefPtr<GstBuffer>&&);
    void resetParserState();
    void stopParser();

private:
    GStreamerSourceBufferParser(SourceBufferPrivateGStreamer&, const RefPtr<MediaPlayerPrivateGStreamerMSE>&);
    void initializeParserHarness();
    bool processOutputEvents();
    void notifyInitializationSegment(GstStreamCollection&);
    void handleSample(GRefPtr<GstSample>&&);

    SourceBufferPrivateGStreamer& m_sourceBufferPrivate;
    ThreadSafeWeakPtr<MediaPlayerPrivateGStreamerMSE> m_playerPrivate;
    RefPtr<GStreamerElementHarness> m_harness;
    GRefPtr<GstBus> m_bus;
    Ref<WorkQueue> m_workQueue;
    std::optional<SourceBufferPrivateClient::InitializationSegment> m_initializationSegment;
};

} // namespace WebCore

#endif // USE(GSTREAMER) && ENABLE(MEDIA_SOURCE)
