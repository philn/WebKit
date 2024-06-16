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

#pragma once

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "DisplayCaptureManager.h"
#include "DesktopPortal.h"
#include "GRefPtrGStreamer.h"
#include "GStreamerCaptureDevice.h"
#include "GStreamerCapturer.h"
#include "GStreamerVideoCapturer.h"
#include "PipeWireCaptureDevice.h"
#include "RealtimeMediaSourceCenter.h"
#include "RealtimeMediaSourceFactory.h"
#include "PipeWireCaptureDeviceManager.h"

namespace WebCore {

void teardownGStreamerCaptureDeviceManagers();

class GStreamerCaptureDeviceManager : public CaptureDeviceManager, public RealtimeMediaSourceCenterObserver {
    WTF_MAKE_NONCOPYABLE(GStreamerCaptureDeviceManager)
public:
    GStreamerCaptureDeviceManager();
    ~GStreamerCaptureDeviceManager();
    std::optional<GStreamerCaptureDevice> gstreamerDeviceWithUID(const String&);

    const Vector<CaptureDevice>& captureDevices() final;
    virtual CaptureDevice::DeviceType deviceType() = 0;

    // RealtimeMediaSourceCenterObserver interface.
    void devicesChanged() final;
    void deviceWillBeRemoved(const String& persistentId) final;

    void registerCapturer(const RefPtr<GStreamerCapturer>&);
    void unregisterCapturer(const GStreamerCapturer&);
    void stopCapturing(const String& persistentId);

    void addDevice(GRefPtr<GstDevice>&&);

    void teardown();

protected:
    Vector<CaptureDevice> m_devices;

private:
    void removeDevice(GRefPtr<GstDevice>&&);
    void stopMonitor();
    void refreshCaptureDevices();

    GRefPtr<GstDeviceMonitor> m_deviceMonitor;
    Vector<GStreamerCaptureDevice> m_gstreamerDevices;
    Vector<RefPtr<GStreamerCapturer>> m_capturers;
    bool m_isTearingDown { false };
};

class GStreamerAudioCaptureDeviceManager final : public GStreamerCaptureDeviceManager {
    friend class NeverDestroyed<GStreamerAudioCaptureDeviceManager>;
public:
    static GStreamerAudioCaptureDeviceManager& singleton();
    CaptureDevice::DeviceType deviceType() final { return CaptureDevice::DeviceType::Microphone; }
};

class GStreamerVideoCaptureDeviceManager final : public GStreamerCaptureDeviceManager {
    friend class NeverDestroyed<GStreamerVideoCaptureDeviceManager>;
public:
    static GStreamerVideoCaptureDeviceManager& singleton();
    CaptureDevice::DeviceType deviceType() final { return CaptureDevice::DeviceType::Camera; }
    CaptureSourceOrError createVideoCaptureSource(const CaptureDevice&, MediaDeviceHashSalts&&, const MediaConstraints*);

private:
    GStreamerVideoCaptureDeviceManager();
    RefPtr<PipeWireCaptureDeviceManager> m_pipewireCaptureDeviceManager;
};

class GStreamerDisplayCaptureDeviceManager final : public DisplayCaptureManager {
    friend class NeverDestroyed<GStreamerDisplayCaptureDeviceManager>;
public:
    static GStreamerDisplayCaptureDeviceManager& singleton();
    const Vector<CaptureDevice>& captureDevices() final { return m_devices; };
    void computeCaptureDevices(CompletionHandler<void()>&&) final;
    CaptureSourceOrError createDisplayCaptureSource(const CaptureDevice&, MediaDeviceHashSalts&&, const MediaConstraints*);

    enum PipeWireOutputType {
        Monitor = 1 << 0,
        Window = 1 << 1
    };

    void stopSource(const String& persistentID);

    // DisplayCaptureManager interface
    bool requiresCaptureDevicesEnumeration() const final { return true; }

private:
    GStreamerDisplayCaptureDeviceManager();
    ~GStreamerDisplayCaptureDeviceManager();

    Vector<CaptureDevice> m_devices;

    HashMap<String, std::unique_ptr<PipeWireNodeData>> m_sessions;
    RefPtr<DesktopPortalScreenCast> m_portal;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
