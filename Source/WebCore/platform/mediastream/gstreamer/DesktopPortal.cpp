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
#include <wtf/glib/GUniquePtr.h>
#include <gio/gunixfdlist.h>
#include <wtf/UUID.h>
#include <wtf/WeakRandomNumber.h>

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

std::optional<int> DesktopPortal::openCameraPipewireRemote()
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
        return {};
    }

    int fdOut;
    g_variant_get(result.get(), "(h)", &fdOut);
    fd = g_unix_fd_list_get(fdList.get(), fdOut, nullptr);
    return fd;
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
