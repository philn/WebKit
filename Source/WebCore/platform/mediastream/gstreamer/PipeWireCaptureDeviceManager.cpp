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

GST_DEBUG_CATEGORY(webkit_pipewire_capture_device_manager_debug);
#define GST_CAT_DEFAULT webkit_pipewire_capture_device_manager_debug

RefPtr<PipeWireCaptureDeviceManager> PipeWireCaptureDeviceManager::create(CaptureDevice::DeviceType deviceType)
{
    return adoptRef(*new PipeWireCaptureDeviceManager(deviceType));
}

PipeWireCaptureDeviceManager::PipeWireCaptureDeviceManager(CaptureDevice::DeviceType deviceType)
    : m_deviceType(deviceType)
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_pipewire_capture_device_manager_debug, "webkitpipewirecapturedevicemanager", 0, "WebKit PipeWire Capture Device Manager");
    });
}

CaptureSourceOrError PipeWireCaptureDeviceManager::createCaptureSource(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints)
{
    GST_DEBUG("Creating capture source for device %s", device.persistentId().ascii().data());
    if (MockRealtimeMediaSourceCenter::mockRealtimeMediaSourceCenterEnabled())
        return GStreamerVideoCaptureSource::create(String { device.persistentId() }, WTFMove(hashSalts), constraints);

    // We don't support audio capture yet.
    RELEASE_ASSERT(m_deviceType == CaptureDevice::DeviceType::Camera);

    if (!m_portal)
        m_portal = DesktopPortalCamera::create();

    GST_DEBUG("Checking with Camera portal");
    if (!m_portal || !m_portal->isCameraPresent()) {
        GST_DEBUG("Portal not present or has no camera");
        return GStreamerVideoCaptureSource::create(String { device.persistentId() }, WTFMove(hashSalts), constraints);
    }

    if (!m_portal->accessCamera()) {
        GST_DEBUG("Camera access denied");
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });
    }

    bool useFirst = false;
    if (m_pipewireDevices.isEmpty()) {
        for (auto& nodeData : m_portal->openCameraPipewireRemote()) {
            //CaptureDevice device2(nodeData.persistentId, m_deviceType, nodeData.label);
            m_pipewireDevices.ensure(nodeData.persistentId, [&] {
                return makeUnique<PipeWireCaptureDevice>(nodeData, nodeData.persistentId, m_deviceType, nodeData.label);
            });
            // if (!deviceWasAdded)
            //     continue;

            // device.setEnabled(true);
            // devices.append(WTFMove(device));
        }
        useFirst = true;
    }

    gst_printerrln("useFirst: %d %s", useFirst, device.persistentId().ascii().data());
    if (useFirst) {
        const auto it = m_pipewireDevices.begin();
        auto& pipewireCaptureDevice = it->value;
        return GStreamerVideoCaptureSource::createPipewireSource(*pipewireCaptureDevice, WTFMove(hashSalts), constraints);
    }
    // FIXME: This is broken because it implies the pipewiredeviceprovider is available, which won't
    // be the case in sandboxed WebProcess...
    for (auto& [persistentId, pipewireCaptureDevice] : m_pipewireDevices) {
        gst_printerrln(">%s< >%s<", device.persistentId().ascii().data(), pipewireCaptureDevice->label().ascii().data());
        if (device.persistentId() == pipewireCaptureDevice->label())
            return GStreamerVideoCaptureSource::createPipewireSource(*pipewireCaptureDevice, WTFMove(hashSalts), constraints);
    }
    // const auto it = m_pipewireDevices.find(device.persistentId());
    // if (it == m_pipewireDevices.end())
        return CaptureSourceOrError({ { }, MediaAccessDenialReason::PermissionDenied });

    // auto& pipewireCaptureDevice = it->value;
    // return GStreamerVideoCaptureSource::createPipewireSource(*pipewireCaptureDevice, WTFMove(hashSalts), constraints);
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
