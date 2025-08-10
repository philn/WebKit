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

//#include "CoroutineUtilities.h"
#include "NetworkConnectionToWebProcess.h"
#include "NetworkTransportStream.h"
#include "rust/cxx.h"
#include "webkit-web-transport/src/lib.rs.h"
#include <wtf/CompletionHandler.h>

namespace WebKit {

void NetworkTransportSession::initialize(NetworkConnectionToWebProcess& connectionToWebProcess, URL&& url, WebKit::WebPageProxyIdentifier&& pageID, WebCore::ClientOrigin&& clientOrigin, CompletionHandler<void(RefPtr<NetworkTransportSession>&&)>&& completionHandler)
{

    //auto client = org::webkit::create_client();
    //client->create_session(url.string().utf8().data(), WTFMove(completionHandler));
    // callCoroutine([url = WTFMove(url), connectionToWebProcess = Ref { connectionToWebProcess }, completionHandler = WTFMove(completionHandler)]() mutable -> Lazy<void> {
    //     try {
    //         auto session = client->create_session(url.string().utf8().data());
    //         completionHandler(NetworkTransportSession::create(connectionToWebProcess, WTFMove(session)));
    //     } catch (rust::cxxbridge1::Error exception) {
    //         completionHandler(nullptr);
    //     }
    // });
}

NetworkTransportSession::NetworkTransportSession(NetworkConnectionToWebProcess&, rust::Box<org::webkit::WKQuinnSession>&& session)
    : m_session(WTFMove(session))
{

}

void NetworkTransportSession::sendDatagram(std::span<const uint8_t>, CompletionHandler<void(std::optional<WebCore::Exception>&&)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void NetworkTransportSession::createOutgoingUnidirectionalStream(CompletionHandler<void(std::optional<WebCore::WebTransportStreamIdentifier>)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

void NetworkTransportSession::createBidirectionalStream(CompletionHandler<void(std::optional<WebCore::WebTransportStreamIdentifier>)>&& completionHandler)
{
    completionHandler(std::nullopt);
}

} // namespace WebKit

#endif // USE(QUINN)
