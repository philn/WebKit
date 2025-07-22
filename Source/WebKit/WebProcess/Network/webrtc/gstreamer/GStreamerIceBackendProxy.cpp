
#include "config.h"
#include "GStreamerIceBackendProxy.h"

#if USE(GSTREAMER_WEBRTC)

#include "NetworkConnectionToWebProcessMessages.h"
#include "NetworkProcessConnection.h"
#include "GStreamerIceBackendProxyMessages.h"

namespace WebKit {
using namespace WebCore;

Ref<GStreamerIceBackendProxy> GStreamerIceBackendProxy::create(WebPageProxyIdentifier webPageProxyID)
{
    return adoptRef(*new GStreamerIceBackendProxy(webPageProxyID));
}

GStreamerIceBackendProxy::GStreamerIceBackendProxy(WebPageProxyIdentifier webPageProxyID)
    : GStreamerIceBackend()
    , m_webPageProxyID(webPageProxyID)
{
    gst_printerrln("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
}

GStreamerIceBackendProxy::~GStreamerIceBackendProxy() = default;

IPC::Connection* GStreamerIceBackendProxy::messageSenderConnection() const
{
    return &WebProcess::singleton().ensureNetworkProcessConnection().connection();
}

uint64_t GStreamerIceBackendProxy::messageSenderDestinationID() const
{
    return identifier().toUInt64();
}

std::optional<SharedPreferencesForWebProcess> GStreamerIceBackendProxy::sharedPreferencesForWebProcess(const IPC::Connection&) const
{
    //return WebProcess::singleton().
    return std::nullopt;
}

void GStreamerIceBackendProxy::addTurnServer(const String& uri)
{
    //MessageSender::send(Messages::GStreamerIceBackendProxy::AddTurnServer { uri }, identifier(), m_webPageProxyID);
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
