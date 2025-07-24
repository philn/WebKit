/*
 *  Copyright (C) 2025 Igalia S.L. All rights reserved.
 *  Copyright (C) 2025 Metrological Group B.V.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "GStreamerIceBackendProxy.h"

#if USE(GSTREAMER_WEBRTC)

#include "GStreamerIceBackendMessages.h"
#include "NetworkConnectionToWebProcessMessages.h"
#include "NetworkProcessConnection.h"

#include <WebCore/ExceptionOr.h>
#include <WebCore/GStreamerIceUtilities.h>

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
    WebProcess::singleton().removeGStreamerIceBackend(m_identifier);
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

void GStreamerIceBackendProxy::resolveAddress(const String& address, GStreamerIceBackend::ResolveAddressCallback&& callback)
{
    m_connection->sendWithAsyncReply(Messages::GStreamerIceBackend::ResolveAddress { address }, [callback = WTFMove(callback)](auto&& valueOrException) mutable {
        if (!valueOrException.has_value()) {
            callback(valueOrException.error().toException());
            return;
        }
        callback(WTFMove(*valueOrException));
    }, messageSenderDestinationID());
}

Vector<String> GStreamerIceBackendProxy::gatherSocketAddresses(unsigned streamId)
{
    Vector<String> addresses;
    callOnMainRunLoopAndWait([&] {
        auto sendResult = m_connection->sendSync(Messages::GStreamerIceBackend::GatherSocketAddresses { streamId }, messageSenderDestinationID());
        auto [reply] = sendResult.takeReply();
        addresses = reply;
    });
    return addresses;
}

void GStreamerIceBackendProxy::notifyIncomingData(unsigned streamId, RTCIceProtocol protocol, String from, String to, std::span<const uint8_t> data)
{
    m_client->notifyIncomingData(streamId, protocol, from, to, WTFMove(data));
}

void GStreamerIceBackendProxy::send(unsigned streamId, RTCIceProtocol protocol, String from, String to, std::span<const uint8_t> data)
{
    MessageSender::send(Messages::GStreamerIceBackend::SendData { streamId, protocol, from, to, WTFMove(data) });
}

void GStreamerIceBackendProxy::finalizeStream(unsigned streamId)
{
    MessageSender::send(Messages::GStreamerIceBackend::FinalizeStream { streamId });
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
