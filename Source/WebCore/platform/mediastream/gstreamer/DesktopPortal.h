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

#include "PipeWireNodeData.h"
#include <gio/gio.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Forward.h>
#include <wtf/RefCounted.h>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class DesktopPortal : public RefCounted<DesktopPortal> {
    WTF_MAKE_FAST_ALLOCATED;
public:
    DesktopPortal(const String&, GRefPtr<GDBusProxy>&&);

    GRefPtr<GVariant> getProperty(const char* name);

    using ResponseCallback = CompletionHandler<void(GVariant*)>;
    void waitResponseSignal(const char* objectPath, ResponseCallback&& = [](auto*) { });

    void notifyResponse(GVariant* parameters) { m_currentResponseCallback(parameters); }

protected:
    String m_interfaceName;
    GRefPtr<GDBusProxy> m_proxy;
    ResponseCallback m_currentResponseCallback;
};

class DesktopPortalCamera : public DesktopPortal {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static RefPtr<DesktopPortalCamera> create();

    bool isCameraPresent();
    bool accessCamera();
    std::optional<int> openCameraPipewireRemote();

private:
    DesktopPortalCamera(const String&, GRefPtr<GDBusProxy>&&);
    HashMap<String, std::optional<bool>> m_cameraAccessResults;
};

class DesktopPortalScreenCast : public DesktopPortal {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static RefPtr<DesktopPortalScreenCast> create();

    class ScreencastSession {
    public:
        ScreencastSession(String&& path, const GRefPtr<GDBusProxy>& proxy)
            : m_path(WTFMove(path))
            , m_proxy(proxy)
        {
        }
        const String& path() const { return m_path; }
        GRefPtr<GVariant> selectSources(GVariantBuilder&);
        GRefPtr<GVariant> start();
        std::optional<PipeWireNodeData> openPipewireRemote();

    private:
        String m_path;
        const GRefPtr<GDBusProxy>& m_proxy;
    };

    std::optional<ScreencastSession> createScreencastSession();
    void closeSession(const String& path);

private:
    DesktopPortalScreenCast(const String&, GRefPtr<GDBusProxy>&&);
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
