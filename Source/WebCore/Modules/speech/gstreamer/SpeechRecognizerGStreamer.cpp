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
#include "SpeechRecognizer.h"

#if USE(GSTREAMER) && ENABLE(MEDIA_STREAM)

#include "GStreamerAudioData.h"
#include "GStreamerAudioStreamDescription.h"
#include "GStreamerCommon.h"
#include "GStreamerSpeechRecognizerTask.h"
#include "SpeechRecognitionRequest.h"
#include "SpeechRecognitionUpdate.h"

GST_DEBUG_CATEGORY(webkit_speech_recognizer_debug);
#define GST_CAT_DEFAULT webkit_speech_recognizer_debug

#if GST_CHECK_VERSION(1, 22, 0)
#define SR_DEBUG(...) GST_DEBUG_ID(m_recognizerId.ascii().data(), __VA_ARGS__)
#define SR_TRACE(...) GST_TRACE_ID(m_recognizerId.ascii().data(), __VA_ARGS__)
#define SR_WARNING(...) GST_WARNING_ID(m_recognizerId.ascii().data(), __VA_ARGS__)
#define SR_MEMDUMP(...) GST_MEMDUMP_ID(m_recognizerId.ascii().data(), __VA_ARGS__)
#else
#define SR_DEBUG(...) GST_DEBUG(__VA_ARGS__)
#define SR_TRACE(...) GST_TRACE(__VA_ARGS__)
#define SR_WARNING(...) GST_WARNING(__VA_ARGS__)
#define SR_MEMDUMP(...) GST_MEMDUMP(__VA_ARGS__)
#endif

namespace WebCore {

static Vector<float> createAudioSampleBuffer(const PlatformAudioData& audioData, const AudioStreamDescription& description, GstAudioConverter* converter)
{
    const auto& data = static_cast<const GStreamerAudioData&>(audioData);
    GstMappedBuffer mappedBuffer(gst_sample_get_buffer(data.getSample().get()), GST_MAP_READ);

    gpointer convertedSamplesRawData;
    gsize convertedSamplesSize;
    if (!gst_audio_converter_convert(converter, GST_AUDIO_CONVERTER_FLAG_NONE, mappedBuffer.data(), mappedBuffer.size(), &convertedSamplesRawData, &convertedSamplesSize))
        return { };

    Vector<float> newSamples(static_cast<float*>(convertedSamplesRawData), convertedSamplesSize / description.sampleWordSize());
    g_free(convertedSamplesRawData);

    return newSamples;
}

void SpeechRecognizer::dataCaptured(const MediaTime& time, const PlatformAudioData& audioData, const AudioStreamDescription& description, size_t sampleCount)
{
    UNUSED_PARAM(time);
    UNUSED_PARAM(sampleCount);

    if (!m_converter) {
        auto inputAudioInfo = static_cast<const GStreamerAudioStreamDescription&>(description).getInfo();
        // We convert the sample rate and format.
        GstAudioInfo outputAudioInfo;
        gst_audio_info_set_format(&outputAudioInfo, GST_AUDIO_FORMAT_F32LE, WHISPER_SAMPLE_RATE, inputAudioInfo.channels, nullptr);

        m_converter.reset(gst_audio_converter_new(GST_AUDIO_CONVERTER_FLAG_IN_WRITABLE, &inputAudioInfo, &outputAudioInfo, nullptr));
    }

    Vector<float> newSamples(createAudioSampleBuffer(audioData, description, m_converter.get()));
    m_task->audioSamplesAvailable(WTFMove(newSamples));
}

bool SpeechRecognizer::startRecognition(bool mockSpeechRecognitionEnabled, SpeechRecognitionConnectionClientIdentifier identifier, const String& localeIdentifier, bool continuous, bool interimResults, uint64_t alternatives)
{
    UNUSED_PARAM(mockSpeechRecognitionEnabled);
    UNUSED_PARAM(continuous);
    UNUSED_PARAM(interimResults);

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_speech_recognizer_debug, "webkitspeechrecognizer", 0, "WebKit Speech Recognizer");
    });

    static Atomic<uint64_t> counter = 0;
    m_recognizerId = makeString("webkit-speech-recognizer-", counter.exchangeAdd(1));

    WTFLogAlways("Loading model for locale %s", localeIdentifier.ascii().data());

    m_task = GStreamerSpeechRecognizerTask::create(identifier, localeIdentifier, alternatives, [weakThis = WeakPtr { *this }](const SpeechRecognitionUpdate& update) {
        if (weakThis)
            weakThis->m_delegateCallback(update);
    });

    return !!m_task;
}

void SpeechRecognizer::abortRecognition()
{
    ASSERT(m_task);
    m_task->abort();
    m_converter = nullptr;
}

void SpeechRecognizer::stopRecognition()
{
    ASSERT(m_task);
    m_task->stop();
    m_converter = nullptr;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER) && ENABLE(MEDIA_STREAM)
