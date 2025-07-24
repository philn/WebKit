
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
#if USE(LIBNICE)
    : GStreamerIceBackendNice()
#endif
    , m_connection(connection)
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

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
