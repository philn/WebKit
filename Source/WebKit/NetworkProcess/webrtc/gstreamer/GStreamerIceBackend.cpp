
#include "config.h"
#include "GStreamerIceBackend.h"

#if USE(GSTREAMER_WEBRTC)

#include "NetworkConnectionToWebProcess.h"
// #include "GStreamerIceBackendMessages.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {
using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(GStreamerIceBackend);

void GStreamerIceBackend::initialize(NetworkConnectionToWebProcess& connectionToWebProcess, WebKit::WebPageProxyIdentifier&&, CompletionHandler<void(RefPtr<GStreamerIceBackend>&&)>&& completionHandler)
{
    Ref backend = GStreamerIceBackend::create(connectionToWebProcess);
    completionHandler(WTFMove(backend));
}

GStreamerIceBackend::GStreamerIceBackend(NetworkConnectionToWebProcess& connection)
    : m_connection(connection)
{
}

GStreamerIceBackend::~GStreamerIceBackend() = default;

IPC::Connection* GStreamerIceBackend::messageSenderConnection() const
{
    return m_connection ? &m_connection->connection() : nullptr;
}

uint64_t GStreamerIceBackend::messageSenderDestinationID() const
{
    return identifier().toUInt64();
}

std::optional<SharedPreferencesForWebProcess> GStreamerIceBackend::sharedPreferencesForWebProcess() const
{
    if (auto connectionToWebProcess = m_connection.get())
        return connectionToWebProcess->sharedPreferencesForWebProcess();

    return std::nullopt;
}

void GStreamerIceBackend::setForceRelay(bool forceRelay)
{
    WTFLogAlways("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
}

void GStreamerIceBackend::addTurnServer(const String& uri)
{
    WTFLogAlways("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
