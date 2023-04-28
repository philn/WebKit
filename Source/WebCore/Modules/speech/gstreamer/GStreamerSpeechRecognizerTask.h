/*
 * Copyright (C) 2023 ChangSeok Oh <changseok@webkit.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "GUniquePtrGStreamer.h"
#include "SpeechRecognitionConnectionClientIdentifier.h"
#include "SpeechRecognitionUpdate.h"
#include <wtf/Lock.h>
#include <wtf/RunLoop.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/WeakPtr.h>

#if USE(WHISPER)
#include <whisper.h>
namespace WTF {
WTF_DEFINE_GPTR_DELETER(struct whisper_context, whisper_free)
}
#endif

namespace WebCore {

class GStreamerSpeechRecognizerTask: public ThreadSafeRefCounted<GStreamerSpeechRecognizerTask>, public CanMakeWeakPtr<GStreamerSpeechRecognizerTask> {
    WTF_MAKE_NONCOPYABLE(GStreamerSpeechRecognizerTask);
    WTF_MAKE_FAST_ALLOCATED;
public:
    using DelegateCallback = Function<void(const SpeechRecognitionUpdate&)>;
    static Ref<GStreamerSpeechRecognizerTask> create(SpeechRecognitionConnectionClientIdentifier, const String& localeIdentifier, uint64_t alternatives, DelegateCallback&&);
    virtual ~GStreamerSpeechRecognizerTask();

    void abort();
    void stop();

    void audioSamplesAvailable(Vector<float>&& audioSamples);
    void sendSpeechStartIfNeeded();
    void sendSpeechEndIfNeeded();
    void sendEndIfNeeded();

private:
    GStreamerSpeechRecognizerTask(SpeechRecognitionConnectionClientIdentifier, const String& localeIdentifier, uint64_t alternatives, DelegateCallback&&);
    void audioSampleProcessingTimerFired();

#if USE(WHISPER)
    void initializeWhisper(const String& localeIdentifier);
#endif

    Lock m_lock;
    Ref<RunLoop> m_runLoop;
    RunLoop::Timer m_audioSampleProcessingTimer WTF_GUARDED_BY_LOCK(m_lock);

    SpeechRecognitionConnectionClientIdentifier m_identifier;
    uint64_t m_maxAlternatives;
    Vector<float> m_audioSampleBuffer WTF_GUARDED_BY_LOCK(m_lock);
    DelegateCallback m_delegateCallback;

    bool m_hasSentSpeechStart { false };
    bool m_hasSentSpeechEnd { false };
    bool m_hasSentEnd { false };
#if USE(WHISPER)
    GUniquePtr<struct whisper_context> m_whisperContext;
    struct whisper_full_params m_whisperParams;
#endif

};

} // namespace WebCore
