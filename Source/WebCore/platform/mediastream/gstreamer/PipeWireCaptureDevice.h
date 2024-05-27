/*
 * Copyright (C) 2023 Metrological Group B.V.
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

#include "CaptureDevice.h"
#include "PipeWireSession.h"

namespace WebCore {

class PipeWireCaptureDevice : public CaptureDevice {
    WTF_MAKE_FAST_ALLOCATED;

public:
    PipeWireCaptureDevice(PipeWireNodeData& nodeData, const String& persistentId, DeviceType type, const String& label, const String& groupId = emptyString())
        : CaptureDevice(persistentId, type, label, groupId)
        , m_nodeData(nodeData)
    {
    }

    uint32_t objectId() const { return m_nodeData.objectId; }
    int fd() const { return m_nodeData.fd; }
    const GRefPtr<GstCaps>& caps() const { return m_nodeData.caps; }

private:
    PipeWireNodeData m_nodeData;
};

}

#endif // ENABLE(MEDIA_STREAM)  && USE(GSTREAMER)
