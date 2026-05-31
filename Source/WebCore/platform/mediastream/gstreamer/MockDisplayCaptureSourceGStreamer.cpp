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
#include "MockDisplayCaptureSourceGStreamer.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "ContextDestructionObserverInlines.h"
#include "MockRealtimeMediaSourceCenter.h"

namespace WebCore {

CaptureSourceOrError MockDisplayCaptureSourceGStreamer::create(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints, std::optional<PageIdentifier> pageIdentifier)
{
    Ref mockSource = MockRealtimeVideoSourceGStreamer::create(String { device.persistentId() }, AtomString { device.label() }, MediaDeviceHashSalts { hashSalts }, pageIdentifier, constraints);

    if (constraints) {
        std::optional<ApplyConstraintsError> error;
        mockSource->applyConstraints(*constraints, [&](auto&& result) { error = WTF::move(result); });
        if (error)
            return CaptureSourceOrError(CaptureSourceError { error->invalidConstraint });
    }

    Ref<RealtimeMediaSource> source = adoptRef(*new MockDisplayCaptureSourceGStreamer(device, WTF::move(mockSource), WTF::move(hashSalts), pageIdentifier));
    return source;
}

MockDisplayCaptureSourceGStreamer::MockDisplayCaptureSourceGStreamer(const CaptureDevice& device, Ref<MockRealtimeVideoSourceGStreamer>&& source, MediaDeviceHashSalts&& hashSalts, std::optional<PageIdentifier> pageIdentifier)
    : RealtimeVideoCaptureSource(device, WTF::move(hashSalts), pageIdentifier)
    , m_source(WTF::move(source))
    , m_deviceType(device.type())
{
    // auto mockDevice = MockRealtimeMediaSourceCenter::mockDeviceWithPersistentID(persistentID());
    // ASSERT(mockDevice);
    // auto& properties = std::get<MockDisplayProperties>(mockDevice->properties);
    // setIntrinsicSize(properties.defaultSize);
    // setSize(properties.defaultSize);

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

void MockDisplayCaptureSourceGStreamer::requestToEnd(RealtimeMediaSourceObserver& callingObserver)
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
        capabilities.setWidth({ 1, 1920 });
        capabilities.setHeight({ 1, 1080 });
        capabilities.setFrameRate({ .01, 30.0 });

        capabilities.setDeviceId(hashedId());

        m_capabilities = WTF::move(capabilities);
    }
    return m_capabilities.value();
}

void MockDisplayCaptureSourceGStreamer::storePresetConstraints(const MediaConstraints& constraints)
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
    // gst_printerrln("widthConstraint: %d heightConstraint: %d size: %dx%d @ %f fps", m_widthConstraint, m_heightConstraint, size().width(), size().height(), m_frameRateConstraint);
    // WTFReportBacktrace();
    m_source->removeVideoFrameObserver(*this);
    m_source->addVideoFrameObserver(*this, { m_widthConstraint, m_heightConstraint }, m_frameRateConstraint);
    m_currentSettings = {};
}

void MockDisplayCaptureSourceGStreamer::applyConstraints(const MediaConstraints& constraints, ApplyConstraintsHandler&& callback)
{
    RealtimeMediaSource::applyConstraints(constraints, [this, constraints, callback = WTF::move(callback)](auto&& error) mutable {
        if (!error)
            storePresetConstraints(constraints);

        callback(WTF::move(error));
    });
}

IntSize MockDisplayCaptureSourceGStreamer::computeResizedVideoFrameSize(IntSize desiredSize, IntSize intrinsicSize)
{
    // We keep the aspect ratio of the intrinsic size for the frame size as getDisplayMedia allows max constraints only.
    if (!intrinsicSize.width() || !intrinsicSize.height())
        return desiredSize;

    if (!desiredSize.height())
        desiredSize.setHeight(intrinsicSize.height());
    if (!desiredSize.width())
        desiredSize.setWidth(intrinsicSize.width());

    auto maxHeight = std::min(desiredSize.height(), intrinsicSize.height());
    auto maxWidth = std::min(desiredSize.width(), intrinsicSize.width());

    auto heightForMaxWidth = maxWidth * intrinsicSize.height() / intrinsicSize.width();
    auto widthForMaxHeight = maxHeight * intrinsicSize.width() / intrinsicSize.height();

    if (heightForMaxWidth <= maxHeight)
        return { maxWidth, heightForMaxWidth };

    if (widthForMaxHeight <= maxWidth)
        return { widthForMaxHeight, maxHeight };

    return intrinsicSize;
}

const RealtimeMediaSourceSettings& MockDisplayCaptureSourceGStreamer::settings()
{
    if (!m_currentSettings) {
        // auto settings = m_source->settings().isolatedCopy();
        RealtimeMediaSourceSettings settings;

        if (m_widthConstraint || m_heightConstraint) {
            // TODO: intrinsic size is 0x0 likely because we don't override RealtimeVideoCaptureSource::applyFrameRateAndZoomWithPreset
            gst_printerrln("->>>>>>> intrinsic size: %dx%d", intrinsicSize().width(), intrinsicSize().height());
            auto desiredSize = computeResizedVideoFrameSize({ m_widthConstraint, m_heightConstraint }, intrinsicSize());

            // auto videoFrameRotation = this->videoFrameRotation();
            // if (videoFrameRotation == VideoFrameRotation::Left || videoFrameRotation == VideoFrameRotation::Right)
            //     desiredSize = desiredSize.transposedSize();

            settings.setWidth(desiredSize.width());
            settings.setHeight(desiredSize.height());
            gst_printerrln("->> mock %dx%d constraint: %dx%d", desiredSize.width(), desiredSize.height(), m_widthConstraint, m_heightConstraint);
        } else {
            auto size = this->size();
            settings.setWidth(size.width());
            settings.setHeight(size.height());
        }

        if (m_frameRateConstraint // && m_frameRateConstraint < m_currentSettings->frameRate()
        ) {
            gst_printerrln("->> mock %f fps", m_frameRateConstraint);
            settings.setFrameRate(m_frameRateConstraint);
        } else
            settings.setFrameRate(frameRate());

        // m_source->ensureIntrinsicSizeMaintainsAspectRatio();
        // auto size = m_source->size();
        // settings.setWidth(size.width());
        // settings.setHeight(size.height());
        // settings.setDeviceId(hashedId());
        settings.setDisplaySurface(m_source->mockScreen() ? DisplaySurfaceType::Monitor : DisplaySurfaceType::Window);
        settings.setLogicalSurface(false);

        RealtimeMediaSourceSupportedConstraints supportedConstraints = settings.supportedConstraints();
        supportedConstraints.setSupportsFrameRate(true);
        supportedConstraints.setSupportsWidth(true);
        supportedConstraints.setSupportsHeight(true);
        supportedConstraints.setSupportsDisplaySurface(true);
        supportedConstraints.setSupportsLogicalSurface(true);
        supportedConstraints.setSupportsDeviceId(true);

        settings.setSupportedConstraints(supportedConstraints);

        m_currentSettings = WTF::move(settings);
    }
    return m_currentSettings.value();
}

void MockDisplayCaptureSourceGStreamer::applyFrameRateAndZoomWithPreset(double requestedFrameRate, double requestedZoom, std::optional<VideoPreset>&& preset)
{
    UNUSED_PARAM(requestedZoom);

    m_currentPreset = WTF::move(preset);
    if (!m_currentPreset)
        return;

    setIntrinsicSize(m_currentPreset->size());
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
