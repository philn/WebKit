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

#include "config.h"

#if USE(AUDIO_SESSION) && USE(PIPEWIRE)

#include "AudioSessionPipeWire.h"

namespace WebCore {

bool AudioSessionPipeWire::isPipeWireRunning()
{
    return false;
}

RefPtr<AudioSessionPipeWire> AudioSessionPipeWire::create()
{
    pw_init(nullptr, nullptr);

    auto loop = pw_thread_loop_new("pipewire-main-loop", nullptr);
    auto context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    auto core = pw_context_connect(context, nullptr, 0);
    //auto core = pw_context_connect_fd(context, fcntl(fd, F_DUPFD_CLOEXEC, 3), nullptr, 0);
    if (!core) {
        WTFLogAlways("Unable to connect to PipeWire");
        pw_context_destroy(context);
        pw_thread_loop_destroy(loop);
        return nullptr;
    }

    return adoptRef(*new AudioSessionPipeWire(loop, context, core));
}

AudioSessionPipeWire::AudioSessionPipeWire(struct pw_thread_loop*, struct pw_context*, struct pw_core*)
{
    
}

float AudioSessionPipeWire::sampleRate() const
{
    return 48000;
}

size_t AudioSessionPipeWire::bufferSize() const
{
    return 256;
}

size_t AudioSessionPipeWire::numberOfOutputChannels() const
{
    return 2;
}

size_t AudioSessionPipeWire::maximumNumberOfOutputChannels() const
{
    return 4;
}

size_t AudioSessionPipeWire::preferredBufferSize() const
{
    return 256;
}

void AudioSessionPipeWire::setPreferredBufferSize(size_t)
{
    
}

} // namespace WebCore

#endif // USE(AUDIO_SESSION) && USE(PIPEWIRE)
