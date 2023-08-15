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

#include <gio/gio.h>
#include <pipewire/pipewire.h>
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
    static RefPtr<DesktopPortal> create(const String& interfaceName);
    DesktopPortal(const String&, GRefPtr<GDBusProxy>&&);

    class Session {
    public:
        Session(String&& path, const GRefPtr<GDBusProxy>&);

        const String& path() const { return m_path; }

        GRefPtr<GVariant> accessCamera();

    protected:
        String m_path;
        const GRefPtr<GDBusProxy>& m_proxy;
    };

    class ScreencastSession : public Session {
    public:
        ScreencastSession(String&& path, const GRefPtr<GDBusProxy>& proxy)
            : Session(WTFMove(path), proxy)
        {
        }
        GRefPtr<GVariant> selectSources(GVariantBuilder&);
        GRefPtr<GVariant> start();
        std::optional<int> openPipewireRemote();
    };

    std::optional<ScreencastSession> createScreencastSession();
    void closeSession(const String& path);

    GRefPtr<GVariant> accessCamera();
    std::optional<std::pair<uint32_t, int>> openCameraPipewireRemote();

    GRefPtr<GVariant> getProperty(const char* name);

    using ResponseCallback = CompletionHandler<void(GVariant*)>;
    void waitResponseSignal(const char* objectPath, ResponseCallback&& = [](GVariant*) {});

protected:
    void notifyResponse(GVariant* parameters) { m_currentResponseCallback(parameters); }

private:
    String m_interfaceName;
    GRefPtr<GDBusProxy> m_proxy;
    ResponseCallback m_currentResponseCallback;

    struct PipeWireCore {
        ~PipeWireCore()
        {
            WTFLogAlways("Closing pipewire things");
            pw_context_destroy(context);
            pw_thread_loop_destroy(loop);
        }
        int fd;
        struct pw_thread_loop* loop;
        struct pw_context* context;
        struct pw_core* core;
        struct spa_hook coreListener;
        int lastSeq;
        int pendingSeq;
        int lastError;
    };
    WEBKIT_DEFINE_ASYNC_DATA_STRUCT(PipeWireCore)

    struct PipeWireCore* m_pipewireCore { nullptr };
    struct pw_registry* m_registry { nullptr };
    struct spa_hook m_registryListener;
    int m_seq { 0 };
    bool m_loopDone { false };
    uint32_t m_nodeId;
};

} // namespace WebCore
