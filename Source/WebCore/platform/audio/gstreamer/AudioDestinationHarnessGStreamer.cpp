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

#include "config.h"

#if ENABLE(WEB_AUDIO)

#include "AudioDestinationHarnessGStreamer.h"

#include "AudioUtilities.h"
#include "GStreamerQuirks.h"
#include <gst/audio/gstaudiobasesink.h>
#include <wtf/Scope.h>

namespace WebCore {

GST_DEBUG_CATEGORY(webkit_audio_destination_harness_debug);
#define GST_CAT_DEFAULT webkit_audio_destination_harness_debug

static GstAudioChannelPosition webKitWebAudioGStreamerChannelPosition2(int channelIndex)
{
    GstAudioChannelPosition position = GST_AUDIO_CHANNEL_POSITION_NONE;

    switch (channelIndex) {
    case AudioBus::ChannelLeft:
        position = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        break;
    case AudioBus::ChannelRight:
        position = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        break;
    case AudioBus::ChannelCenter:
        position = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        break;
    case AudioBus::ChannelLFE:
        position = GST_AUDIO_CHANNEL_POSITION_LFE1;
        break;
    case AudioBus::ChannelSurroundLeft:
        position = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        break;
    case AudioBus::ChannelSurroundRight:
        position = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
        break;
    default:
        break;
    };

    return position;
}

AudioDestinationHarnessGStreamer::AudioDestinationHarnessGStreamer(const CreationOptions& options)
    : AudioDestination(options)
    , m_renderBus(AudioBus::create(options.numberOfOutputChannels, AudioUtilities::renderQuantumSize, false))
    , m_renderTimer(*this, &AudioDestinationHarnessGStreamer::renderTimerFired)
{
    ensureGStreamerInitialized();
    registerWebKitGStreamerElements();

    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_audio_destination_harness_debug, "webkitaudiodestinationharness", 0, "WebKit WebAudio Destination");
    });

    auto numberOfChannels = m_renderBus->numberOfChannels();

    guint64 channelMask = 0;
    Vector<GstAudioChannelPosition> positions(numberOfChannels, [&](size_t channelIndex) -> GstAudioChannelPosition {
        return webKitWebAudioGStreamerChannelPosition2(channelIndex);
    });

    gst_audio_channel_positions_to_mask(reinterpret_cast<GstAudioChannelPosition*>(positions.mutableSpan().data()),
        numberOfChannels, FALSE, &channelMask);

    m_caps = gst_caps_new_simple("audio/x-raw", "rate", G_TYPE_INT, static_cast<int>(sampleRate()),
        "channels", G_TYPE_INT, static_cast<int>(numberOfChannels),
        "channel-mask", GST_TYPE_BITMASK, channelMask,
        "format", G_TYPE_STRING, GST_AUDIO_NE(F32),
        "layout", G_TYPE_STRING, "non-interleaved", nullptr);

    gst_audio_info_from_caps(&m_info, m_caps.get());
}

AudioDestinationHarnessGStreamer::~AudioDestinationHarnessGStreamer()
{
    auto scopeExit = makeScopeExit([&] {
        notifyStopResult(true);
    });
}

unsigned AudioDestinationHarnessGStreamer::framesPerBuffer() const
{
    return AudioUtilities::renderQuantumSize;
}

void AudioDestinationHarnessGStreamer::start(DispatchToRenderThreadCallback&& dispatchToRenderThread, CompletionHandler<void(bool)>&& completionHandler)
{
    auto& quirksManager = GStreamerQuirksManager::singleton();
    GRefPtr audioSink = quirksManager.createWebAudioSink();
    m_audioSinkAvailable = audioSink;
    if (audioSink) {

        if (gstObjectHasProperty(audioSink.get(), "sync"_s))
            g_object_set(audioSink.get(), "sync", FALSE, nullptr);
        if (gstObjectHasProperty(audioSink.get(), "async"_s))
            g_object_set(audioSink.get(), "async", FALSE, nullptr);

        // Probe platform early on for a working audio output device in autoaudiosink.
        auto nameView = StringView::fromLatin1(GST_OBJECT_NAME(audioSink.get()));
        if (nameView.startsWith("autoaudiosink"_s)) {
            g_signal_connect(audioSink.get(), "child-added", G_CALLBACK(+[](GstChildProxy*, GObject* object, gchar*, gpointer) {
                // TODO: Check if sink supports planar F32, create a converter if not.

                if (GST_IS_AUDIO_BASE_SINK(object))
                    g_object_set(GST_AUDIO_BASE_SINK(object), "buffer-time", static_cast<gint64>(100000), nullptr);

                if (gstObjectHasProperty(GST_ELEMENT_CAST(object), "sync"_s))
                    g_object_set(object, "sync", FALSE, nullptr);
                if (gstObjectHasProperty(GST_ELEMENT_CAST(object), "async"_s))
                    g_object_set(object, "async", FALSE, nullptr);

            }), nullptr);

            // Autoaudiosink does the real sink detection in the GST_STATE_NULL->READY transition
            // so it's best to roll it to READY as soon as possible to ensure the underlying platform
            // audiosink was loaded correctly.
            GstStateChangeReturn stateChangeReturn = gst_element_set_state(audioSink.get(), GST_STATE_READY);
            if (stateChangeReturn == GST_STATE_CHANGE_FAILURE) {
                GST_ERROR("Failed to change autoaudiosink element state");
                gst_element_set_state(audioSink.get(), GST_STATE_NULL);
                m_audioSinkAvailable = false;
            }
        }
    } else
        GST_ERROR("Failed to create GStreamer audio sink element");

    if (m_audioSinkAvailable) {
        GRefPtr bin = gst_bin_new(nullptr);
        auto audioconvert = gst_element_factory_make("audioconvert", nullptr);
        gst_bin_add_many(GST_BIN_CAST(bin.get()), audioconvert, audioSink.get(), nullptr);
        gst_element_link(audioconvert, audioSink.get());
        GRefPtr sinkPad = adoptGRef(gst_element_get_static_pad(audioconvert, "sink"));
        gst_element_add_pad(bin.get(), gst_ghost_pad_new("sink", sinkPad.get()));

        m_harness = GStreamerElementHarness::create(WTF::move(bin));
        m_harness->setIsLive(true);

        m_pool = adoptGRef(gst_buffer_pool_new());
        GstStructure* config = gst_buffer_pool_get_config(m_pool.get());
        auto bufferSize = sizeof(float) * AudioUtilities::renderQuantumSize;
        gst_buffer_pool_config_set_params(config, nullptr, bufferSize * m_renderBus->numberOfChannels(), 0, 0);
        gst_buffer_pool_set_config(m_pool.get(), config);
        if (!gst_buffer_pool_set_active(m_pool.get(), TRUE))
            m_audioSinkAvailable = false;
    }

    if (m_audioSinkAvailable)
        m_renderCallback = WTF::move(dispatchToRenderThread);

    startRendering(WTF::move(completionHandler));
}

void AudioDestinationHarnessGStreamer::startRendering(CompletionHandler<void(bool)>&& completionHandler)
{
    ASSERT(m_audioSinkAvailable);
    m_startupCompletionHandler = WTF::move(completionHandler);

    GST_DEBUG("Starting");
    if (m_isPlaying) {
        notifyStartupResult(true);
        return;
    }

    if (!m_audioSinkAvailable) {
        notifyStartupResult(false);
        return;
    }

    GstClockTime duration = gst_util_uint64_scale(AudioUtilities::renderQuantumSize -5, GST_SECOND, sampleRate());
    m_renderTimer.startRepeating(Seconds::fromNanoseconds(static_cast<double>(duration)));

    notifyStartupResult(true);
}

void AudioDestinationHarnessGStreamer::stop(CompletionHandler<void(bool)>&& completionHandler)
{
    stopRendering(WTF::move(completionHandler));
}

void AudioDestinationHarnessGStreamer::stopRendering(CompletionHandler<void(bool)>&& completionHandler)
{
    m_stopCompletionHandler = WTF::move(completionHandler);
    m_renderTimer.stop();

    ASSERT(m_audioSinkAvailable);

    if (!m_isPlaying) {
        notifyStopResult(true);
        return;
    }

    if (!m_audioSinkAvailable) {
        notifyStopResult(false);
        return;
    }
}

void AudioDestinationHarnessGStreamer::notifyStartupResult(bool success)
{
    if (success)
        notifyIsPlaying(true);

    callOnMainThreadAndWait([completionHandler = WTF::move(m_startupCompletionHandler), success]() mutable {
        if (completionHandler)
            completionHandler(success);
    });
}

void AudioDestinationHarnessGStreamer::notifyStopResult(bool success)
{
    if (success)
        notifyIsPlaying(false);

    callOnMainThreadAndWait([completionHandler = WTF::move(m_stopCompletionHandler), success]() mutable {
        if (completionHandler)
            completionHandler(success);
    });
}

void AudioDestinationHarnessGStreamer::notifyIsPlaying(bool isPlaying)
{
    if (m_isPlaying == isPlaying)
        return;

    GST_DEBUG("Is playing: %s", boolForPrinting(isPlaying));
    m_isPlaying = isPlaying;

    {
        Locker locker { m_callbackLock };
        if (m_callback)
            m_callback->isPlayingDidChange();
    }
}

GRefPtr<GstBuffer> AudioDestinationHarnessGStreamer::allocateBuffer()
{
    GRefPtr<GstBuffer> buffer;
    GstFlowReturn ret = gst_buffer_pool_acquire_buffer(m_pool.get(), &buffer.outPtr(), nullptr);
    if (ret != GST_FLOW_OK)
        return nullptr;

    auto framesToPull = AudioUtilities::renderQuantumSize;
    auto bufferSize = sizeof(float) * AudioUtilities::renderQuantumSize;
    ASSERT(buffer);
    gst_buffer_add_audio_meta(buffer.get(), &m_info, framesToPull, nullptr);

    {
        GstMappedBuffer mappedBuffer(buffer.get(), GST_MAP_READ);
        ASSERT(mappedBuffer);
        for (unsigned channelIndex = 0; channelIndex < m_renderBus->numberOfChannels(); channelIndex++)
            m_renderBus->setChannelMemory(channelIndex, spanReinterpretCast<float>(mappedBuffer.mutableSpan<uint8_t>().subspan(channelIndex * bufferSize)).first(framesToPull));
    }

    return buffer;
}

void AudioDestinationHarnessGStreamer::renderAndPushFrames()
{
    auto buffer = allocateBuffer();
    if (!buffer)
        return;

    auto framesToPull = AudioUtilities::renderQuantumSize;
    GstClockTime timestamp = gst_util_uint64_scale(m_numberOfSamples, GST_SECOND, sampleRate());
    m_numberOfSamples += framesToPull;
    GstClockTime duration = gst_util_uint64_scale(framesToPull, GST_SECOND, sampleRate());

    AudioIOPosition outputTimestamp;
    outputTimestamp.position = Seconds::fromNanoseconds(timestamp);
    auto now = static_cast<double>((g_get_monotonic_time() + GST_TIME_AS_USECONDS(timestamp)) / 1000000.0);
    outputTimestamp.timestamp = MonotonicTime::fromRawSeconds(now);

    // FIXME: Add support for local/live audio input.
    callRenderCallback(*m_renderBus, framesToPull, outputTimestamp);

    GST_BUFFER_TIMESTAMP(buffer.get()) = timestamp;
    GST_BUFFER_DURATION(buffer.get()) = duration;

    GRefPtr sample = adoptGRef(gst_sample_new(buffer.get(), m_caps.get(), nullptr, nullptr));
    m_harness->pushSample(WTF::move(sample));
}

void AudioDestinationHarnessGStreamer::render()
{
    if (m_renderCallback) {
        m_renderCallback([&] {
            renderAndPushFrames();
        });
        return;
    }

    renderAndPushFrames();
}

void AudioDestinationHarnessGStreamer::renderTimerFired()
{
    render();
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
