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

#include "config.h"

#if USE(GSTREAMER) && ENABLE(SPEECH_SYNTHESIS)

#include "GStreamerSpeechRecognizerTask.h"

#if USE(GLIB_EVENT_LOOP)
#include <wtf/glib/RunLoopSourcePriority.h>
#endif

namespace WebCore {

Ref<GStreamerSpeechRecognizerTask> GStreamerSpeechRecognizerTask::create(SpeechRecognitionConnectionClientIdentifier identifier, const String& localeIdentifier, uint64_t alternatives, DelegateCallback&& delegateCallback)
{
    return adoptRef(*new GStreamerSpeechRecognizerTask(identifier, localeIdentifier, alternatives, WTFMove(delegateCallback)));
}

GStreamerSpeechRecognizerTask::GStreamerSpeechRecognizerTask(SpeechRecognitionConnectionClientIdentifier identifier, const String& localeIdentifier, uint64_t alternatives, DelegateCallback&& delegateCallback)
    : m_runLoop(RunLoop::create("org.webkit.GStreamerSpeechRecognizerTask"_s, ThreadType::Audio))
    // FIXME: m_runLoop or RunLoop::current()?
    , m_audioSampleProcessingTimer(RunLoop::current(), this, &GStreamerSpeechRecognizerTask::audioSampleProcessingTimerFired)
    , m_identifier(identifier)
    , m_maxAlternatives(alternatives ? alternatives : 1)
    , m_delegateCallback(WTFMove(delegateCallback))
{
#if USE(GLIB_EVENT_LOOP)
    m_audioSampleProcessingTimer.setPriority(RunLoopSourcePriority::RunLoopDispatcher);
    m_audioSampleProcessingTimer.setName("[WebKit] GStreamerSpeechRecognizerTask");
#endif

    initializeWhisper(localeIdentifier);
}

GStreamerSpeechRecognizerTask::~GStreamerSpeechRecognizerTask()
{
    m_audioSampleProcessingTimer.stop();
    m_audioSampleBuffer.clear();
}

void GStreamerSpeechRecognizerTask::audioSamplesAvailable(Vector<float>&& audioSamples)
{
//    ASSERT(isMainThread());
    {
        Locker locker { m_lock };
        m_audioSampleBuffer.appendVector(WTFMove(audioSamples));
    }
    m_audioSampleProcessingTimer.startOneShot(0_s);
}

void GStreamerSpeechRecognizerTask::abort()
{
//    ASSERT(isMainThread());

    // FIXME: Process the remaining samples in the buffer before stop.
    m_audioSampleProcessingTimer.stop();
    sendSpeechEndIfNeeded();
    sendEndIfNeeded();
}

void GStreamerSpeechRecognizerTask::stop()
{
//    ASSERT(isMainThread());

    // FIXME: Process the remaining samples in the buffer before stop.
    m_audioSampleProcessingTimer.stop();
    sendSpeechEndIfNeeded();
    sendEndIfNeeded();
}

void GStreamerSpeechRecognizerTask::sendSpeechStartIfNeeded()
{
    if (m_hasSentSpeechStart)
        return;

    m_hasSentSpeechStart = true;

    if (isMainThread()) {
        m_delegateCallback(WebCore::SpeechRecognitionUpdate::create(m_identifier, WebCore::SpeechRecognitionUpdateType::SpeechStart));
        return;
    }
    callOnMainThread([this, weakThis = WeakPtr { *this }] {
        if (!weakThis)
            return;
        m_delegateCallback(WebCore::SpeechRecognitionUpdate::create(m_identifier, WebCore::SpeechRecognitionUpdateType::SpeechStart));
    });
}

void GStreamerSpeechRecognizerTask::sendSpeechEndIfNeeded()
{
    if (!m_hasSentSpeechStart || m_hasSentSpeechEnd)
        return;

    m_hasSentSpeechEnd = true;

    if (isMainThread()) {
        m_delegateCallback(WebCore::SpeechRecognitionUpdate::create(m_identifier, WebCore::SpeechRecognitionUpdateType::SpeechEnd));
        return;
    }
    callOnMainThread([this, weakThis = WeakPtr { *this }] {
        if (!weakThis)
            return;
        m_delegateCallback(WebCore::SpeechRecognitionUpdate::create(m_identifier, WebCore::SpeechRecognitionUpdateType::SpeechEnd));
    });
}

void GStreamerSpeechRecognizerTask::sendEndIfNeeded()
{
    if (m_hasSentEnd)
        return;

    m_hasSentEnd = true;

    if (isMainThread()) {
        m_delegateCallback(WebCore::SpeechRecognitionUpdate::create(m_identifier, WebCore::SpeechRecognitionUpdateType::End));
        return;
    }
    callOnMainThread([this, weakThis = WeakPtr { *this }] {
        if (!weakThis)
            return;
        m_delegateCallback(WebCore::SpeechRecognitionUpdate::create(m_identifier, WebCore::SpeechRecognitionUpdateType::End));
    });
}

void GStreamerSpeechRecognizerTask::audioSampleProcessingTimerFired()
{
    ASSERT(!isMainThread());

    Vector<WebCore::SpeechRecognitionAlternativeData> alternatives;

    static constexpr size_t retainedSampleCount = WHISPER_SAMPLE_RATE * 0.2; // 0.2s
    static constexpr size_t minSampleCount = WHISPER_SAMPLE_RATE * 3 + retainedSampleCount;

    Vector<float> audioSamples;
    {
        Locker locker { m_lock };
        if (m_audioSampleBuffer.size() < minSampleCount)
            return;
        audioSamples.swap(m_audioSampleBuffer);
        // Keep the last part of the audio samples for next iteration to mitigate word boundary issues.
        m_audioSampleBuffer = Vector<float>(audioSamples.end() - retainedSampleCount, retainedSampleCount);
    }

    if (whisper_full(m_whisperContext.get(), m_whisperParams, audioSamples.data(), audioSamples.size())) {
        WTFLogAlways("Failed to process audio");
        return;
    }

    const int segmentCount = whisper_full_n_segments(m_whisperContext.get());
    if (segmentCount < 1) {
        WTFLogAlways("No segments to process: %lu.", audioSamples.size());
        return;
    }

    alternatives.reserveInitialCapacity(m_maxAlternatives);
    for (int i = 0; i < segmentCount; ++i) {
        float maxConfidence = 0.0;
        const int tokenCount = whisper_full_n_tokens(m_whisperContext.get(), i);
        for (int j = 0; j < tokenCount; ++j) {
            // FIXME: Is concatinating tokens better than using whisper_full_get_segment_text?
            // const char* token = whisper_full_get_token_text(m_whisperContext.get(), i, j);
            const float confidence = whisper_full_get_token_p(m_whisperContext.get(), i, j);
            maxConfidence = std::max(maxConfidence, confidence);
        }

        const char* text = whisper_full_get_segment_text(m_whisperContext.get(), i);
        WTFLogAlways(">>> %s, confidence: %f\n", text, maxConfidence);

        alternatives.uncheckedAppend(WebCore::SpeechRecognitionAlternativeData { String::fromUTF8(text), maxConfidence });
        if (alternatives.size() >= m_maxAlternatives)
            break;
    }

    callOnMainThread([this, weakThis = WeakPtr { *this }, alternatives = WTFMove(alternatives)] {
        if (!weakThis)
            return;
        m_delegateCallback(WebCore::SpeechRecognitionUpdate::createResult(m_identifier, { WebCore::SpeechRecognitionResultData { alternatives, true } }));
    });
}

static CString whisperModelPath()
{
    if (const char* path = g_getenv("WEBKIT_WHISPER_MODEL_PATH"))
        return path;

    return "/usr/share/whisper";
}

void GStreamerSpeechRecognizerTask::initializeWhisper(const String& localeIdentifier)
{
    // Suppress logs of Whisper.
    // whisper_set_log_callback(nullptr);

    GUniquePtr<char> whisperModelFilename(g_build_filename(whisperModelPath().data(), "ggml-base.en.bin", nullptr));
    m_whisperContext.reset(whisper_init_from_file(whisperModelFilename.get()));

    m_whisperParams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    m_whisperParams.print_progress = false;
    m_whisperParams.print_special = false;
    m_whisperParams.print_realtime = false;
    m_whisperParams.print_timestamps = false;

//    m_whisperParams.suppress_blank = true;
//    m_whisperParams.suppress_non_speech_tokens = true;

    m_whisperParams.translate = false;
    m_whisperParams.single_segment = true;
    m_whisperParams.max_tokens = 0;
    m_whisperParams.language = localeIdentifier.left(localeIdentifier.find('-')).utf8().data();

    m_whisperParams.n_threads = 4;
    m_whisperParams.audio_ctx = 0;
    m_whisperParams.speed_up = false;

    m_whisperParams.temperature_inc = 0.4;
    m_whisperParams.prompt_tokens = nullptr;
    m_whisperParams.prompt_n_tokens = 0;

    m_whisperParams.progress_callback_user_data = this;
    m_whisperParams.progress_callback = [](struct whisper_context*, struct whisper_state*, int progress, void* userData) {
        if (!userData)
            return;

        auto* task = static_cast<GStreamerSpeechRecognizerTask*>(userData);
        if (!progress) {
            task->sendSpeechStartIfNeeded();
            return;
        }
        task->sendSpeechEndIfNeeded();
    };
}

} // namespace WebCore

#endif // USE(GSTREAMER) && ENABLE(SPEECH_SYNTHESIS)
