/*
 * Copyright (C) 2023 Metrological Group B.V.
 * Author: Philippe Normand <philn@igalia.com>
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

#include "GStreamerCaptureDevice.h"
#include <gst/app/gstappsrc.h>

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

using namespace WebCore;

void GStreamerCaptureDevice::setMockSource(GRefPtr<GstElement>&& element)
{
    m_mockSource = WTFMove(element);
}

void GStreamerCaptureDevice::pushMockSample(const GRefPtr<GstSample>& sample)
{
    if (!m_mockSource)
        return;

    gst_app_src_push_sample(GST_APP_SRC_CAST(m_mockSource.get()), sample.get());
}

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
