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

#include "CSSStyleImageValue.h"
#include "CachedImage.h"
#include "DOMRectReadOnly.h"
#include "ExceptionOr.h"
#include "HTMLCanvasElement.h"
#include "HTMLImageElement.h"
#include "HTMLVideoElement.h"
#include "ImageBitmap.h"
#include "ImageBuffer.h"
#include "JSDOMPromiseDeferred.h"
#include "JSPlaneLayout.h"
#include "OffscreenCanvas.h"
#include "PixelBuffer.h"
#include "SVGImageElement.h"
#include "VideoColorSpace.h"
// #include "WebCodecsAudioDataAlgorithms.h"

#if USE(GSTREAMER)
#include "AudioDataGStreamer.h"
#endif

namespace WebCore {

WebCodecsAudioData::WebCodecsAudioData(ScriptExecutionContext& context)
    : ContextDestructionObserver(&context)
{
}

WebCodecsAudioData::WebCodecsAudioData(ScriptExecutionContext& context, WebCodecsAudioDataData&& data)
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


ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::create(ScriptExecutionContext& context, CanvasImageSource&& source, Init&& init)
{
    if (auto exception = checkImageUsability(context, source))
        return WTFMove(*exception);

    return switchOn(source,
    [&] (RefPtr<HTMLImageElement>& imageElement) -> ExceptionOr<Ref<WebCodecsAudioData>> {
        if (!init.timestamp)
            return Exception { TypeError,  "timestamp is not provided"_s };

        auto image = imageElement->cachedImage()->image()->nativeImageForCurrentFrame();
        if (!image)
            return Exception { InvalidStateError,  "Image element has no video frame"_s };

        return initializeFrameWithResourceAndSize(context, image.releaseNonNull(), WTFMove(init));
    },
    [&] (RefPtr<SVGImageElement>& imageElement) -> ExceptionOr<Ref<WebCodecsAudioData>> {
        if (!init.timestamp)
            return Exception { TypeError,  "timestamp is not provided"_s };

        auto image = imageElement->cachedImage()->image()->nativeImageForCurrentFrame();
        if (!image)
            return Exception { InvalidStateError,  "Image element has no video frame"_s };

        return initializeFrameWithResourceAndSize(context, image.releaseNonNull(), WTFMove(init));
    },
    [&] (RefPtr<CSSStyleImageValue>& cssImage) -> ExceptionOr<Ref<WebCodecsAudioData>> {
        if (!init.timestamp)
            return Exception { TypeError,  "timestamp is not provided"_s };

        auto image = cssImage->image()->image()->nativeImageForCurrentFrame();
        if (!image)
            return Exception { InvalidStateError,  "CSS Image has no video frame"_s };

        return initializeFrameWithResourceAndSize(context, image.releaseNonNull(), WTFMove(init));
    },
#if ENABLE(VIDEO)
    [&] (RefPtr<HTMLVideoElement>& video) -> ExceptionOr<Ref<WebCodecsAudioData>> {
        RefPtr<AudioData> videoFrame = video->player() ? video->player()->videoFrameForCurrentTime() : nullptr;
        if (!videoFrame)
            return Exception { InvalidStateError,  "Video element has no video frame"_s };
        return initializeFrameFromOtherFrame(context, videoFrame.releaseNonNull(), WTFMove(init));
    },
#endif
    [&] (RefPtr<HTMLCanvasElement>& canvas) -> ExceptionOr<Ref<WebCodecsAudioData>> {
        if (!init.timestamp)
            return Exception { TypeError,  "timestamp is not provided"_s };

        if (!canvas->width() || !canvas->height())
            return Exception { InvalidStateError,  "Input canvas has a bad size"_s };

        auto videoFrame = canvas->toAudioData();
        if (!videoFrame)
            return Exception { InvalidStateError,  "Canvas has no frame"_s };
        return initializeFrameFromOtherFrame(context, videoFrame.releaseNonNull(), WTFMove(init));
    },
#if ENABLE(OFFSCREEN_CANVAS)
    [&] (RefPtr<OffscreenCanvas>& canvas) -> ExceptionOr<Ref<WebCodecsAudioData>> {
        if (!init.timestamp)
            return Exception { TypeError,  "timestamp is not provided"_s };

        if (!canvas->width() || !canvas->height())
            return Exception { InvalidStateError,  "Input canvas has a bad size"_s };

        auto* imageBuffer = canvas->buffer();
        if (!imageBuffer)
            return Exception { InvalidStateError,  "Input canvas has no image buffer"_s };

        return create(context, *imageBuffer, { static_cast<int>(canvas->width()), static_cast<int>(canvas->height()) }, WTFMove(init));
    },
#endif // ENABLE(OFFSCREEN_CANVAS)
    [&] (RefPtr<ImageBitmap>& image) -> ExceptionOr<Ref<WebCodecsAudioData>> {
        if (!init.timestamp)
            return Exception { TypeError,  "timestamp is not provided"_s };

        if (!image->width() || !image->height())
            return Exception { InvalidStateError,  "Input image has a bad size"_s };

        auto* imageBuffer = image->buffer();
        if (!imageBuffer)
            return Exception { InvalidStateError,  "Input image has no image buffer"_s };

        return create(context, *imageBuffer, { static_cast<int>(image->width()), static_cast<int>(image->height()) }, WTFMove(init));
    });
}

ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::create(ScriptExecutionContext& context, ImageBuffer& buffer, IntSize size, WebCodecsAudioData::Init&& init)
{
    PixelBufferFormat format { AlphaPremultiplication::Unpremultiplied, PixelFormat::BGRA8, DestinationColorSpace::SRGB() };
    IntRect region { IntPoint::zero(), size };

    auto pixelBuffer = buffer.getPixelBuffer(format, region);
    if (!pixelBuffer)
        return Exception { InvalidStateError,  "Buffer has no frame"_s };

    auto videoFrame = AudioData::createFromPixelBuffer(pixelBuffer.releaseNonNull(), { PlatformVideoColorPrimaries::Bt709, PlatformVideoTransferCharacteristics::Iec6196621, PlatformVideoMatrixCoefficients::Rgb, true });

    if (!videoFrame)
        return Exception { InvalidStateError,  "Unable to create frame from buffer"_s };

    return WebCodecsAudioData::initializeFrameFromOtherFrame(context, videoFrame.releaseNonNull(), WTFMove(init));
}

ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::create(ScriptExecutionContext& context, Ref<WebCodecsAudioData>&& initFrame, Init&& init)
{
    if (initFrame->isDetached())
        return Exception { InvalidStateError,  "AudioData is detached"_s };
    return initializeFrameFromOtherFrame(context, WTFMove(initFrame), WTFMove(init));
}

static std::optional<Exception> validateI420Sizes(const WebCodecsAudioData::BufferInit& init)
{
    if (init.codedWidth % 2 || init.codedHeight % 2)
        return Exception { TypeError, "coded width or height is odd"_s };
    if (init.visibleRect && (static_cast<size_t>(init.visibleRect->x) % 2 || static_cast<size_t>(init.visibleRect->x) % 2))
        return Exception { TypeError, "visible x or y is odd"_s };
    return { };
}

// https://w3c.github.io/webcodecs/#dom-videoframe-videoframe-data-init
ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::create(ScriptExecutionContext& context, BufferSource&& data, BufferInit&& init)
{
    if (!isValidAudioDataBufferInit(init))
        return Exception { TypeError, "buffer init is not valid"_s };

    DOMRectInit defaultRect { 0, 0, static_cast<double>(init.codedWidth), static_cast<double>(init.codedHeight) };
    auto parsedRectOrExtension = parseVisibleRect(defaultRect, init.visibleRect, init.codedWidth, init.codedHeight, init.format);
    if (parsedRectOrExtension.hasException())
        return parsedRectOrExtension.releaseException();

    auto parsedRect = parsedRectOrExtension.releaseReturnValue();
    auto layoutOrException = computeLayoutAndAllocationSize(defaultRect, init.layout, init.format);
    if (layoutOrException.hasException())
        return layoutOrException.releaseException();
    
    auto layout = layoutOrException.releaseReturnValue();
    if (data.length() < layout.allocationSize)
        return Exception { TypeError, makeString("Data is too small ", data.length(), " / ", layout.allocationSize) };

    auto colorSpace = videoFramePickColorSpace(init.colorSpace, init.format);
    RefPtr<AudioData> videoFrame;
    if (init.format == VideoPixelFormat::NV12) {
        if (init.codedWidth % 2 || init.codedHeight % 2)
            return Exception { TypeError, "coded width or height is odd"_s };
        if (init.visibleRect && (static_cast<size_t>(init.visibleRect->x) % 2 || static_cast<size_t>(init.visibleRect->x) % 2))
            return Exception { TypeError, "visible x or y is odd"_s };
        videoFrame = AudioData::createNV12({ data.data(), data.length() }, parsedRect.width, parsedRect.height, layout.computedLayouts[0], layout.computedLayouts[1], WTFMove(colorSpace));
    } else if (init.format == VideoPixelFormat::RGBA || init.format == VideoPixelFormat::RGBX)
        videoFrame = AudioData::createRGBA({ data.data(), data.length() }, parsedRect.width, parsedRect.height, layout.computedLayouts[0], WTFMove(colorSpace));
    else if (init.format == VideoPixelFormat::BGRA || init.format == VideoPixelFormat::BGRX)
        videoFrame = AudioData::createBGRA({ data.data(), data.length() }, parsedRect.width, parsedRect.height, layout.computedLayouts[0], WTFMove(colorSpace));
    else if (init.format == VideoPixelFormat::I420) {
        if (auto exception = validateI420Sizes(init))
            return WTFMove(*exception);
        videoFrame = AudioData::createI420({ data.data(), data.length() }, parsedRect.width, parsedRect.height, layout.computedLayouts[0], layout.computedLayouts[1], layout.computedLayouts[2], WTFMove(colorSpace));
    } else if (init.format == VideoPixelFormat::I420A) {
        if (auto exception = validateI420Sizes(init))
            return WTFMove(*exception);
        videoFrame = AudioData::createI420A({ data.data(), data.length() }, parsedRect.width, parsedRect.height, layout.computedLayouts[0], layout.computedLayouts[1], layout.computedLayouts[2], layout.computedLayouts[3], WTFMove(colorSpace));
    } else
        return Exception { NotSupportedError, "VideoPixelFormat is not supported"_s };

    if (!videoFrame)
        return Exception { TypeError, "Unable to create internal resource from data"_s };
    
    return WebCodecsAudioData::create(context, videoFrame.releaseNonNull(), WTFMove(init));
}

Ref<WebCodecsAudioData> WebCodecsAudioData::create(ScriptExecutionContext& context, Ref<AudioData>&& videoFrame, BufferInit&& init)
{
    ASSERT(isValidAudioDataBufferInit(init));

    auto result = adoptRef(*new WebCodecsAudioData(context));
    result->m_data.internalFrame = WTFMove(videoFrame);
    result->m_data.format = init.format;

    result->m_data.codedWidth = result->m_data.internalFrame->presentationSize().width();
    result->m_data.codedHeight = result->m_data.internalFrame->presentationSize().height();

    result->m_data.visibleLeft = 0;
    result->m_data.visibleTop = 0;

    if (init.visibleRect) {
        result->m_data.visibleWidth = init.visibleRect->width;
        result->m_data.visibleHeight = init.visibleRect->height;
    } else {
        result->m_data.visibleWidth = result->m_data.codedWidth;
        result->m_data.visibleHeight = result->m_data.codedHeight;
    }

    result->m_data.displayWidth = init.displayWidth.value_or(result->m_data.visibleWidth);
    result->m_data.displayHeight = init.displayHeight.value_or(result->m_data.visibleHeight);

    result->m_data.duration = init.duration;
    result->m_data.timestamp = init.timestamp;

    return result;
}

static VideoPixelFormat computeVideoPixelFormat(VideoPixelFormat baseFormat, bool shouldDiscardAlpha)
{
    if (!shouldDiscardAlpha)
        return baseFormat;
    switch (baseFormat) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::I422:
    case VideoPixelFormat::NV12:
    case VideoPixelFormat::I444:
    case VideoPixelFormat::RGBX:
    case VideoPixelFormat::BGRX:
        return baseFormat;
    case VideoPixelFormat::I420A:
        return VideoPixelFormat::I420;
    case VideoPixelFormat::RGBA:
        return VideoPixelFormat::RGBX;
    case VideoPixelFormat::BGRA:
        return VideoPixelFormat::BGRX;
    }
    return baseFormat;
}

// https://w3c.github.io/webcodecs/#videoframe-initialize-frame-from-other-frame
ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::initializeFrameFromOtherFrame(ScriptExecutionContext& context, Ref<WebCodecsAudioData>&& videoFrame, Init&& init)
{
    auto codedWidth = videoFrame->m_data.codedWidth;
    auto codedHeight = videoFrame->m_data.codedHeight;
    auto format = computeVideoPixelFormat(videoFrame->m_data.format.value_or(VideoPixelFormat::I420), init.alpha == WebCodecsAlphaOption::Discard);
    if (!validateAudioDataInit(init, codedWidth, codedHeight, format))
        return Exception { TypeError,  "AudioDataInit is not valid"_s };

    auto result = adoptRef(*new WebCodecsAudioData(context));
    result->m_data.internalFrame = videoFrame->m_data.internalFrame;
    if (videoFrame->m_data.format)
        result->m_data.format = format;

    result->m_data.codedWidth = videoFrame->m_data.codedWidth;
    result->m_data.codedHeight = videoFrame->m_data.codedHeight;

    initializeVisibleRectAndDisplaySize(result.get(), init, DOMRectInit { static_cast<double>(videoFrame->m_data.visibleLeft), static_cast<double>(videoFrame->m_data.visibleTop), static_cast<double>(videoFrame->m_data.visibleWidth), static_cast<double>(videoFrame->m_data.visibleHeight) }, videoFrame->m_data.displayWidth, videoFrame->m_data.displayHeight);

    result->m_data.duration = init.duration ? init.duration : videoFrame->m_data.duration;
    result->m_data.timestamp = init.timestamp.value_or(videoFrame->m_data.timestamp);

    return result;
}

ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::initializeFrameFromOtherFrame(ScriptExecutionContext& context, Ref<AudioData>&& internalAudioData, Init&& init)
{
    auto codedWidth = internalAudioData->presentationSize().width();
    auto codedHeight = internalAudioData->presentationSize().height();
    auto format = convertAudioDataPixelFormat(internalAudioData->pixelFormat(), init.alpha == WebCodecsAlphaOption::Discard);
    if (!validateAudioDataInit(init, codedWidth, codedHeight, format))
        return Exception { TypeError,  "AudioDataInit is not valid"_s };

    auto result = adoptRef(*new WebCodecsAudioData(context));
    result->m_data.internalFrame = WTFMove(internalAudioData);
    result->m_data.format = format;
    result->m_data.codedWidth = codedWidth;
    result->m_data.codedHeight = codedHeight;

    initializeVisibleRectAndDisplaySize(result.get(), init, DOMRectInit { 0, 0 , static_cast<double>(result->m_data.codedWidth), static_cast<double>(result->m_data.codedHeight) }, result->m_data.codedWidth, result->m_data.codedHeight);

    result->m_data.duration = init.duration;
    // FIXME: Use internalAudioData timestamp if available and init has no timestamp.
    result->m_data.timestamp = init.timestamp.value_or(0);

    return result;
}

// https://w3c.github.io/webcodecs/#videoframe-initialize-frame-with-resource-and-size
ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::initializeFrameWithResourceAndSize(ScriptExecutionContext& context, Ref<NativeImage>&& image, Init&& init)
{
    auto internalAudioData = AudioData::fromNativeImage(image.get());
    if (!internalAudioData)
        return Exception { TypeError,  "image has no resource"_s };

    auto codedWidth = image->size().width();
    auto codedHeight = image->size().height();
    auto format = convertAudioDataPixelFormat(internalAudioData->pixelFormat(), init.alpha == WebCodecsAlphaOption::Discard);
    if (!validateAudioDataInit(init, codedWidth, codedHeight, format))
        return Exception { TypeError,  "AudioDataInit is not valid"_s };

    auto result = adoptRef(*new WebCodecsAudioData(context));
    result->m_data.internalFrame = WTFMove(internalAudioData);
    result->m_data.format = format;
    result->m_data.codedWidth = codedWidth;
    result->m_data.codedHeight = codedHeight;

    initializeVisibleRectAndDisplaySize(result.get(), init, DOMRectInit { 0, 0 , static_cast<double>(result->m_data.codedWidth), static_cast<double>(result->m_data.codedHeight) }, result->m_data.codedWidth, result->m_data.codedHeight);

    result->m_data.duration = init.duration;
    result->m_data.timestamp = init.timestamp.value_or(0);

    return result;
}


ExceptionOr<size_t> WebCodecsAudioData::allocationSize(const CopyToOptions& options)
{
    if (isDetached())
        return Exception { InvalidStateError,  "AudioData is detached"_s };

    if (!m_data.format)
        return Exception { NotSupportedError,  "AudioData has no format"_s };

    auto layoutOrException = parseAudioDataCopyToOptions(*this, options);
    if (layoutOrException.hasException())
        return layoutOrException.releaseException();

    return layoutOrException.returnValue().allocationSize;
}

void WebCodecsAudioData::copyTo(BufferSource&& source, CopyToOptions&& options, CopyToPromise&& promise)
{
    if (isDetached()) {
        promise.reject(Exception { InvalidStateError,  "AudioData is detached"_s });
        return;
    }
    if (!m_data.format) {
        promise.reject(Exception { NotSupportedError,  "AudioData has no format"_s });
        return;
    }

    auto combinedLayoutOrException = parseAudioDataCopyToOptions(*this, options);
    if (combinedLayoutOrException.hasException()) {
        promise.reject(combinedLayoutOrException.releaseException());
        return;
    }

    auto combinedLayout = combinedLayoutOrException.releaseReturnValue();
    if (source.length() < combinedLayout.allocationSize) {
        promise.reject(Exception { TypeError,  "Buffer is too small"_s });
        return;
    }

    std::span<uint8_t> buffer { static_cast<uint8_t*>(source.mutableData()), source.length() };
    m_data.internalFrame->copyTo(buffer, *m_data.format, WTFMove(combinedLayout.computedLayouts), [source = WTFMove(source), promise = WTFMove(promise)](auto planeLayouts) mutable {
        if (!planeLayouts) {
            promise.reject(Exception { TypeError,  "Unable to copy data"_s });
            return;
        }
        promise.resolve(WTFMove(*planeLayouts));
    });
}

ExceptionOr<Ref<WebCodecsAudioData>> WebCodecsAudioData::clone(ScriptExecutionContext& context)
{
    if (isDetached())
        return Exception { InvalidStateError,  "AudioData is detached"_s };

    auto clone = adoptRef(*new WebCodecsAudioData(context, WebCodecsAudioDataData { m_data }));

    clone->m_colorSpace = &colorSpace();
    clone->m_codedRect = codedRect();
    clone->m_visibleRect = visibleRect();
    clone->m_isDetached = m_isDetached;

    return clone;
}

// https://w3c.github.io/webcodecs/#close-videoframe
void WebCodecsAudioData::close()
{
    m_data.internalFrame = nullptr;

    m_isDetached = true;

    m_data.format = { };

    m_data.codedWidth = 0;
    m_data.codedHeight = 0;
    m_data.displayWidth = 0;
    m_data.displayHeight = 0;
    m_data.visibleWidth = 0;
    m_data.visibleHeight = 0;
    m_data.visibleLeft = 0;
    m_data.visibleTop = 0;

    m_codedRect = nullptr;
    m_visibleRect = nullptr;
}

DOMRectReadOnly* WebCodecsAudioData::codedRect() const
{
    if (m_isDetached)
        return nullptr;
    if (!m_codedRect)
        m_codedRect = DOMRectReadOnly::create(0, 0, m_data.codedWidth, m_data.codedHeight);

    return m_codedRect.get();
}

DOMRectReadOnly* WebCodecsAudioData::visibleRect() const
{
    if (m_isDetached)
        return nullptr;
    if (!m_visibleRect)
        m_visibleRect = DOMRectReadOnly::create(m_data.visibleLeft, m_data.visibleTop, m_data.visibleWidth, m_data.visibleHeight);

    return m_visibleRect.get();
}

void WebCodecsAudioData::setDisplaySize(size_t width, size_t height)
{
    m_data.displayWidth = width;
    m_data.displayHeight = height;
}

void WebCodecsAudioData::setVisibleRect(const DOMRectInit& rect)
{
    m_data.visibleLeft = rect.x;
    m_data.visibleTop = rect.y;
    m_data.visibleWidth = rect.width;
    m_data.visibleHeight = rect.height;
}

VideoColorSpace& WebCodecsAudioData::colorSpace() const
{
    if (!m_colorSpace)
        m_colorSpace = m_data.internalFrame ? VideoColorSpace::create(m_data.internalFrame->colorSpace()) : VideoColorSpace::create();

    return *m_colorSpace.get();
}

} // namespace WebCore

#endif // ENABLE(WEB_CODECS)
