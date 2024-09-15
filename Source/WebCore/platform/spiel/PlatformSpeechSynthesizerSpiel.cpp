/*
 * Copyright (C) 2024 Igalia S.L. All rights reserved.
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
#include "PlatformSpeechSynthesizer.h"

#if ENABLE(SPEECH_SYNTHESIS) && USE(SPIEL) && USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "PlatformSpeechSynthesisUtterance.h"
#include "PlatformSpeechSynthesisVoice.h"
#include "WebKitAudioSinkGStreamer.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

#include <spiel/spiel.h>

namespace WebCore {

class SpielSpeechWrapper {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(SpielSpeechWrapper);
    WTF_MAKE_NONCOPYABLE(SpielSpeechWrapper);
public:
    explicit SpielSpeechWrapper(const PlatformSpeechSynthesizer&);
    ~SpielSpeechWrapper();

    Vector<RefPtr<PlatformSpeechSynthesisVoice>> initializeVoiceList();
    void pause();
    void resume();
    void speakUtterance(RefPtr<PlatformSpeechSynthesisUtterance>&&);
    void cancel();
    void resetState();

private:
    RefPtr<PlatformSpeechSynthesisUtterance> m_utterance;
    const PlatformSpeechSynthesizer& m_platformSynthesizer;
    GRefPtr<SpielSpeaker> m_speaker;
    Vector<GRefPtr<SpielVoice>> m_voices;
};

SpielSpeechWrapper::SpielSpeechWrapper(const PlatformSpeechSynthesizer& synthesizer)
    : m_platformSynthesizer(synthesizer)
{
    ensureGStreamerInitialized();
    registerWebKitGStreamerElements();

    GRefPtr<GstElement> audioSink = createPlatformAudioSink("speech"_s);
    if (!audioSink) {
        GST_ERROR("Failed to create GStreamer audio sink element");
        return;
    }

    // Async API can't really be used here because the voices initialization
    // (PlatformSpeechSynthesizer::initializeVoiceList()) is synchronous.
    GUniqueOutPtr<GError> error;
    m_speaker = adoptGRef(spiel_speaker_new_sync(nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("Spiel speaker failed to initialize: %s", error->message);
        return;
    }

    g_object_set(m_speaker.get(), "sink", audioSink.get(), nullptr);

    // FIXME: Plumb support for boundaryEventOccurred? Using range-started signal?

    g_signal_connect_swapped(m_speaker.get(), "utterance-started", G_CALLBACK(+[](SpielSpeechWrapper* self, SpielUtterance*) {
        self->m_platformSynthesizer.client().didStartSpeaking(*self->m_utterance);
    }), this);

    g_signal_connect_swapped(m_speaker.get(), "utterance-finished", G_CALLBACK(+[](SpielSpeechWrapper* self, SpielUtterance*) {
        self->m_platformSynthesizer.client().didFinishSpeaking(*self->m_utterance);
    }), this);
    g_signal_connect_swapped(m_speaker.get(), "utterance-canceled", G_CALLBACK(+[](SpielSpeechWrapper* self, SpielUtterance*) {
        self->m_platformSynthesizer.client().didFinishSpeaking(*self->m_utterance);
    }), this);

    g_signal_connect_swapped(m_speaker.get(), "utterance-error", G_CALLBACK(+[](SpielSpeechWrapper* self, SpielUtterance*) {
        self->m_platformSynthesizer.client().speakingErrorOccurred(*self->m_utterance);
    }), this);

    g_signal_connect_swapped(m_speaker.get(), "notify::paused", G_CALLBACK(+[](SpielSpeechWrapper* self, SpielSpeaker* speaker) {
        gboolean isPaused;
        g_object_get(speaker, "paused", &isPaused, nullptr);
        if (isPaused)
            self->m_platformSynthesizer.client().didPauseSpeaking(*self->m_utterance);
        else
            self->m_platformSynthesizer.client().didResumeSpeaking(*self->m_utterance);
    }), this);

    g_signal_connect_swapped(m_speaker.get(), "notify::voices", G_CALLBACK(+[](SpielSpeechWrapper* self, SpielSpeaker*) {
        self->m_platformSynthesizer.client().voicesDidChange();
    }), this);
}

SpielSpeechWrapper::~SpielSpeechWrapper()
{
    g_signal_handlers_disconnect_by_data(m_speaker.get(), this);
}

Vector<RefPtr<PlatformSpeechSynthesisVoice>> SpielSpeechWrapper::initializeVoiceList()
{
    Vector<RefPtr<PlatformSpeechSynthesisVoice>> platformVoices;
    auto voices = spiel_speaker_get_voices(m_speaker.get());
    unsigned position = 0;
    m_voices.clear();
    while (auto item = g_list_model_get_item(voices, position)) {
        auto voice = SPIEL_VOICE(item);
        auto name = makeString(span(spiel_voice_get_name(voice)));
        auto identifier = makeString(span(spiel_voice_get_identifier(voice)));
        const char* const* languages = spiel_voice_get_languages(voice);
        auto language = makeString(span(languages[0]));
        m_voices.append(GRefPtr(voice));
        platformVoices.append(PlatformSpeechSynthesisVoice::create(identifier, name, language, true, true));
        position++;
    }
    return platformVoices;
}

void SpielSpeechWrapper::pause()
{
    if (!m_utterance)
        return;

    spiel_speaker_pause(m_speaker.get());
}

void SpielSpeechWrapper::resume()
{
    if (!m_utterance)
        return;

    spiel_speaker_resume(m_speaker.get());
}

void SpielSpeechWrapper::speakUtterance(RefPtr<PlatformSpeechSynthesisUtterance>&& utterance)
{
    ASSERT(!m_utterance);
    ASSERT(utterance);
    if (!utterance)
        return;

    auto text = utterance->text().utf8();
    auto spielUtterance = adoptGRef(spiel_utterance_new(text.data()));
    auto language = utterance->lang().utf8();
    spiel_utterance_set_language(spielUtterance.get(), language.data());
    const auto& uri = utterance->voice()->voiceURI();
    for (auto& voice : m_voices) {
        auto identifier = StringView::fromLatin1(spiel_voice_get_identifier(voice.get()));
        if (uri == identifier) {
            spiel_utterance_set_voice(spielUtterance.get(), voice.get());
            break;
        }
    }
    spiel_utterance_set_volume(spielUtterance.get(), utterance->volume());
    spiel_utterance_set_pitch(spielUtterance.get(), utterance->pitch());
    spiel_utterance_set_rate(spielUtterance.get(), utterance->rate());
    m_utterance = WTFMove(utterance);
    spiel_speaker_speak(m_speaker.get(), spielUtterance.get());
}

void SpielSpeechWrapper::cancel()
{
    if (!m_utterance)
        return;

    spiel_speaker_cancel(m_speaker.get());
}

void SpielSpeechWrapper::resetState()
{
    cancel();
    m_utterance = nullptr;
}

Ref<PlatformSpeechSynthesizer> PlatformSpeechSynthesizer::create(PlatformSpeechSynthesizerClient& client)
{
    return adoptRef(*new PlatformSpeechSynthesizer(client));
}

PlatformSpeechSynthesizer::PlatformSpeechSynthesizer(PlatformSpeechSynthesizerClient& client)
    : m_speechSynthesizerClient(client)
{
}

PlatformSpeechSynthesizer::~PlatformSpeechSynthesizer() = default;

void PlatformSpeechSynthesizer::initializeVoiceList()
{
    if (!m_platformSpeechWrapper)
        m_platformSpeechWrapper = makeUnique<SpielSpeechWrapper>(*this);

    m_voiceList = m_platformSpeechWrapper->initializeVoiceList();
}

void PlatformSpeechSynthesizer::pause()
{
    if (!m_platformSpeechWrapper)
        return;
    m_platformSpeechWrapper->pause();
}

void PlatformSpeechSynthesizer::resume()
{
    if (!m_platformSpeechWrapper)
        return;
    m_platformSpeechWrapper->resume();
}

void PlatformSpeechSynthesizer::speak(RefPtr<PlatformSpeechSynthesisUtterance>&& utterance)
{
    if (!m_platformSpeechWrapper)
        m_platformSpeechWrapper = makeUnique<SpielSpeechWrapper>(*this);
    m_platformSpeechWrapper->speakUtterance(WTFMove(utterance));
}

void PlatformSpeechSynthesizer::cancel()
{
    if (!m_platformSpeechWrapper)
        return;
    m_platformSpeechWrapper->cancel();
}

void PlatformSpeechSynthesizer::resetState()
{
    if (!m_platformSpeechWrapper)
        return;
    m_platformSpeechWrapper->resetState();
}

} // namespace WebCore

#endif // ENABLE(SPEECH_SYNTHESIS) && USE(SPIEL) && USE(GSTREAMER)
