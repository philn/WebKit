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
#include "NetworkTransportStream.h"

#if USE(QUINN)

#include "rust/cxx.h"
#include "webkit-web-transport/src/lib.rs.h"
#include <WebCore/Exception.h>
#include <wtf/CompletionHandler.h>

namespace WebKit {

NetworkTransportStream::NetworkTransportStream(NetworkTransportSession& session, rust::Box<org::webkit::WKQuinnStream>&& stream, NetworkTransportStreamType streamType)
    : m_identifier(WebCore::WebTransportStreamIdentifier::generate())
    , m_stream(WTFMove(stream))
    , m_streamType(streamType)
{
}

void NetworkTransportStream::sendBytes(std::span<const uint8_t> data, bool withFin, CompletionHandler<void(std::optional<WebCore::Exception>&&)>&& completionHandler)
{
    try {
        m_stream->send_bytes(rust::Slice(data.data(), data.size_bytes()), withFin);
        completionHandler(std::nullopt);
    } catch (rust::cxxbridge1::Error exception) {
        completionHandler(WebCore::Exception(WebCore::ExceptionCode::NetworkError));
    }
}

void NetworkTransportStream::cancelReceive(std::optional<WebCore::WebTransportStreamErrorCode>)
{
    m_stream->cancel_receive();
}

void NetworkTransportStream::cancelSend(std::optional<WebCore::WebTransportStreamErrorCode>)
{
    m_stream->cancel_send();
}

void NetworkTransportStream::cancel(std::optional<WebCore::WebTransportStreamErrorCode>)
{
    m_stream->cancel();
}

} // namespace WebKit

#endif // USE(QUINN)
