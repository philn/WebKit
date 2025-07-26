
#include "config.h"
#include "GStreamerIceBackendProxy.h"

#if USE(GSTREAMER_WEBRTC)

#include "NetworkConnectionToWebProcessMessages.h"
#include "NetworkProcessConnection.h"
#include "GStreamerIceBackendMessages.h"

namespace WebKit {
using namespace WebCore;

Ref<GStreamerIceBackendProxy> GStreamerIceBackendProxy::create(WebPageProxyIdentifier webPageProxyID)
{
    Ref connection = WebProcess::singleton().ensureNetworkProcessConnection().connection();
    auto sendResult = connection->sendSync(Messages::NetworkConnectionToWebProcess::InitializeGStreamerIceBackend(webPageProxyID), 0);
    auto [identifier] = sendResult.takeReply();
    return adoptRef(*new GStreamerIceBackendProxy(WTFMove(connection), webPageProxyID, *identifier));
}

GStreamerIceBackendProxy::GStreamerIceBackendProxy(Ref<IPC::Connection>&& connection, WebPageProxyIdentifier webPageProxyID, GStreamerIceBackendIdentifier identifier)
    : GStreamerIceBackend()
    , m_connection(WTFMove(connection))
    , m_webPageProxyID(webPageProxyID)
    , m_identifier(identifier)
{
    gst_printerrln("woo %s line %d pid=%d id=%zu", __FILE__, __LINE__, getpid(), messageSenderDestinationID());
}

GStreamerIceBackendProxy::~GStreamerIceBackendProxy()
{
    m_connection->send(Messages::NetworkConnectionToWebProcess::DestroyGStreamerIceBackend(m_identifier), 0);
}

IPC::Connection* GStreamerIceBackendProxy::messageSenderConnection() const
{
    return m_connection.ptr();
}

uint64_t GStreamerIceBackendProxy::messageSenderDestinationID() const
{
    return m_identifier.toUInt64();
}

void GStreamerIceBackendProxy::setForceRelay(bool forceRelay)
{
    MessageSender::send(Messages::GStreamerIceBackend::SetForceRelay { forceRelay });
}

void GStreamerIceBackendProxy::addTurnServer(const String& uri)
{
    MessageSender::send(Messages::GStreamerIceBackend::AddTurnServer { uri });
}

std::optional<unsigned> GStreamerIceBackendProxy::addStream()
{
    // Called from webrtcbin PC thread, and as this is a sync message it needs to be sent from the main thread.
    std::optional<unsigned> streamId;
    callOnMainThreadAndWait([&] {
        auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::AddStream {}, messageSenderDestinationID());
        auto [reply] = sendResult.takeReply();
        streamId = reply;
    });
    return streamId;
}

bool GStreamerIceBackendProxy::gatherCandidatesForStream(unsigned streamId)
{
    auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::GatherCandidatesForStream { streamId }, messageSenderDestinationID());
    auto [result] = sendResult.takeReply();
    return result;
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
