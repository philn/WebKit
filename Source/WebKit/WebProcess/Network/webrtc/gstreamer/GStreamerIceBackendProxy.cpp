
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
    ASSERT(RunLoop::isMain());
    WebProcess::singleton().addGStreamerIceBackend(m_identifier, *this);
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

void GStreamerIceBackendProxy::setTurnServer(const String& uri)
{
    MessageSender::send(Messages::GStreamerIceBackend::SetTurnServer { uri });
}

void GStreamerIceBackendProxy::setTos(unsigned streamId, unsigned tos)
{
    MessageSender::send(Messages::GStreamerIceBackend::SetTos { streamId, tos });
}

bool GStreamerIceBackendProxy::setLocalCredentials(unsigned streamId, const String& ufrag, const String& pwd)
{
    // Called from webrtcbin PC thread, and as this is a sync message it needs to be sent from the main thread.
    bool result;
    callOnMainThreadAndWait([&] {
        auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::SetLocalCredentials { streamId, ufrag, pwd }, messageSenderDestinationID());
        auto [reply] = sendResult.takeReply();
        result = reply;
    });
    return result;
}

bool GStreamerIceBackendProxy::setRemoteCredentials(unsigned streamId, const String& ufrag, const String& pwd)
{
    // Called from webrtcbin PC thread, and as this is a sync message it needs to be sent from the main thread.
    bool result;
    callOnMainThreadAndWait([&] {
        auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::SetRemoteCredentials { streamId, ufrag, pwd }, messageSenderDestinationID());
        auto [reply] = sendResult.takeReply();
        result = reply;
    });
    return result;
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
    // Called from webrtcbin PC thread, and as this is a sync message it needs to be sent from the main thread.
    bool result;
    callOnMainThreadAndWait([&] {
        auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::GatherCandidatesForStream { streamId }, messageSenderDestinationID());
        auto [reply] = sendResult.takeReply();
        result = reply;
    });
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

    m_connection->sendWithAsyncReply(Messages::GStreamerIceBackend::AddCandidate { streamId, candidate }, WTFMove(completionHandler), messageSenderDestinationID());
}

void GStreamerIceBackendProxy::notifyNewCandidate(unsigned sessionId, String&& candidate)
{
    m_client->notifyIceCandidate(sessionId, candidate);
}

void GStreamerIceBackendProxy::notifyGatheringDone(unsigned streamId)
{
    if (!streamId)
        return;
    m_client->notifyGatheringDone(streamId);
}

void GStreamerIceBackendProxy::notifyComponentStateChanged(unsigned streamId, RTCIceComponent component, RTCIceConnectionState state)
{
    m_client->notifyComponentStateChanged(streamId, component, state);
}

void GStreamerIceBackendProxy::notifyNewSelectedPair(unsigned streamId, RTCIceComponent component)
{
    m_client->notifyNewSelectedPair(streamId, component);
}

void GStreamerIceBackendProxy::notifyDataRead(unsigned streamId, RTCIceComponent component, std::span<const uint8_t> data)
{
    m_client->notifyDataRead(streamId, component, WTFMove(data));
}

void GStreamerIceBackendProxy::send(unsigned streamId, RTCIceComponent component, std::span<const uint8_t> data)
{
    MessageSender::send(Messages::GStreamerIceBackend::SendData { streamId, component, WTFMove(data) });
}

void GStreamerIceBackendProxy::finalizeStream(unsigned streamId)
{
    MessageSender::send(Messages::GStreamerIceBackend::FinalizeStream { streamId });
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
