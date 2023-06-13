/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Copyright (C) 2020 Igalia S.L.
 * Author: Thibault Saunier <tsaunier@igalia.com>
 * Author: Alejandro G. Castro <alex@igalia.com>
 * Copyright (C) 2021 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "MockRealtimeVideoSource.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "MockRealtimeVideoSourceGStreamer.h"

#include "GStreamerCaptureDeviceManager.h"
#include "MockRealtimeMediaSourceCenter.h"
#include "PixelBuffer.h"
#include "VideoFrameGStreamer.h"
#include <gst/app/gstappsrc.h>

namespace WebCore {

CaptureSourceOrError MockRealtimeVideoSource::create(String&& deviceID, AtomString&& name, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints, PageIdentifier)
{
#ifndef NDEBUG
    auto device = MockRealtimeMediaSourceCenter::mockDeviceWithPersistentID(deviceID);
    ASSERT(device);
    if (!device)
        return { "No mock camera device"_s };
#endif

    Ref<RealtimeMediaSource> source = adoptRef(*new MockRealtimeVideoSourceGStreamer(WTFMove(deviceID), WTFMove(name), WTFMove(hashSalts)));
    if (constraints) {
        if (auto error = source->applyConstraints(*constraints))
            return WTFMove(error.value().badConstraint);
    }

    return source;
}

CaptureSourceOrError MockDisplayCaptureSourceGStreamer::create(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints, PageIdentifier pageIdentifier)
{
    auto mockSource = adoptRef(*new MockRealtimeVideoSourceGStreamer(String { device.persistentId() }, AtomString { device.label() }, MediaDeviceHashSalts { hashSalts }));

    if (constraints) {
        if (auto error = mockSource->applyConstraints(*constraints))
            return WTFMove(error->message);
    }

    Ref<RealtimeMediaSource> source = adoptRef(*new MockDisplayCaptureSourceGStreamer(device, WTFMove(mockSource), WTFMove(hashSalts), pageIdentifier));
    return source;
}

MockDisplayCaptureSourceGStreamer::MockDisplayCaptureSourceGStreamer(const CaptureDevice& device, Ref<MockRealtimeVideoSourceGStreamer>&& source, MediaDeviceHashSalts&& hashSalts, PageIdentifier pageIdentifier)
    : RealtimeVideoCaptureSource(device, WTFMove(hashSalts), pageIdentifier)
    , m_source(WTFMove(source))
    , m_deviceType(device.type())
{
    m_source->addVideoFrameObserver(*this);
}

MockDisplayCaptureSourceGStreamer::~MockDisplayCaptureSourceGStreamer()
{
    m_source->removeVideoFrameObserver(*this);
}

void MockDisplayCaptureSourceGStreamer::stopProducingData()
{
    m_source->removeVideoFrameObserver(*this);
    m_source->stop();
}

void MockDisplayCaptureSourceGStreamer::requestToEnd(Observer& callingObserver)
{
    RealtimeMediaSource::requestToEnd(callingObserver);
    m_source->removeVideoFrameObserver(*this);
    m_source->requestToEnd(callingObserver);
}

void MockDisplayCaptureSourceGStreamer::setMuted(bool isMuted)
{
    RealtimeMediaSource::setMuted(isMuted);
    m_source->setMuted(isMuted);
}

void MockDisplayCaptureSourceGStreamer::videoFrameAvailable(VideoFrame& videoFrame, VideoFrameTimeMetadata metadata)
{
    RealtimeMediaSource::videoFrameAvailable(videoFrame, metadata);
}

const RealtimeMediaSourceCapabilities& MockDisplayCaptureSourceGStreamer::capabilities()
{
    if (!m_capabilities) {
        RealtimeMediaSourceCapabilities capabilities(settings().supportedConstraints());

        // FIXME: what should these be?
        // Currently mimicking the values for SCREEN-1 in MockRealtimeMediaSourceCenter.cpp::defaultDevices()
        capabilities.setWidth(CapabilityValueOrRange(1, 1920));
        capabilities.setHeight(CapabilityValueOrRange(1, 1080));
        capabilities.setFrameRate(CapabilityValueOrRange(.01, 30.0));

        m_capabilities = WTFMove(capabilities);
    }
    return m_capabilities.value();
}

const RealtimeMediaSourceSettings& MockDisplayCaptureSourceGStreamer::settings()
{
    if (!m_currentSettings) {
        RealtimeMediaSourceSettings settings;
        settings.setFrameRate(frameRate());

        m_source->ensureIntrinsicSizeMaintainsAspectRatio();
        auto size = m_source->size();
        settings.setWidth(size.width());
        settings.setHeight(size.height());
        settings.setDeviceId(hashedId());
        settings.setDisplaySurface(m_source->mockScreen() ? DisplaySurfaceType::Monitor : DisplaySurfaceType::Window);
        settings.setLogicalSurface(false);

        RealtimeMediaSourceSupportedConstraints supportedConstraints;
        supportedConstraints.setSupportsFrameRate(true);
        supportedConstraints.setSupportsWidth(true);
        supportedConstraints.setSupportsHeight(true);
        supportedConstraints.setSupportsDisplaySurface(true);
        supportedConstraints.setSupportsLogicalSurface(true);
        supportedConstraints.setSupportsDeviceId(true);

        settings.setSupportedConstraints(supportedConstraints);

        m_currentSettings = WTFMove(settings);
    }
    return m_currentSettings.value();
}

MockRealtimeVideoSourceGStreamer::MockRealtimeVideoSourceGStreamer(String&& deviceID, AtomString&& name, MediaDeviceHashSalts&& hashSalts)
    : MockRealtimeVideoSource(WTFMove(deviceID), WTFMove(name), WTFMove(hashSalts), { })
{
    ensureGStreamerInitialized();
}

void MockRealtimeVideoSourceGStreamer::startProducingData()
{
    if (m_capturer && m_capturer->pipeline())
        return;

    auto& singleton = GStreamerVideoCaptureDeviceManager::singleton();
    auto device = singleton.gstreamerDeviceWithUID2(this->captureDevice().label());
    device.setIsMockDevice(true);
    m_capturer = makeUnique<GStreamerVideoCapturer>(WTFMove(device));

    m_capturer->setupPipeline();

    if (deviceType() == CaptureDevice::DeviceType::Camera)
        m_capturer->setSize(size().width(), size().height());

    m_capturer->setFrameRate(frameRate());
    m_capturer->setSinkVideoFrameCallback([this](auto&& videoFrame) {
        if (!isProducingData() || muted())
            return;
        dispatchVideoFrameToObservers(WTFMove(videoFrame), { });
    });
    m_capturer->play();

    MockRealtimeVideoSource::startProducingData();
}

void MockRealtimeVideoSourceGStreamer::stopProducingData()
{
    MockRealtimeVideoSource::stopProducingData();

    GST_INFO("Reset height and width after stopping source");
    setSize({ 0, 0 });
    m_capturer->stop();
}

void MockRealtimeVideoSourceGStreamer::updateSampleBuffer()
{
    auto imageBuffer = this->imageBuffer();
    if (!imageBuffer)
        return;

    auto pixelBuffer = imageBuffer->getPixelBuffer({ AlphaPremultiplication::Premultiplied, PixelFormat::BGRA8, DestinationColorSpace::SRGB() }, { { }, imageBuffer->truncatedLogicalSize() });
    if (!pixelBuffer)
        return;

    VideoFrameTimeMetadata metadata;
    metadata.captureTime = MonotonicTime::now().secondsSinceEpoch();
    auto presentationTime = MediaTime::createWithDouble((elapsedTime()).seconds());
    auto videoFrame = VideoFrameGStreamer::createFromPixelBuffer(pixelBuffer.releaseNonNull(), VideoFrameGStreamer::CanvasContentType::Canvas2D, videoFrameRotation(), presentationTime, size(), frameRate(), false, WTFMove(metadata));
    gst_app_src_push_sample(GST_APP_SRC_CAST(m_capturer->source()), videoFrame->sample());
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
