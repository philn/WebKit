/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
 * Copyright (C) 2023 Igalia S.L
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
#include "WebCodecsAudioData.h"

#if ENABLE(WEB_CODECS)

#include "ExceptionOr.h"
#include "JSDOMPromiseDeferred.h"
#include "PlatformRawAudioData.h"
#include "WebCodecsAudioDataAlgorithms.h"

namespace WebCore {

// https://www.w3.org/TR/webcodecs/#dom-audiodata-audiodata
ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::create(ScriptExecutionContext& context, Init&& init)
{
    if (!isValidAudioDataInit(init))
        return Exception { TypeError, "Invalid init data"_s };

    auto rawData = init.data.span();
    auto data = PlatformRawAudioData::create(WTFMove(rawData), init.format, init.sampleRate, init.timestamp, init.numberOfFrames, init.numberOfChannels);

    return adoptRef(*new WebCodecsAudioData(context, WebCodecsAudioInternalData { WTFMove(data) }));
}

Ref<WebCodecsAudioData> WebCodecsAudioData::create(ScriptExecutionContext& context, Ref<PlatformRawAudioData>&& data)
{
    return adoptRef(*new WebCodecsAudioData(context, WebCodecsAudioInternalData { WTFMove(data) }));
}

WebCodecsAudioData::WebCodecsAudioData(ScriptExecutionContext& context)
    : ContextDestructionObserver(&context)
{
}

WebCodecsAudioData::WebCodecsAudioData(ScriptExecutionContext& context, WebCodecsAudioInternalData&& data)
    : ContextDestructionObserver(&context)
    , m_data(WTFMove(data))
{
}

WebCodecsAudioData::~WebCodecsAudioData()
{
    if (m_isDetached)
        return;
    if (auto* context = scriptExecutionContext()) {
        context->postTask([](auto& context) {
            context.addConsoleMessage(MessageSource::JS, MessageLevel::Warning, "A AudioData was destroyed without having been closed explicitly"_s);
        });
    }
}

std::optional<AudioSampleFormat> WebCodecsAudioData::format() const
{
    if (!m_data.audioData)
        return { };

    return m_data.audioData->format();
}

float WebCodecsAudioData::sampleRate() const
{
    if (!m_data.audioData)
        return 0;

    return m_data.audioData->sampleRate();
}

size_t WebCodecsAudioData::numberOfFrames() const
{
    if (!m_data.audioData)
        return 0;

    return m_data.audioData->numberOfFrames();
}

size_t WebCodecsAudioData::numberOfChannels() const
{
    if (!m_data.audioData)
        return 0;

    return m_data.audioData->numberOfChannels();
}

std::optional<uint64_t> WebCodecsAudioData::duration()
{
    if (!m_data.audioData)
        return { };

    return m_data.audioData->duration();
}

int64_t WebCodecsAudioData::timestamp() const
{
    if (!m_data.audioData)
        return 0;

    return m_data.audioData->timestamp();
}

ExceptionOr<size_t> WebCodecsAudioData::allocationSize(const CopyToOptions& options)
{
    if (isDetached())
        return Exception { InvalidStateError,  "AudioData is detached"_s };

    auto copyElementCount = computeCopyElementCount(*this, options);
    if (copyElementCount.hasException())
        return copyElementCount.releaseException();

    auto destFormat = options.format.value_or(*format());
    auto bytesPerSample = computeBytesPerSample(destFormat);
    return copyElementCount.releaseReturnValue() * bytesPerSample;
}

// https://www.w3.org/TR/webcodecs/#dom-audiodata-copyto
ExceptionOr<void> WebCodecsAudioData::copyTo(BufferSource&& source, CopyToOptions&& options)
{
    if (isDetached())
        return Exception { InvalidStateError,  "AudioData is detached"_s };

    // XXX

    // if (!m_data.format) {
    //     promise.reject(Exception { NotSupportedError,  "AudioData has no format"_s });
    //     return;
    // }

    // auto combinedLayoutOrException = parseAudioDataCopyToOptions(*this, options);
    // if (combinedLayoutOrException.hasException()) {
    //     promise.reject(combinedLayoutOrException.releaseException());
    //     return;
    // }

    // auto combinedLayout = combinedLayoutOrException.releaseReturnValue();
    // if (source.length() < combinedLayout.allocationSize) {
    //     promise.reject(Exception { TypeError,  "Buffer is too small"_s });
    //     return;
    // }

    // std::span<uint8_t> buffer { static_cast<uint8_t*>(source.mutableData()), source.length() };
    // m_data.internalFrame->copyTo(buffer, *m_data.format, WTFMove(combinedLayout.computedLayouts), [source = WTFMove(source), promise = WTFMove(promise)](auto planeLayouts) mutable {
    //     if (!planeLayouts) {
    //         promise.reject(Exception { TypeError,  "Unable to copy data"_s });
    //         return;
    //     }
    //     promise.resolve(WTFMove(*planeLayouts));
    // });
}

// https://www.w3.org/TR/webcodecs/#dom-audiodata-clone
ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::clone(ScriptExecutionContext& context)
{
    if (isDetached())
        return Exception { InvalidStateError,  "AudioData is detached"_s };

    auto clone = adoptRef(*new WebCodecsAudioData(context, WebCodecsAudioInternalData { m_data }));

    clone->m_isDetached = m_isDetached;

    return clone;
}

// https://www.w3.org/TR/webcodecs/#dom-audiodata-close
void WebCodecsAudioData::close()
{
    m_data.audioData = nullptr;

    m_isDetached = true;
}

} // namespace WebCore

#endif // ENABLE(WEB_CODECS)
