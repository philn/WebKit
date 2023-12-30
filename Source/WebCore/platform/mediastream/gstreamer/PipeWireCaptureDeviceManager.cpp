/*
 * Copyright (C) 2024 Igalia S.L
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
#include "PipeWireCaptureDeviceManager.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GStreamerVideoCaptureSource.h"
#include "MockRealtimeMediaSourceCenter.h"
#include <wtf/Scope.h>

namespace WebCore {

RefPtr<PipeWireCaptureDeviceManager> PipeWireCaptureDeviceManager::create(CaptureDevice::DeviceType deviceType)
{
    return adoptRef(*new PipeWireCaptureDeviceManager(deviceType));
}

PipeWireCaptureDeviceManager::PipeWireCaptureDeviceManager(CaptureDevice::DeviceType deviceType)
    : m_deviceType(deviceType)
{
}

Vector<CaptureDevice> PipeWireCaptureDeviceManager::computeCaptureDevices(CompletionHandler<void()>&& callback)
{
    auto scopeExit = makeScopeExit([&] {
        callback();
    });
    Vector<CaptureDevice> devices;
    if (MockRealtimeMediaSourceCenter::mockRealtimeMediaSourceCenterEnabled())
        return devices;

    if (!m_portal)
        m_portal = DesktopPortalCamera::create();

    if (!m_portal || !m_portal->isCameraPresent())
        return devices;

    // We don't support audio capture yet.
    RELEASE_ASSERT(m_deviceType == CaptureDevice::DeviceType::Camera);

    if (!m_portal->accessCamera())
        return devices;

    for (auto& nodeData : m_portal->openCameraPipewireRemote()) {
        CaptureDevice device(nodeData.persistentId, m_deviceType, nodeData.label);
        auto deviceWasAdded = m_pipewireDevices.ensure(device.persistentId(), [&] {
            return makeUnique<PipeWireCaptureDevice>(nodeData, device.persistentId(), device.type(), device.label(), device.groupId());
        });
        if (!deviceWasAdded)
            continue;

        device.setEnabled(true);
        devices.append(WTFMove(device));
    }
    return devices;
}

CaptureSourceOrError PipeWireCaptureDeviceManager::createCaptureSource(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints)
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
