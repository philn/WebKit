/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Author: Thibault Saunier <tsaunier@igalia.com>
 * Author: Alejandro G. Castro <alex@igalia.com>
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

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "GStreamerCaptureDeviceManager.h"

#include "DesktopPortal.h"
#include "GStreamerCommon.h"
#include "GStreamerVideoCaptureSource.h"
#include "MockRealtimeMediaSourceCenter.h"
#include "PipeWireCaptureDevice.h"
#include <wtf/Scope.h>

namespace WebCore {

class GStreamerVideoCaptureSourceFactory final : public VideoCaptureFactory {
public:
    CaptureSourceOrError createVideoCaptureSource(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints, PageIdentifier) final
    {
        auto& manager = GStreamerVideoCaptureDeviceManager::singleton();
        return manager.createVideoCaptureSource(device, WTFMove(hashSalts), constraints);
    }

private:
    CaptureDeviceManager& videoCaptureDeviceManager() final { return GStreamerVideoCaptureDeviceManager::singleton(); }
};

GStreamerVideoCaptureDeviceManager& GStreamerVideoCaptureDeviceManager::singleton()
{
    static NeverDestroyed<GStreamerVideoCaptureDeviceManager> manager;
    return manager;
}

void GStreamerVideoCaptureDeviceManager::computeCaptureDevices(CompletionHandler<void()>&& callback)
{
    auto scopeExit = makeScopeExit([&] {
        callback();
    });
    if (MockRealtimeMediaSourceCenter::mockRealtimeMediaSourceCenterEnabled())
        return;

    if (!m_portal)
        m_portal = DesktopPortalCamera::create();

    if (!m_portal || !m_portal->isCameraPresent())
        return;

    if (!m_portal->accessCamera())
        return;

    for (auto& nodeData : m_portal->openCameraPipewireRemote()) {
        CaptureDevice device(nodeData.persistentId, CaptureDevice::DeviceType::Camera, nodeData.label);
        auto deviceWasAdded = m_pipewireDevices.ensure(device.persistentId(), [&] {
            return makeUnique<PipeWireCaptureDevice>(nodeData, device.persistentId(), device.type(), device.label(), device.groupId());
        });
        if (!deviceWasAdded)
            continue;

        device.setEnabled(true);
        m_devices.append(WTFMove(device));
    }
}

CaptureSourceOrError GStreamerVideoCaptureDeviceManager::createVideoCaptureSource(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints)
{
    if (!m_portal)
        return GStreamerVideoCaptureSource::create(String { device.persistentId() }, WTFMove(hashSalts), constraints);

    const auto it = m_pipewireDevices.find(device.persistentId());
    if (it == m_pipewireDevices.end())
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });

    auto& pipewireCaptureDevice = it->value;
    return GStreamerVideoCaptureSource::createPipewireSource(*pipewireCaptureDevice, WTFMove(hashSalts), constraints);
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
