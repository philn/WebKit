/*
 * Copyright (C) 2026 Igalia S.L
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

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "GStreamerElementHarness.h"
#include "MediaPlayerEnums.h"
#include "MediaSampleGStreamer.h"
#include "PlatformMediaError.h"
#include "SourceBufferPrivateClient.h"
#include <expected>
#include <wtf/RefCounted.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/ThreadSafeWeakPtr.h>

namespace WebCore {
class ContentType;
class SharedBuffer;

class GStreamerBufferParser : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<GStreamerBufferParser, WTF::DestructionThread::Main> {
public:
    static MediaPlayerEnums::SupportsType isContentTypeSupported(const ContentType&);

    static RefPtr<GStreamerBufferParser> create();
    virtual ~GStreamerBufferParser() = default;

    /* enum class Type : uint8_t { */
    /*     AVFObjC, */
    /*     WebM, */
    /* }; */
    /* virtual Type type() const = 0; */
    /* enum class AppendFlags : uint8_t { */
    /*     None, */
    /*     Discontinuity, */
    /* }; */

    // All callbacks will be called via this function if set.
    using CallOnClientThreadCallback = Function<void(Function<void()>&&)>;
    void setCallOnClientThreadCallback(CallOnClientThreadCallback&&);

    // appendData will be called on the SourceBufferPrivateAVFObjC data parser queue.
    // Other methods will be called on the main thread, but only once appendData has returned.
    std::expected<void, PlatformMediaError> appendData(Ref<const SharedBuffer>&&);
    void flushPendingMediaData();
    void resetParserState();
    void invalidate();
    void setMinimumAudioSampleDuration(float);
#if !RELEASE_LOG_DISABLED
    void setLogger(const Logger&, uint64_t logIdentifier);
#endif

    using InitializationSegment = SourceBufferPrivateClient::InitializationSegment;
    using DidParseInitializationDataCallback = Function<void(const InitializationSegment&)>;
    void setDidParseInitializationDataCallback(DidParseInitializationDataCallback&& callback)
    {
        m_didParseInitializationDataCallback = WTF::move(callback);
    }

    using DidProvideMediaDataCallback = Function<void(Ref<MediaSampleGStreamer>&&, uint64_t trackID, const String& mediaType)>;
    void setDidProvideMediaDataCallback(DidProvideMediaDataCallback&& callback)
    {
        m_didProvideMediaDataCallback = WTF::move(callback);
    }

    /* using WillProvideContentKeyRequestInitializationDataForTrackIDCallback = Function<void(uint64_t trackID)>; */
    /* void setWillProvideContentKeyRequestInitializationDataForTrackIDCallback(WillProvideContentKeyRequestInitializationDataForTrackIDCallback&& callback) */
    /* { */
    /*     m_willProvideContentKeyRequestInitializationDataForTrackIDCallback = WTF::move(callback); */
    /* } */

    /* using DidProvideContentKeyRequestInitializationDataForTrackIDCallback = Function<void(Ref<SharedBuffer>&&, uint64_t trackID)>; */
    /* void setDidProvideContentKeyRequestInitializationDataForTrackIDCallback(DidProvideContentKeyRequestInitializationDataForTrackIDCallback&& callback) */
    /* { */
    /*     m_didProvideContentKeyRequestInitializationDataForTrackIDCallback = WTF::move(callback); */
    /* } */

    /* using DidProvideContentKeyRequestIdentifierForTrackIDCallback = Function<void(Ref<SharedBuffer>&&, uint64_t trackID)>; */
    /* void setDidProvideContentKeyRequestIdentifierForTrackIDCallback(DidProvideContentKeyRequestIdentifierForTrackIDCallback&& callback) */
    /* { */
    /*     m_didProvideContentKeyRequestIdentifierForTrackIDCallback = WTF::move(callback); */
    /* } */

    /* using DidUpdateFormatDescriptionForTrackIDCallback = Function<void(Ref<TrackInfo>&&, uint64_t trackID)>; */
    /* void setDidUpdateFormatDescriptionForTrackIDCallback(DidUpdateFormatDescriptionForTrackIDCallback&& callback) */
    /* { */
    /*     m_didUpdateFormatDescriptionForTrackIDCallback = WTF::move(callback); */
    /* } */

protected:
    GStreamerBufferParser();

    CallOnClientThreadCallback m_callOnClientThreadCallback;
    DidParseInitializationDataCallback m_didParseInitializationDataCallback;
    DidProvideMediaDataCallback m_didProvideMediaDataCallback;
    // WillProvideContentKeyRequestInitializationDataForTrackIDCallback m_willProvideContentKeyRequestInitializationDataForTrackIDCallback;
    // DidProvideContentKeyRequestInitializationDataForTrackIDCallback m_didProvideContentKeyRequestInitializationDataForTrackIDCallback;
    // DidProvideContentKeyRequestIdentifierForTrackIDCallback m_didProvideContentKeyRequestIdentifierForTrackIDCallback;
    // DidUpdateFormatDescriptionForTrackIDCallback m_didUpdateFormatDescriptionForTrackIDCallback;

private:
    void initializeParserHarness();
    void pushNewBuffer(GRefPtr<GstBuffer>&&);
    bool processOutputEvents();
    void notifyInitializationSegment(GstStreamCollection&);
    void handleSample(GRefPtr<GstSample>&&);

    RefPtr<GStreamerElementHarness> m_harness;
    GRefPtr<GstBus> m_bus;
    // Ref<WorkQueue> m_workQueue;
    std::optional<SourceBufferPrivateClient::InitializationSegment> m_initializationSegment;
};

} // namespace WebCore

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
