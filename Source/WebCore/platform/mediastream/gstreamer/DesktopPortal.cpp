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

#include "config.h"

#include "DesktopPortal.h"
#include "wtf/Compiler.h"
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <pipewire/context.h>
#include <pipewire/main-loop.h>
#include <pipewire/thread-loop.h>
#include <spa/utils/dict.h>
#include <spa/utils/result.h>
#include <unistd.h>
#include <wtf/Scope.h>
#include <wtf/UUID.h>
#include <wtf/WeakRandomNumber.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/glib/WTFGType.h>

namespace WebCore {

static const Seconds s_dbusCallTimeout = 10_ms;

RefPtr<DesktopPortal> DesktopPortal::create(const String& interfaceName)
{
    GUniqueOutPtr<GError> error;
    auto proxy = adoptGRef(g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
        static_cast<GDBusProxyFlags>(G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES), nullptr,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", interfaceName.ascii().data(), nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("Unable to connect to the Deskop portal: %s", error->message);
        return nullptr;
    }

    return adoptRef(*new DesktopPortal(interfaceName, WTFMove(proxy)));
}

DesktopPortal::DesktopPortal(const String& interfaceName, GRefPtr<GDBusProxy>&& proxy)
    : m_interfaceName(interfaceName)
    , m_proxy(WTFMove(proxy))
{
}

DesktopPortal::Session::Session(String&& path, const GRefPtr<GDBusProxy>& proxy)
    : m_path(WTFMove(path))
    , m_proxy(proxy)
{
}

GRefPtr<GVariant> DesktopPortal::ScreencastSession::selectSources(GVariantBuilder& options)
{
    auto token = makeString("WebKit", weakRandomNumber<uint32_t>());
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.ascii().data()));

    GUniqueOutPtr<GError> error;
    auto result = adoptGRef(g_dbus_proxy_call_sync(m_proxy.get(), "SelectSources",
        g_variant_new("(oa{sv})", m_path.ascii().data(), &options), G_DBUS_CALL_FLAGS_NONE, s_dbusCallTimeout.millisecondsAs<int>(), nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("SelectSources error: %s", error->message);
        return nullptr;
    }

    return result;
}

GRefPtr<GVariant> DesktopPortal::ScreencastSession::start()
{
    auto token = makeString("WebKit", weakRandomNumber<uint32_t>());
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.ascii().data()));

    GUniqueOutPtr<GError> error;
    auto result = adoptGRef(g_dbus_proxy_call_sync(m_proxy.get(), "Start",
        g_variant_new("(osa{sv})", m_path.ascii().data(), "", &options), G_DBUS_CALL_FLAGS_NONE, s_dbusCallTimeout.millisecondsAs<int>(), nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("Start error: %s", error->message);
        return { };
    }
    return result;
}

std::optional<int> DesktopPortal::ScreencastSession::openPipewireRemote()
{
    GRefPtr<GUnixFDList> fdList;
    int fd = -1;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUniqueOutPtr<GError> error;
    auto result = adoptGRef(g_dbus_proxy_call_with_unix_fd_list_sync(m_proxy.get(), "OpenPipeWireRemote",
        g_variant_new("(oa{sv})", m_path.ascii().data(), &options), G_DBUS_CALL_FLAGS_NONE, s_dbusCallTimeout.millisecondsAs<int>(), nullptr, &fdList.outPtr(), nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("Unable to open pipewire remote. Error: %s", error->message);
        return { };
    }

    int fdOut;
    g_variant_get(result.get(), "(h)", &fdOut);
    fd = g_unix_fd_list_get(fdList.get(), fdOut, nullptr);
    return fd;
}

GRefPtr<GVariant> DesktopPortal::accessCamera()
{
    auto token = makeString("WebKit", weakRandomNumber<uint32_t>());
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.ascii().data()));

    GUniqueOutPtr<GError> error;
    auto result = adoptGRef(g_dbus_proxy_call_sync(m_proxy.get(), "AccessCamera",
        g_variant_new("(a{sv})", &options), G_DBUS_CALL_FLAGS_NONE, s_dbusCallTimeout.millisecondsAs<int>(), nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("AccessCamera error: %s", error->message);
        return { };
    }

    GUniqueOutPtr<char> objectPath;
    g_variant_get(result.get(), "(o)", &objectPath.outPtr());
    waitResponseSignal(objectPath.get());

    return result;
}

std::optional<std::pair<uint32_t, int>> DesktopPortal::openCameraPipewireRemote()
{
    GRefPtr<GUnixFDList> fdList;
    int fd = -1;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUniqueOutPtr<GError> error;
    auto result = adoptGRef(g_dbus_proxy_call_with_unix_fd_list_sync(m_proxy.get(), "OpenPipeWireRemote",
        g_variant_new("(a{sv})", &options), G_DBUS_CALL_FLAGS_NONE, s_dbusCallTimeout.millisecondsAs<int>(), nullptr, &fdList.outPtr(), nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("Unable to open pipewire remote. Error: %s", error->message);
        return { };
    }

    int fdOut;
    g_variant_get(result.get(), "(h)", &fdOut);
    fd = g_unix_fd_list_get(fdList.get(), fdOut, nullptr);

    // TODO: get node ID, see also https://github.com/bilelmoussaoui/ashpd/blob/master/src/desktop/camera.rs#L184

    pw_init(nullptr, nullptr);

    m_pipewireCore = createPipeWireCore();
    auto scopeExit = makeScopeExit([&] {
        destroyPipeWireCore(m_pipewireCore);
    });
    m_pipewireCore->fd = fd;
    m_pipewireCore->loop = pw_thread_loop_new("pipewire-main-loop", nullptr);
    m_pipewireCore->context = pw_context_new(pw_thread_loop_get_loop(m_pipewireCore->loop), nullptr, 0);
    m_pipewireCore->lastSeq = -1;
    m_pipewireCore->lastError = 0;

    if (pw_thread_loop_start(m_pipewireCore->loop) < 0) {
        WTFLogAlways("Unable to start PipeWire context loop");
        return { };
    }

    pw_thread_loop_lock(m_pipewireCore->loop);

    m_pipewireCore->core = pw_context_connect_fd(m_pipewireCore->context, fcntl(fd, F_DUPFD_CLOEXEC, 3), nullptr, 0);
    if (!m_pipewireCore->core) {
        WTFLogAlways("Unable to connect to PipeWire");
        pw_thread_loop_unlock(m_pipewireCore->loop);
        return { };
    }

    static const struct pw_core_events coreEvents = {
        PW_VERSION_CORE_EVENTS,
        nullptr,
        [](void* data, uint32_t id, int seq) {
            auto* portal = reinterpret_cast<DesktopPortal*>(data);
            auto* core = portal->m_pipewireCore;
            WTFLogAlways("Stopping! 0");
            if (id != PW_ID_CORE)
                return;
            WTFLogAlways("Stopping! 1");
            if (seq == portal->m_seq) {
                WTFLogAlways("Stopping!");
                portal->m_loopDone = true;
                pw_thread_loop_stop(core->loop);
                return;
            }
            WTFLogAlways("Stopping! 2");
            core->lastSeq = seq;
            pw_thread_loop_signal(core->loop, FALSE);
        },
        nullptr,
        [](void* data, uint32_t id, int seq, int res, const char* message) {
            auto* portal = reinterpret_cast<DesktopPortal*>(data);
            auto* core = portal->m_pipewireCore;

            pw_log_warn("error id:%u seq:%d res:%d (%s): %s", id, seq, res, spa_strerror(res), message);
            if (id == PW_ID_CORE)
                core->lastError = res;

            pw_thread_loop_signal(core->loop, FALSE);
        },
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    };

    {
        IGNORE_WARNINGS_BEGIN("unused-value")
        pw_core_add_listener(m_pipewireCore->core, &m_pipewireCore->coreListener, &coreEvents, this);
        IGNORE_WARNINGS_END
    }

    m_registry = pw_core_get_registry(m_pipewireCore->core, PW_VERSION_REGISTRY, 0);

    static const struct pw_registry_events registryEvents = {
        PW_VERSION_REGISTRY_EVENTS,
        [](void* data, uint32_t nodeId, uint32_t permissions, const char* type, uint32_t version, const struct spa_dict* properties) {
            UNUSED_PARAM(permissions);
            UNUSED_PARAM(type);
            UNUSED_PARAM(version);
            if (!properties)
                return;

            const char* role = spa_dict_lookup(properties, PW_KEY_MEDIA_ROLE);
            WTFLogAlways("role: %s", role);
            if (!role || !g_str_equal(role, "Camera"))
                return;

            // TODO: notify nodeId (converted to string) to DesktopPortal
            auto* portal = reinterpret_cast<DesktopPortal*>(data);
            portal->m_nodeId = nodeId;
        },
        [](void*, uint32_t) { }
    };

    {
        IGNORE_WARNINGS_BEGIN("unused-value")
        pw_registry_add_listener(m_registry, &m_registryListener, &registryEvents, this);
        IGNORE_WARNINGS_END
    }

    pw_thread_loop_unlock(m_pipewireCore->loop);

    {
        IGNORE_WARNINGS_BEGIN("unused-value")
        m_seq = pw_core_sync(m_pipewireCore->core, PW_ID_CORE, m_seq);
        IGNORE_WARNINGS_END
    }

    if (!m_seq) {
        WTFLogAlways("PipeWire sync failed");
        return { };
    }

    m_loopDone = false;
    WTFLogAlways("Pipewire loop starting");
    struct timespec abstime;
    m_pipewireCore->pendingSeq = pw_core_sync(m_pipewireCore->core, 0, m_pipewireCore->pendingSeq);
    pw_thread_loop_get_time(m_pipewireCore->loop, &abstime,
        30 * SPA_NSEC_PER_SEC);

    auto sync = [&] {
        while (true) {
            if (m_pipewireCore->lastSeq == m_pipewireCore->pendingSeq || m_pipewireCore->lastError < 0)
                break;
            if (pw_thread_loop_timed_wait_full(m_pipewireCore->loop, &abstime) < 0)
                break;
        }
    };
    for (;;) {
        if (m_loopDone)
            break;
        // pw_thread_loop_lock(m_pipewireCore->loop);
        //pw_thread_loop_wait(m_pipewireCore->loop);
        sync();
        // pw_thread_loop_unlock(m_pipewireCore->loop);
    }
    WTFLogAlways("Pipewire loop ended node ID: %u", m_nodeId);
    return { { m_nodeId, fd } };
}

std::optional<DesktopPortal::ScreencastSession> DesktopPortal::createScreencastSession()
{
    auto token = makeString("WebKit", weakRandomNumber<uint32_t>());
    auto sessionToken = makeString("WebKit", weakRandomNumber<uint32_t>());
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.ascii().data()));
    g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(sessionToken.ascii().data()));

    GUniqueOutPtr<GError> error;
    auto result = adoptGRef(g_dbus_proxy_call_sync(m_proxy.get(), "CreateSession", g_variant_new("(a{sv})", &options),
        G_DBUS_CALL_FLAGS_NONE, s_dbusCallTimeout.millisecondsAs<int>(), nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("Unable to create a Deskop portal session: %s", error->message);
        return { };
    }

    GUniqueOutPtr<char> objectPath;
    g_variant_get(result.get(), "(o)", &objectPath.outPtr());
    waitResponseSignal(objectPath.get());

    auto requestPath = String::fromLatin1(objectPath.get());
    auto sessionPath = makeStringByReplacingAll(requestPath, "/request/"_s, "/session/"_s);
    sessionPath = makeStringByReplacingAll(sessionPath, token, sessionToken);
    // if (m_interfaceName == "org.freedesktop.portal.ScreenCast"_s)
        return { ScreencastSession { WTFMove(sessionPath), m_proxy } };
    // if (m_interfaceName == "org.freedesktop.portal.Camera"_s)
    //     return { CameraSession { WTFMove(sessionPath), m_proxy } };

    // return { Session { WTFMove(sessionPath), m_proxy } };
}

GRefPtr<GVariant> DesktopPortal::getProperty(const char* name)
{
    auto propertiesResult = adoptGRef(g_dbus_proxy_call_sync(m_proxy.get(), "org.freedesktop.DBus.Properties.Get",
        g_variant_new("(ss)", m_interfaceName.ascii().data(), name), G_DBUS_CALL_FLAGS_NONE,
        s_dbusCallTimeout.millisecondsAs<int>(), nullptr, nullptr));
    if (propertiesResult) {
        GRefPtr<GVariant> property;
        g_variant_get(propertiesResult.get(), "(v)", &property.outPtr());
        return property;
    }
    return nullptr;
}

void DesktopPortal::closeSession(const String& path)
{
    GUniqueOutPtr<GError> error;
    auto proxy = adoptGRef(g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
        static_cast<GDBusProxyFlags>(G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES), nullptr,
        "org.freedesktop.portal.Desktop", path.ascii().data(), "org.freedesktop.portal.Session", nullptr, &error.outPtr()));
    if (error) {
        WTFLogAlways("Unable to connect to the Deskop portal: %s", error->message);
        return;
    }
    auto dbusCallTimeout = 100_ms;
    auto result = adoptGRef(g_dbus_proxy_call_sync(proxy.get(), "Close", nullptr, G_DBUS_CALL_FLAGS_NONE,
        dbusCallTimeout.millisecondsAs<int>(), nullptr, &error.outPtr()));
    if (error)
        WTFLogAlways("Portal session could not be closed: %s", error->message);
}

void DesktopPortal::waitResponseSignal(const char* objectPath, ResponseCallback&& callback)
{
    RELEASE_ASSERT(!m_currentResponseCallback);
    m_currentResponseCallback = WTFMove(callback);
    auto* connection = g_dbus_proxy_get_connection(m_proxy.get());
    auto signalId = g_dbus_connection_signal_subscribe(connection, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
        "Response", objectPath, nullptr, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, reinterpret_cast<GDBusSignalCallback>(+[](GDBusConnection*, const char* /* senderName */, const char* /* objectPath */, const char* /* interfaceName */, const char* /* signalName */, GVariant* parameters, gpointer userData) {
            auto& self = *reinterpret_cast<DesktopPortal*>(userData);
            self.notifyResponse(parameters);
        }), this, nullptr);

    while (m_currentResponseCallback)
        g_main_context_iteration(nullptr, false);

    g_dbus_connection_signal_unsubscribe(connection, signalId);
}

} // namespace WebCore
