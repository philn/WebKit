/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Copyright (C) 2020 Igalia S.L.
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
#include "CaptureDevice.h"

#if ENABLE(VIDEO) && ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GStreamerCapturer.h"
#include "VideoFrameMetadataGStreamer.h"

#include <gst/app/gstappsink.h>
#include <mutex>
#include <wtf/HexNumber.h>
#include <wtf/MonotonicTime.h>
#include <wtf/PrintStream.h>

GST_DEBUG_CATEGORY(webkit_capturer_debug);
#define GST_CAT_DEFAULT webkit_capturer_debug

namespace WebCore {

static void initializeCapturerDebugCategory()
{
    ensureGStreamerInitialized();

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_capturer_debug, "webkitcapturer", 0, "WebKit Capturer");
    });
}

GStreamerCapturer::GStreamerCapturer(GStreamerCaptureDevice&& device, GRefPtr<GstCaps>&& caps)
    : m_caps(WTFMove(caps))
    , m_deviceType(device.type())
{
    initializeCapturerDebugCategory();
    m_device.emplace(WTFMove(device));
}

GStreamerCapturer::GStreamerCapturer(const PipeWireCaptureDevice& device)
    : m_deviceType(device.type())
{
    initializeCapturerDebugCategory();
    m_pipewireDevice.emplace(device);

    const char* mediaType = nullptr;
    switch(device.type()) {
    case CaptureDevice::DeviceType::Camera:
    case CaptureDevice::DeviceType::Screen:
    case CaptureDevice::DeviceType::Window:
        mediaType = "video/x-raw";
        break;
    case CaptureDevice::DeviceType::Microphone:
        mediaType = "audio/x-raw";
        break;
    default:
        break;
    }
    if (mediaType)
        m_caps = adoptGRef(gst_caps_new_empty_simple(mediaType));
}

GStreamerCapturer::~GStreamerCapturer()
{
    tearDown();
}

void GStreamerCapturer::tearDown(bool disconnectSignals)
{
    GST_DEBUG("Disposing capture pipeline %" GST_PTR_FORMAT " (disconnecting signals: %s)", m_pipeline.get(), boolForPrinting(disconnectSignals));
    if (disconnectSignals && m_sink)
        g_signal_handlers_disconnect_by_data(m_sink.get(), this);

    if (m_pipeline) {
        if (disconnectSignals) {
            unregisterPipeline(m_pipeline);
            disconnectSimpleBusMessageCallback(pipeline());
        }
        gst_element_set_state(pipeline(), GST_STATE_NULL);
    }

    if (!disconnectSignals)
        return;

    m_sink = nullptr;
    m_valve = nullptr;
    m_src = nullptr;
    m_capsfilter = nullptr;
    m_pipeline = nullptr;
}

GStreamerCapturerObserver::~GStreamerCapturerObserver()
{
}

void GStreamerCapturer::addObserver(GStreamerCapturerObserver& observer)
{
    ASSERT(isMainThread());
    m_observers.add(observer);
}

void GStreamerCapturer::removeObserver(GStreamerCapturerObserver& observer)
{
    ASSERT(isMainThread());
    m_observers.remove(observer);
}

void GStreamerCapturer::forEachObserver(const Function<void(GStreamerCapturerObserver&)>& apply)
{
    ASSERT(isMainThread());
    Ref protectedThis { *this };
    m_observers.forEach(apply);
}

GstElement* GStreamerCapturer::createSource()
{
    if (m_pipewireDevice) {
        m_src = makeElement("pipewiresrc");
        ASSERT(m_src);
        auto path = AtomString::number(m_pipewireDevice->objectId());
        // FIXME: The path property is deprecated in favor of target-object but the portal doesn't expose this object.
        g_object_set(m_src.get(), "path", path.string().ascii().data(), "fd", m_pipewireDevice->fd(), nullptr);
    } else {
        ASSERT(m_device);
        auto sourceName = makeString(WTF::span(name()), hex(reinterpret_cast<uintptr_t>(this)));
        m_src = gst_device_create_element(m_device->device(), sourceName.ascii().data());
        ASSERT(m_src);
        g_object_set(m_src.get(), "do-timestamp", TRUE, nullptr);
    }

    auto* factory = gst_element_get_factory(m_src.get());
    GST_DEBUG_OBJECT(m_pipeline.get(), "Source element created: %" GST_PTR_FORMAT, factory);
    if (g_str_equal(GST_OBJECT_NAME(factory), "pipewiresrc")) {
        auto srcPad = adoptGRef(gst_element_get_static_pad(m_src.get(), "src"));
        gst_pad_add_probe(srcPad.get(), GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, [](GstPad*, GstPadProbeInfo* info, void* userData) -> GstPadProbeReturn {
            auto* event = gst_pad_probe_info_get_event(info);
            if (GST_EVENT_TYPE(event) != GST_EVENT_CAPS)
                return GST_PAD_PROBE_OK;

            callOnMainThread([event, capturer = reinterpret_cast<GStreamerCapturer*>(userData)] {
                GstCaps* caps;
                gst_event_parse_caps(event, &caps);
                capturer->forEachObserver([caps](auto& observer) {
                    observer.sourceCapsChanged(caps);
                });
            });
            return GST_PAD_PROBE_OK;
        }, this, nullptr);
    }

    if (m_deviceType == CaptureDevice::DeviceType::Camera) {
        auto srcPad = adoptGRef(gst_element_get_static_pad(m_src.get(), "src"));
        gst_pad_add_probe(srcPad.get(), static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_PUSH | GST_PAD_PROBE_TYPE_BUFFER), [](GstPad*, GstPadProbeInfo* info, gpointer) -> GstPadProbeReturn {
            VideoFrameTimeMetadata metadata;
            metadata.captureTime = MonotonicTime::now().secondsSinceEpoch();
            auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
            auto* modifiedBuffer = webkitGstBufferSetVideoFrameTimeMetadata(buffer, std::make_optional<VideoFrameTimeMetadata>(metadata));
            gst_buffer_replace(&buffer, modifiedBuffer);
            return GST_PAD_PROBE_OK;
        }, nullptr, nullptr);
    }

    return m_src.get();
}

GRefPtr<GstCaps> GStreamerCapturer::caps()
{
    if (m_pipewireDevice)
        return m_pipewireDevice->caps();

    ASSERT(m_device);
    return adoptGRef(gst_device_get_caps(m_device->device()));
}

void GStreamerCapturer::setupPipeline()
{
    if (m_pipeline) {
        unregisterPipeline(m_pipeline);
        disconnectSimpleBusMessageCallback(pipeline());
    }

    m_pipeline = makeElement("pipeline");
    registerActivePipeline(m_pipeline);

    GRefPtr<GstElement> source = createSource();
    GRefPtr<GstElement> converter = createConverter();

    m_valve = makeElement("valve");
    m_capsfilter = makeElement("capsfilter");
    m_sink = makeElement("appsink");

    gst_util_set_object_arg(G_OBJECT(m_capsfilter.get()), "caps-change-mode", "delayed");

    gst_app_sink_set_emit_signals(GST_APP_SINK(m_sink.get()), TRUE);
    g_object_set(m_sink.get(), "enable-last-sample", FALSE, nullptr);
    g_object_set(m_capsfilter.get(), "caps", m_caps.get(), nullptr);

    auto* queue = gst_element_factory_make("queue", nullptr);
    gst_bin_add_many(GST_BIN(m_pipeline.get()), source.get(), converter.get(), m_capsfilter.get(), m_valve.get(), queue, m_sink.get(), nullptr);
    gst_element_link_many(source.get(), converter.get(), m_capsfilter.get(), m_valve.get(), queue, m_sink.get(), nullptr);

    connectSimpleBusMessageCallback(pipeline());
}

GstElement* GStreamerCapturer::makeElement(const char* factoryName)
{
    auto* element = makeGStreamerElement(factoryName, nullptr);
    auto elementName = makeString(span(name()), "_capturer_"_s, span(GST_OBJECT_NAME(element)), '_', hex(reinterpret_cast<uintptr_t>(this)));
    gst_object_set_name(GST_OBJECT(element), elementName.ascii().data());

    return element;
}

void GStreamerCapturer::start()
{
    if (!m_pipeline)
        setupPipeline();

    ASSERT(m_pipeline);
    GST_INFO_OBJECT(pipeline(), "Starting");
    gst_element_set_state(pipeline(), GST_STATE_PLAYING);
}

void GStreamerCapturer::stop()
{
    GST_INFO_OBJECT(pipeline(), "Stopping");
    tearDown(false);
}

bool GStreamerCapturer::isInterrupted() const
{
    gboolean isInterrupted;
    g_object_get(m_valve.get(), "drop", &isInterrupted, nullptr);
    return isInterrupted;
}

void GStreamerCapturer::setInterrupted(bool isInterrupted)
{
    g_object_set(m_valve.get(), "drop", isInterrupted, nullptr);
}

void GStreamerCapturer::stopDevice(bool disconnectSignals)
{
    forEachObserver([](auto& observer) {
        observer.captureEnded();
    });
    if (disconnectSignals) {
        m_device.reset();
        m_caps = nullptr;
    }
    tearDown(disconnectSignals);
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(VIDEO) && ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
