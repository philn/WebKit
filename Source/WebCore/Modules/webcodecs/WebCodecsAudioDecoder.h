/*
 * Copyright (C) 2023 Igalia S.L
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

#if ENABLE(WEB_CODECS)

#include "ActiveDOMObject.h"
#include "AudioDecoder.h"
#include "EventTarget.h"
#include "JSDOMPromiseDeferredForward.h"
#include "WebCodecsAudioDecoderConfig.h"
#include "WebCodecsAudioDecoderSupport.h"
#include "WebCodecsCodecState.h"
#include "WebCodecsEncodedAudioChunkType.h"
#include <wtf/Deque.h>
#include <wtf/Vector.h>

namespace WebCore {

class WebCodecsEncodedAudioChunk;
class WebCodecsErrorCallback;
class WebCodecsAudioDataOutputCallback;

class WebCodecsAudioDecoder
    : public RefCounted<WebCodecsAudioDecoder>
    , public ActiveDOMObject
    , public EventTarget {
    WTF_MAKE_ISO_ALLOCATED(WebCodecsAudioDecoder);
public:
    ~WebCodecsAudioDecoder();

    struct Init {
        RefPtr<WebCodecsAudioDataOutputCallback> output;
        RefPtr<WebCodecsErrorCallback> error;
    };

    static Ref<WebCodecsAudioDecoder> create(ScriptExecutionContext&, Init&&);

    WebCodecsCodecState state() const { return m_state; }
    size_t decodeQueueSize() const { return m_decodeQueueSize; }

    ExceptionOr<void> configure(ScriptExecutionContext&, WebCodecsAudioDecoderConfig&&);
    ExceptionOr<void> decode(Ref<WebCodecsEncodedAudioChunk>&&);
    ExceptionOr<void> flush(Ref<DeferredPromise>&&);
    ExceptionOr<void> reset();
    ExceptionOr<void> close();

    static void isConfigSupported(ScriptExecutionContext&, WebCodecsAudioDecoderConfig&&, Ref<DeferredPromise>&&);

    using RefCounted::ref;
    using RefCounted::deref;

private:
    WebCodecsAudioDecoder(ScriptExecutionContext&, Init&&);

    // ActiveDOMObject API.
    void stop() final;
    const char* activeDOMObjectName() const final;
    void suspend(ReasonForSuspension) final;
    bool virtualHasPendingActivity() const final;

    // EventTarget
    void refEventTarget() final { ref(); }
    void derefEventTarget() final { deref(); }
    EventTargetInterface eventTargetInterface() const final { return WebCodecsAudioDecoderEventTargetInterfaceType; }
    ScriptExecutionContext* scriptExecutionContext() const final { return ActiveDOMObject::scriptExecutionContext(); }

    ExceptionOr<void> closeDecoder(Exception&&);
    ExceptionOr<void> resetDecoder(const Exception&);
    void setInternalDecoder(UniqueRef<AudioDecoder>&&);
    void scheduleDequeueEvent();

    void queueControlMessageAndProcess(Function<void()>&&);
    void processControlMessageQueue();

    WebCodecsCodecState m_state { WebCodecsCodecState::Unconfigured };
    size_t m_decodeQueueSize { 0 };
    size_t m_beingDecodedQueueSize { 0 };
    Ref<WebCodecsAudioDataOutputCallback> m_output;
    Ref<WebCodecsErrorCallback> m_error;
    std::unique_ptr<AudioDecoder> m_internalDecoder;
    bool m_dequeueEventScheduled { false };
    Deque<Ref<DeferredPromise>> m_pendingFlushPromises;
    size_t m_clearFlushPromiseCount { 0 };
    bool m_isKeyChunkRequired { false };
    Deque<Function<void()>> m_controlMessageQueue;
    bool m_isMessageQueueBlocked { false };
    bool m_isFlushing { false };
};

}

#endif
