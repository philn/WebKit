/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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
#include "WebSpeechRecognitionConnection.h"

#include "MessageSenderInlines.h"
#include "SpeechRecognitionServerMessages.h"
#include "WebFrame.h"
#include "WebProcess.h"
#include "WebProcessProxyMessages.h"
#include "WebSpeechRecognitionConnectionMessages.h"
#include <WebCore/SpeechRecognitionConnectionClient.h>
#include <WebCore/SpeechRecognitionRequestInfo.h>
#include <WebCore/SpeechRecognitionUpdate.h>

#if USE(GSTREAMER)
#include <WebCore/SpeechRecognitionCaptureSource.h>
#include <WebCore/SpeechRecognizer.h>
#endif

namespace WebKit {

Ref<WebSpeechRecognitionConnection> WebSpeechRecognitionConnection::create(SpeechRecognitionConnectionIdentifier identifier)
{
    return adoptRef(*new WebSpeechRecognitionConnection(identifier));
}

WebSpeechRecognitionConnection::WebSpeechRecognitionConnection(SpeechRecognitionConnectionIdentifier identifier)
    : m_identifier(identifier)
{
    WebProcess::singleton().addMessageReceiver(Messages::WebSpeechRecognitionConnection::messageReceiverName(), m_identifier, *this);
#if USE(GSTREAMER)
    auto permissionChecker = [](auto& request, auto&& completionHandler) mutable {
        // FIXME: Send the UI process a request for the speech recognition permission.
        completionHandler(std::nullopt);
    };
    auto checkIfMockCaptureDevicesEnabled = []() {
        // FIXME: Ask the UI process if the mock capture device is enabled.
        return false;
    };

    m_speechRecognitionServer = ThreadedSpeechRecognitionServer::create(*this, m_identifier, WTFMove(permissionChecker), WTFMove(checkIfMockCaptureDevicesEnabled));
#else
    send(Messages::WebProcessProxy::CreateSpeechRecognitionServer(m_identifier), 0);
#endif

#if ENABLE(MEDIA_STREAM)
    WebProcess::singleton().ensureSpeechRecognitionRealtimeMediaSourceManager();
#endif
}

WebSpeechRecognitionConnection::~WebSpeechRecognitionConnection()
{
#if USE(GSTREAMER)
    m_speechRecognitionServer = nullptr;
#else
    send(Messages::WebProcessProxy::DestroySpeechRecognitionServer(m_identifier), 0);
#endif
    WebProcess::singleton().removeMessageReceiver(*this);
}

void WebSpeechRecognitionConnection::registerClient(WebCore::SpeechRecognitionConnectionClient& client)
{
    m_clientMap.add(client.identifier(), client);
}

void WebSpeechRecognitionConnection::unregisterClient(WebCore::SpeechRecognitionConnectionClient& client)
{
    m_clientMap.remove(client.identifier());
}

void WebSpeechRecognitionConnection::start(WebCore::SpeechRecognitionConnectionClientIdentifier clientIdentifier, const String& lang, bool continuous, bool interimResults, uint64_t maxAlternatives, WebCore::ClientOrigin&& clientOrigin, WebCore::FrameIdentifier frameIdentifier)
{
#if USE(GSTREAMER)
    m_speechRecognitionServer->start(clientIdentifier, lang, continuous, interimResults, maxAlternatives, WTFMove(clientOrigin), frameIdentifier);
#else
    send(Messages::SpeechRecognitionServer::Start(clientIdentifier, lang, continuous, interimResults, maxAlternatives, WTFMove(clientOrigin), frameIdentifier));
#endif
}

void WebSpeechRecognitionConnection::stop(WebCore::SpeechRecognitionConnectionClientIdentifier clientIdentifier)
{
#if USE(GSTREAMER)
    m_speechRecognitionServer->stop(clientIdentifier);
#else
    send(Messages::SpeechRecognitionServer::Stop(clientIdentifier));
#endif
}

void WebSpeechRecognitionConnection::abort(WebCore::SpeechRecognitionConnectionClientIdentifier clientIdentifier)
{
#if USE(GSTREAMER)
    m_speechRecognitionServer->abort(clientIdentifier);
#else
    send(Messages::SpeechRecognitionServer::Abort(clientIdentifier));
#endif
}

void WebSpeechRecognitionConnection::invalidate(WebCore::SpeechRecognitionConnectionClientIdentifier clientIdentifier)
{
#if USE(GSTREAMER)
    m_speechRecognitionServer->invalidate(clientIdentifier);
#else
    send(Messages::SpeechRecognitionServer::Invalidate(clientIdentifier));
#endif
}

void WebSpeechRecognitionConnection::didReceiveUpdate(WebCore::SpeechRecognitionUpdate&& update)
{
    auto clientIdentifier = update.clientIdentifier();
    if (!m_clientMap.contains(clientIdentifier))
        return;

    auto client = m_clientMap.get(clientIdentifier);
    if (!client) {
        m_clientMap.remove(clientIdentifier);
        // Inform server that client does not exist any more.
        invalidate(clientIdentifier);
        return;
    }

    switch (update.type()) {
    case WebCore::SpeechRecognitionUpdateType::Start:
        printf("WebSpeechRecognitionConnection::%s(%d) Start\n", __FUNCTION__, __LINE__);
        client->didStart();
        break;
    case WebCore::SpeechRecognitionUpdateType::AudioStart:
        printf("WebSpeechRecognitionConnection::%s(%d) AudioStart\n", __FUNCTION__, __LINE__);
        client->didStartCapturingAudio();
        break;
    case WebCore::SpeechRecognitionUpdateType::SoundStart:
        printf("WebSpeechRecognitionConnection::%s(%d) SoundStart\n", __FUNCTION__, __LINE__);
        client->didStartCapturingSound();
        break;
    case WebCore::SpeechRecognitionUpdateType::SpeechStart:
        printf("WebSpeechRecognitionConnection::%s(%d) SpeechStart\n", __FUNCTION__, __LINE__);
        client->didStartCapturingSpeech();
        break;
    case WebCore::SpeechRecognitionUpdateType::SpeechEnd:
        printf("WebSpeechRecognitionConnection::%s(%d) SpeechEnd\n", __FUNCTION__, __LINE__);
        client->didStopCapturingSpeech();
        break;
    case WebCore::SpeechRecognitionUpdateType::SoundEnd:
        printf("WebSpeechRecognitionConnection::%s(%d) SoundEnd\n", __FUNCTION__, __LINE__);
        client->didStopCapturingSound();
        break;
    case WebCore::SpeechRecognitionUpdateType::AudioEnd:
        printf("WebSpeechRecognitionConnection::%s(%d) AudioEnd\n", __FUNCTION__, __LINE__);
        client->didStopCapturingAudio();
        break;
    case WebCore::SpeechRecognitionUpdateType::NoMatch:
        printf("WebSpeechRecognitionConnection::%s(%d) NoMatch\n", __FUNCTION__, __LINE__);
        client->didFindNoMatch();
        break;
    case WebCore::SpeechRecognitionUpdateType::Result:
        printf("WebSpeechRecognitionConnection::%s(%d) Result\n", __FUNCTION__, __LINE__);
        client->didReceiveResult(update.result());
        break;
    case WebCore::SpeechRecognitionUpdateType::Error:
        printf("WebSpeechRecognitionConnection::%s(%d) Error\n", __FUNCTION__, __LINE__);
        client->didError(update.error());
        break;
    case WebCore::SpeechRecognitionUpdateType::End:
        printf("WebSpeechRecognitionConnection::%s(%d) End\n", __FUNCTION__, __LINE__);
        client->didEnd();
    }
}

IPC::Connection* WebSpeechRecognitionConnection::messageSenderConnection() const
{
    return WebProcess::singleton().parentProcessConnection();
}

uint64_t WebSpeechRecognitionConnection::messageSenderDestinationID() const
{
    return m_identifier.toUInt64();
}

} // namespace WebKit
