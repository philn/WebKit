
#pragma once

#if USE(GSTREAMER_WEBRTC)

#include "MessageReceiver.h"
#include "MessageSender.h"
#include "SharedPreferencesForWebProcess.h"
#include "WebPageProxyIdentifier.h"
#include "WebProcess.h"
#include <WebCore/Document.h>
#include <WebCore/GStreamerIceAgent.h>
#include <WebCore/RTCIceComponent.h>
#include <WebCore/RTCIceConnectionState.h>
#include <wtf/ThreadSafeWeakPtr.h>

namespace IPC {
class Connection;
class Decoder;
}

namespace WebKit {

struct GStreamerIceBackendIdentifierType { };

using GStreamerIceBackendIdentifier = ObjectIdentifier<GStreamerIceBackendIdentifierType>;

class GStreamerIceBackendProxy : public IPC::MessageSender, public IPC::MessageReceiver, public WebCore::GStreamerIceBackend, public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<GStreamerIceBackendProxy, WTF::DestructionThread::MainRunLoop> {
public:
    static Ref<GStreamerIceBackendProxy> create(WebPageProxyIdentifier, WebCore::GStreamerIceBackendClient&);
    ~GStreamerIceBackendProxy();

    void ref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::ref(); }
    void deref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::deref(); }

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);

private:
    GStreamerIceBackendProxy(Ref<IPC::Connection>&&, WebPageProxyIdentifier, GStreamerIceBackendIdentifier, WebCore::GStreamerIceBackendClient&);

    // GStreamerIceBackend (Web -> Network)
    void setForceRelay(bool) final;
    void setStunServer(const String&) final;
    Expected<bool, WebCore::ExceptionData> addTurnServer(const String&) final;
    std::optional<unsigned> addStream(unsigned) final;
    bool gatherCandidatesForStream(unsigned) final;
    void setIsController(bool) final;
    void setTurnServer(const String&) final;
    void setTos(unsigned, unsigned) final;
    bool setLocalCredentials(unsigned, const String&, const String&) final;
    bool setRemoteCredentials(unsigned, const String&, const String&) final;

    void addCandidate(unsigned, const String&, WebCore::GStreamerIceBackend::AddCandidateCallback&&) final;

    bool send(unsigned, WebCore::RTCIceComponent, std::span<const uint8_t>) final;

    void finalizeStream(unsigned) final;

    void refGStreamerIceBackend() final { ref(); }
    void derefGStreamerIceBackend() final { deref(); }

    // GStreamerIceBackendClient (Network -> Web)
    void notifyNewCandidate(unsigned, String&&);
    void notifyGatheringDone(unsigned);
    void notifyComponentStateChanged(unsigned, WebCore::RTCIceComponent, WebCore::RTCIceConnectionState);
    void notifyNewSelectedPair(unsigned, WebCore::RTCIceComponent);
    void notifyDataRead(unsigned, WebCore::RTCIceComponent, std::span<const uint8_t>);

    // MessageSender
    IPC::Connection* messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final;

    const Ref<IPC::Connection> m_connection;
    WebPageProxyIdentifier m_webPageProxyID;
    RefPtr<WebCore::GStreamerIceBackendClient> m_client;
    const GStreamerIceBackendIdentifier m_identifier;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
