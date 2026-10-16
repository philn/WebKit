/*
 *  Copyright (C) 2026 Igalia S.L
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

#include "AudioBus.h"
#include "AudioDestination.h"
#include "GRefPtrGStreamer.h"
#include "GStreamerCommon.h"
#include "GStreamerElementHarness.h"
#include <wtf/Forward.h>
#include <wtf/ThreadSafeWeakPtr.h>
#include "Timer.h"

namespace WebCore {

class AudioDestinationHarnessGStreamer : public AudioDestination, public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<AudioDestinationHarnessGStreamer, WTF::DestructionThread::Main> {
public:
    AudioDestinationHarnessGStreamer(const CreationOptions&);
    virtual ~AudioDestinationHarnessGStreamer();

    void ref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::ref(); }
    void deref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::deref(); }

    using DispatchToRenderThreadCallback = Function<void(Function<void()>&&)>;
    WEBCORE_EXPORT void start(DispatchToRenderThreadCallback&&, CompletionHandler<void(bool)>&&) final;
    WEBCORE_EXPORT void stop(CompletionHandler<void(bool)>&&) final;

    bool isPlaying() override { return m_isPlaying; }
    unsigned framesPerBuffer() const final;

protected:
    virtual void startRendering(CompletionHandler<void(bool)>&&);
    virtual void stopRendering(CompletionHandler<void(bool)>&&);

private:
    void notifyIsPlaying(bool);
    void notifyStartupResult(bool);
    void notifyStopResult(bool);

    GRefPtr<GstBuffer> allocateBuffer();
    void renderAndPushFrames();
    void render();

    DispatchToRenderThreadCallback m_renderCallback;

    const RefPtr<AudioBus> m_renderBus;

    bool m_isPlaying { false };
    bool m_audioSinkAvailable { false };
    uint64_t m_numberOfSamples { 0 };

    RefPtr<GStreamerElementHarness> m_harness;

    GRefPtr<GstBufferPool> m_pool;

    GRefPtr<GstCaps> m_caps;
    GstAudioInfo m_info;

    CompletionHandler<void(bool)> m_startupCompletionHandler;
    CompletionHandler<void(bool)> m_stopCompletionHandler;

    void renderTimerFired();
    Timer m_renderTimer;
};

} // namespace WebCore
