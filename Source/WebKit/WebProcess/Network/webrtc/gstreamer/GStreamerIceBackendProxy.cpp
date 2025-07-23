
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

void GStreamerIceBackendProxy::setForceRelay(bool forceRelay)
{
    gst_printerrln("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
    MessageSender::send(Messages::GStreamerIceBackendProxy::SetForceRelay { forceRelay });
    gst_printerrln("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
}

void GStreamerIceBackendProxy::addTurnServer(const String& uri)
{
    gst_printerrln("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
    MessageSender::send(Messages::GStreamerIceBackendProxy::AddTurnServer { uri });
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
