/*
 * Copyright (C) 2025 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "NetworkTransportSession.h"

#if USE(QUINN)

#include "NetworkConnectionToWebProcess.h"
#include "NetworkTransportStream.h"
#include "rust/cxx.h"
#include "webkit-web-transport/src/lib.rs.h"
#include <WebCore/Exception.h>
#include <wtf/CompletionHandler.h>

namespace WebKit {

void NetworkTransportSession::initialize(NetworkConnectionToWebProcess& connectionToWebProcess, URL&& url, WebKit::WebPageProxyIdentifier&& pageID, WebCore::ClientOrigin&& clientOrigin, CompletionHandler<void(RefPtr<NetworkTransportSession>&&)>&& completionHandler)
{
    try {
        auto endpoint = org::webkit::create_endpoint();
        auto connection = endpoint->create_connection(url.string().utf8().data());
        completionHandler(NetworkTransportSession::create(connectionToWebProcess, WTFMove(endpoint), WTFMove(connection)));
    } catch (rust::cxxbridge1::Error exception) {
        completionHandler(nullptr);
    }
}

NetworkTransportSession::NetworkTransportSession(NetworkConnectionToWebProcess&, rust::Box<org::webkit::WKQuinnEndPoint>&& endpoint, rust::Box<org::webkit::WKQuinnConnection>&& connection)
    : m_endpoint(WTFMove(endpoint))
    , m_connection(WTFMove(connection))
{

}

void NetworkTransportSession::sendDatagram(std::span<const uint8_t> data, CompletionHandler<void(std::optional<WebCore::Exception>&&)>&& completionHandler)
{
    try {
        m_connection->send_datagram(rust::Slice(data.data(), data.size_bytes()));
        completionHandler(std::nullopt);
    } catch (rust::cxxbridge1::Error exception) {
        completionHandler(WebCore::Exception(WebCore::ExceptionCode::NetworkError));
    }
}

void NetworkTransportSession::createStream(NetworkTransportStreamType streamType, CompletionHandler<void(std::optional<WebCore::WebTransportStreamIdentifier>)>&& completionHandler)
{
    RefPtr<NetworkTransportStream> stream;
    org::webkit::StreamType type;
    switch (streamType) {
    case NetworkTransportStreamType::OutgoingUnidirectional:
        type = org::webkit::StreamType::Unidirectional;
        break;
    case NetworkTransportStreamType::Bidirectional:
        type = org::webkit::StreamType::Bidirectional;
        break;
    case NetworkTransportStreamType::IncomingUnidirectional:
        ASSERT_NOT_REACHED();
        return;
    };
    try {
        auto quinnStream = m_connection->create_stream(type);
        Ref stream = NetworkTransportStream::create(*this, WTFMove(quinnStream), streamType);
    } catch (rust::cxxbridge1::Error exception) {
        completionHandler(std::nullopt);
    }
}

void NetworkTransportSession::createOutgoingUnidirectionalStream(CompletionHandler<void(std::optional<WebCore::WebTransportStreamIdentifier>)> && completionHandler)
{
    createStream(NetworkTransportStreamType::OutgoingUnidirectional, WTFMove(completionHandler));
}

void NetworkTransportSession::createBidirectionalStream(CompletionHandler<void(std::optional<WebCore::WebTransportStreamIdentifier>)> && completionHandler)
{
    createStream(NetworkTransportStreamType::Bidirectional, WTFMove(completionHandler));
}

} // namespace WebKit

#endif // USE(QUINN)
