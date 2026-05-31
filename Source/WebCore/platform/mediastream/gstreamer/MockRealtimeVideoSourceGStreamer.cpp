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
#include "IntSize.h"
#include "MockRealtimeMediaSourceCenter.h"
#include "PixelBuffer.h"
#include "VideoFrameGStreamer.h"
#include <gst/app/gstappsrc.h>

namespace WebCore {

CaptureSourceOrError MockRealtimeVideoSource::create(String&& deviceID, AtomString&& name, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints, std::optional<PageIdentifier> pageIdentifier)
{
#ifndef NDEBUG
    auto device = MockRealtimeMediaSourceCenter::mockDeviceWithPersistentID(deviceID);
    ASSERT(device);
    if (!device)
        return CaptureSourceOrError({ "No mock camera device"_s , MediaAccessDenialReason::PermissionDenied });
#endif

    Ref source = MockRealtimeVideoSourceGStreamer::create(WTF::move(deviceID), WTF::move(name), WTF::move(hashSalts), pageIdentifier, constraints);
    if (constraints) {
        std::optional<ApplyConstraintsError> error;
        source->applyConstraints(*constraints, [&](auto&& result) { error = WTF::move(result); });
        if (error)
            return CaptureSourceOrError(CaptureSourceError { error->invalidConstraint });
        // source->storePresetConstraints(*constraints);
    }

    return upcast<RealtimeMediaSource>(source);
}

MockRealtimeVideoSourceGStreamer::MockRealtimeVideoSourceGStreamer(String&& deviceID, AtomString&& name, MediaDeviceHashSalts&& hashSalts, std::optional<PageIdentifier> pageIdentifier, const MediaConstraints* constraints)
    : MockRealtimeVideoSource(WTF::move(deviceID), WTF::move(name), WTF::move(hashSalts), pageIdentifier)
{
    // if (constraints)
    //     storePresetConstraints(*constraints);
    ensureGStreamerInitialized();
    auto& singleton = GStreamerVideoCaptureDeviceManager::singleton();
    auto device = singleton.gstreamerDeviceWithUID(this->captureDevice().persistentId());
    ASSERT(device);
    if (!device)
        return;

    device->setIsMockDevice(true);
    m_capturer = adoptRef(*new GStreamerVideoCapturer(WTF::move(*device)));
    m_capturer->addObserver(*this);
    m_capturer->setupPipeline();
    m_capturer->setSinkVideoFrameCallback([this](auto&& videoFrame) {
        if (!isProducingData() || muted())
            return;
        dispatchVideoFrameToObservers(WTF::move(videoFrame), { });
    });
    singleton.registerCapturer(m_capturer);
}

MockRealtimeVideoSourceGStreamer::~MockRealtimeVideoSourceGStreamer()
{
    m_capturer->stop();
    m_capturer->removeObserver(*this);

    auto& singleton = GStreamerVideoCaptureDeviceManager::singleton();
    singleton.unregisterCapturer(*m_capturer);
}

void MockRealtimeVideoSourceGStreamer::startProducingData()
{
    m_capturer->setFrameRate(frameRate());
    m_capturer->start();
    MockRealtimeVideoSource::startProducingData();
}

void MockRealtimeVideoSourceGStreamer::stopProducingData()
{
    m_capturer->stop();
    MockRealtimeVideoSource::stopProducingData();
}

void MockRealtimeVideoSourceGStreamer::captureEnded()
{
    // NOTE: We could call captureFailed() like in the mock audio source, but that would trigger new
    // test failures. For some reason we want 'ended' MediaStreamTrack notifications only for audio
    // devices removal.
}

std::pair<GstClockTime, GstClockTime> MockRealtimeVideoSourceGStreamer::queryCaptureLatency() const
{
    if (!m_capturer)
        return { GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE };

    return m_capturer->queryLatency();
}

void MockRealtimeVideoSourceGStreamer::updateSampleBuffer()
{
    RefPtr imageBuffer = this->imageBufferInternal();
    if (!imageBuffer)
        return;

    auto pixelBuffer = imageBuffer->getPixelBuffer({ AlphaPremultiplication::Unpremultiplied, PixelFormat::BGRA8, DestinationColorSpace::SRGB() }, { { }, imageBuffer->truncatedLogicalSize() });
    if (!pixelBuffer)
        return;

    int frameRateNumerator, frameRateDenominator;
    gst_util_double_to_fraction(settings().frameRate(), &frameRateNumerator, &frameRateDenominator);
    gst_printerrln("-> %f fps", settings().frameRate());
    VideoFrameTimeMetadata metadata;
    metadata.captureTime = MonotonicTime::now().secondsSinceEpoch();

    VideoFrameGStreamer::CreateOptions options;
    options.presentationTime = fromGstClockTime(gst_util_uint64_scale(m_frameNumber, frameRateDenominator * GST_SECOND, frameRateNumerator));
    options.timeMetadata = WTF::move(metadata);

    auto videoFrame = VideoFrameGStreamer::createFromPixelBuffer(pixelBuffer.releaseNonNull(), m_capturer->size(), frameRate(), options);
    if (!videoFrame)
        return;

    // Mock GstDevice is an appsrc, see webkitMockDeviceCreateElement().
    auto appSrc = m_capturer->source();
    if (!appSrc || !GST_IS_APP_SRC(appSrc.get())) {
        GST_WARNING("AppSrc not available, capture source may have changed");
        return;
    }

    const auto& sample = videoFrame->sample();
    gst_app_src_push_sample(GST_APP_SRC_CAST(appSrc.get()), sample.get());
}

void MockRealtimeVideoSourceGStreamer::setSizeFrameRateAndZoom(const VideoPresetConstraints& constraints)
{
    MockRealtimeVideoSource::setSizeFrameRateAndZoom(constraints);

    if (!constraints.width || !constraints.height)
        return;

    m_capturer->setSize({ *constraints.width, *constraints.height });
}

void MockRealtimeVideoSourceGStreamer::storePresetConstraints(const MediaConstraints& constraints)
{
    auto resultingConstraints = extractVideoPresetConstraints(constraints);

    if (resultingConstraints.width)
        m_widthConstraint = *resultingConstraints.width;
    else if (resultingConstraints.height)
        m_widthConstraint = 0;
    if (resultingConstraints.height)
        m_heightConstraint = *resultingConstraints.height;
    else if (resultingConstraints.width)
        m_heightConstraint = 0;
    if (resultingConstraints.frameRate)
        m_frameRateConstraint = *resultingConstraints.frameRate;
    gst_printerrln("widthConstraint: %d heightConstraint: %d size: %dx%d @ %f fps", m_widthConstraint, m_heightConstraint, size().width(), size().height(), m_frameRateConstraint);
    WTFReportBacktrace();
    m_currentSettings = {};
}

void MockRealtimeVideoSourceGStreamer::applyConstraints(const MediaConstraints& constraints, ApplyConstraintsHandler&& callback)
{
    MockRealtimeVideoSource::applyConstraints(constraints, [this, constraints, callback = WTF::move(callback)](auto&& error) mutable {
        if (!error)
            storePresetConstraints(constraints);
        // m_currentSettings = {};
        callback(WTF::move(error));
    });
}

const RealtimeMediaSourceSettings& MockRealtimeVideoSourceGStreamer::settings()
{
    // if (!m_currentSettings) {
    //     RealtimeMediaSourceSettings settings;
    //     settings.setDeviceId(hashedId());

    //     RealtimeMediaSourceSupportedConstraints supportedConstraints;
    //     supportedConstraints.setSupportsDeviceId(true);
    //     supportedConstraints.setSupportsFacingMode(true);
    //     supportedConstraints.setSupportsWidth(true);
    //     supportedConstraints.setSupportsHeight(true);
    //     supportedConstraints.setSupportsAspectRatio(true);
    //     supportedConstraints.setSupportsFrameRate(true);
    //     settings.setSupportedConstraints(supportedConstraints);

    //     m_currentSettings = WTF::move(settings);
    // }
    [[maybe_unused]] const auto& settings = MockRealtimeVideoSource::settings();
    ASSERT(m_currentSettings);
    if (m_widthConstraint || m_heightConstraint) {
        auto desiredSize = computeResizedVideoFrameSize({ m_widthConstraint, m_heightConstraint }, intrinsicSize());

        auto videoFrameRotation = this->videoFrameRotation();
        if (videoFrameRotation == VideoFrameRotation::Left || videoFrameRotation == VideoFrameRotation::Right)
            desiredSize = desiredSize.transposedSize();

        m_currentSettings->setWidth(desiredSize.width());
        m_currentSettings->setHeight(desiredSize.height());
        gst_printerrln("->> mock %dx%d constraint: %dx%d", desiredSize.width(), desiredSize.height(), m_widthConstraint, m_heightConstraint);
    }

    if (m_frameRateConstraint // && m_frameRateConstraint < m_currentSettings->frameRate()
        ) {
        gst_printerrln("->> mock %f fps", m_frameRateConstraint);
        m_currentSettings->setFrameRate(m_frameRateConstraint);
    }
    m_currentSettings->setFacingMode(facingMode());
    return m_currentSettings.value();
}


} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
