/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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

void SpeechRecognizer::dataCaptured(const MediaTime& time, const PlatformAudioData& audioData, const AudioStreamDescription& description, size_t sampleCount)
{
    const auto& data = static_cast<const GStreamerAudioData&>(audioData);
    GRefPtr<GstSample> sample = data.getSample();

    auto* buffer = gst_sample_get_buffer(sample.get());
    auto* audioLevelMeta = gst_buffer_get_audio_level_meta(buffer);
    // gst_printerrln("meta: %p voice: %s", audioLevelMeta, audioLevelMeta ? boolForPrinting(audioLevelMeta->voice_activity) : "no");
    bool vad = audioLevelMeta && audioLevelMeta->voice_activity;
    vad = true;
    if (vad) {
        GstMappedBuffer mappedBuffer2(buffer, GST_MAP_READ);
        if (!m_converter) {
            auto desc = static_cast<const GStreamerAudioStreamDescription&>(description);
            m_inputStreamDescription = desc.getInfo();
            gst_audio_info_set_format(&m_outputStreamDescription, GST_AUDIO_FORMAT_F32LE, WHISPER_SAMPLE_RATE, 1, nullptr);
            m_converter.reset(gst_audio_converter_new(GST_AUDIO_CONVERTER_FLAG_NONE, &m_inputStreamDescription,
                &m_outputStreamDescription, nullptr));
        }

        gpointer out;
        gsize outSize;
        if (!gst_audio_converter_convert(m_converter.get(), GST_AUDIO_CONVERTER_FLAG_NONE, mappedBuffer2.data(), mappedBuffer2.size(), &out, &outSize)) {
            return;
        }

        float* s = (float*) out;
        for (unsigned i = 0; i < outSize / GST_AUDIO_INFO_BPF(&m_outputStreamDescription); i++)
            m_buffer.append((float)s[i]);
        m_queuedSampleCount += sampleCount;
        g_free(out);
    }

    MediaTime queuedTime((m_queuedSampleCount * G_USEC_PER_SEC) / WHISPER_SAMPLE_RATE, G_USEC_PER_SEC);
    if (queuedTime.toFloat() < 10)
        return;

    // High pass filter
    // {
    //     float cutoff = 100.0;
    //     float sample_rate = 16000;
    //     const float rc = 1.0f / (2.0f * M_PI * cutoff);
    //     const float dt = 1.0f / sample_rate;
    //     const float alpha = dt / (rc + dt);

    //     float y = m_buffer[0];

    //     for (size_t i = 1; i < m_buffer.size(); i++) {
    //         y = alpha * (y + m_buffer[i] - m_buffer[i - 1]);
    //         m_buffer[i] = y;
    //     }
    // }

    if (whisper_full(m_whisperContext.get(), m_whisperParams, reinterpret_cast<const float*>(m_buffer.data()), m_buffer.size()) != 0) {
        WTFLogAlways("failed to process audio");
        return;
    }

    m_queuedSampleCount = 0;
    m_buffer.clear();
    const int n_segments = whisper_full_n_segments(m_whisperContext.get());
    gst_printerrln(">>>>> %d segments", n_segments);
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(m_whisperContext.get(), i);
        WTFLogAlways("%s ", text);
        // int n_tokens = whisper_full_n_tokens(m_whisperContext.get(), i);
        // for (int j = 0; j < n_tokens; j++) {
        //     WTFLogAlways("%s ", whisper_full_get_token_text(m_whisperContext.get(), i, j));
        // }
    }
}

bool SpeechRecognizer::startRecognition(bool mockSpeechRecognitionEnabled, SpeechRecognitionConnectionClientIdentifier identifier, const String& localeIdentifier, bool continuous, bool interimResults, uint64_t alternatives)
{
    UNUSED_PARAM(mockSpeechRecognitionEnabled);
    UNUSED_PARAM(identifier);
    UNUSED_PARAM(continuous);
    UNUSED_PARAM(interimResults);
    UNUSED_PARAM(alternatives);
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_speech_recognizer_debug, "webkitspeechrecognizer", 0, "WebKit Speech Recognizer");
    });

    static Atomic<uint64_t> counter = 0;
    m_recognizerId = makeString("webkit-speech-recognizer-", counter.exchangeAdd(1));

    WTFLogAlways("Loading model for locale %s", localeIdentifier.ascii().data());
    m_adapter = adoptGRef(gst_adapter_new()),

    m_whisperContext.reset(whisper_init_from_file("/home/phil/dev/whisper/subprojects/whisper.cpp/models/ggml-base.en.bin"));


    m_whisperParams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    m_whisperParams.n_threads = 8;
    m_whisperParams.speed_up = false;
    m_whisperParams.single_segment = false;
    m_whisperParams.print_progress = false;
    m_whisperParams.max_tokens = 32;
    m_whisperParams.language = "en";
    m_whisperParams.translate = false;
    m_whisperParams.temperature_inc = -1.0;
    m_whisperParams.audio_ctx = 0;

    return true;
}

void SpeechRecognizer::abortRecognition()
{
    WTFLogAlways("abort");
    m_delegateCallback(SpeechRecognitionUpdate::create(clientIdentifier(), SpeechRecognitionUpdateType::End));
}

void SpeechRecognizer::stopRecognition()
{
    WTFLogAlways("stop");
    m_delegateCallback(SpeechRecognitionUpdate::create(clientIdentifier(), SpeechRecognitionUpdateType::End));
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER) && ENABLE(MEDIA_STREAM)
