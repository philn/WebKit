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

#pragma once

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include <wtf/CompletionHandler.h>
#include <wtf/Forward.h>
#include <wtf/RefCounted.h>

#include "DesktopPortal.h"
#include "MediaConstraints.h"
#include "MediaDeviceHashSalts.h"
#include "PipeWireCaptureDevice.h"
#include "RealtimeMediaSource.h"

namespace WebCore {

class PipeWireCaptureDeviceManager : public RefCounted<PipeWireCaptureDeviceManager> {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static RefPtr<PipeWireCaptureDeviceManager> create(CaptureDevice::DeviceType);
    PipeWireCaptureDeviceManager(CaptureDevice::DeviceType);

    CaptureSourceOrError createCaptureSource(const CaptureDevice&, MediaDeviceHashSalts&&, const MediaConstraints*);

private:
    CaptureDevice::DeviceType m_deviceType;
    RefPtr<DesktopPortalCamera> m_portal;
    GRefPtr<GstDeviceProvider> m_pipewireDeviceProvider;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
