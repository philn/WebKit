/*
 *  Copyright (C) 2025 Igalia S.L
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

#if USE(AUDIO_SESSION) && USE(PIPEWIRE)

#include "AudioSession.h"
#include <wtf/TZoneMalloc.h>

#include <pipewire/core.h>
#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>

namespace WebCore {

class AudioSessionPipeWire final : public AudioSession {
    WTF_MAKE_TZONE_ALLOCATED(AudioSessionPipeWire);
public:
    static bool isPipeWireRunning();
    static RefPtr<AudioSessionPipeWire> create();
    ~AudioSessionPipeWire() = default;

    float sampleRate() const final;
    size_t bufferSize() const final;
    size_t numberOfOutputChannels() const final;
    size_t maximumNumberOfOutputChannels() const final;

    size_t preferredBufferSize() const override;
    void setPreferredBufferSize(size_t) override;

private:
     AudioSessionPipeWire(struct pw_thread_loop*, struct pw_context*, struct pw_core*);

     struct pw_thread_loop* m_loop;
     struct pw_context* m_context;
     struct pw_core* m_core;
     //struct spa_hook m_coreListener;
     int m_lastSeq;
     int m_pendingSeq;
     int m_lastError;

     struct pw_registry* m_registry { nullptr };
     //struct spa_hook m_registryListener;
};

} // namespace WebCore

#endif // USE(AUDIO_SESSION) && USE(PIPEWIRE)
