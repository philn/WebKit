
#include "config.h"
#include "GStreamerIceBackendProxy.h"

#if USE(GSTREAMER_WEBRTC)

#include <WebCore/ExceptionOr.h>
#include "NetworkConnectionToWebProcessMessages.h"
#include "NetworkProcessConnection.h"
#include "GStreamerIceBackendMessages.h"

namespace WebKit {
using namespace WebCore;

Ref<GStreamerIceBackendProxy> GStreamerIceBackendProxy::create(WebPageProxyIdentifier webPageProxyID, WebCore::GStreamerIceBackendClient& client)
{
    Ref connection = WebProcess::singleton().ensureNetworkProcessConnection().connection();
    auto sendResult = connection->sendSync(Messages::NetworkConnectionToWebProcess::InitializeGStreamerIceBackend(webPageProxyID), 0);
    auto [identifier] = sendResult.takeReply();
    return adoptRef(*new GStreamerIceBackendProxy(WTFMove(connection), webPageProxyID, *identifier, client));
}

GStreamerIceBackendProxy::GStreamerIceBackendProxy(Ref<IPC::Connection>&& connection, WebPageProxyIdentifier webPageProxyID, GStreamerIceBackendIdentifier identifier, WebCore::GStreamerIceBackendClient& client)
    : GStreamerIceBackend()
    , m_connection(WTFMove(connection))
    , m_webPageProxyID(webPageProxyID)
    , m_client(&client)
    , m_identifier(identifier)
{
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

void GStreamerIceBackendProxy::setStunServer(const String& uri)
{
    MessageSender::send(Messages::GStreamerIceBackend::SetStunServer { uri });
}

Expected<bool, ExceptionData> GStreamerIceBackendProxy::addTurnServer(const String& uri)
{
    auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::AddTurnServer { uri }, messageSenderDestinationID());
    auto [reply] = sendResult.takeReply();
    return reply;
}

std::optional<unsigned> GStreamerIceBackendProxy::addStream(unsigned sessionId)
{
    // Called from webrtcbin PC thread, and as this is a sync message it needs to be sent from the main thread.
    std::optional<unsigned> streamId;
    callOnMainThreadAndWait([&] {
        auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::AddStream { sessionId }, messageSenderDestinationID());
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

void GStreamerIceBackendProxy::setIsController(bool isController)
{
    MessageSender::send(Messages::GStreamerIceBackend::SetIsController { isController });
}

void GStreamerIceBackendProxy::addCandidate(unsigned streamId, const String& candidate, GStreamerIceBackend::AddCandidateCallback&& callback)
{
    auto completionHandler = [callback = WTFMove(callback)](auto&& valueOrException) mutable {
        if (!valueOrException.has_value()) {
            callback(valueOrException.error().toException());
            return;
        }
        callback(WTFMove(*valueOrException));
    };

    m_connection->sendWithAsyncReply(Messages::GStreamerIceBackend::AddCandidate { streamId, candidate }, WTFMove(completionHandler));
}

void GStreamerIceBackendProxy::notifyNewCandidate(unsigned sessionId, String&& candidate)
{
    m_client->notifyIceCandidate(sessionId, candidate);
}

void GStreamerIceBackendProxy::notifyGatheringDone(unsigned streamId)
{
    m_client->notifyGatheringDone(streamId);
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
