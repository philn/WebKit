
#pragma once

#if USE(GSTREAMER_WEBRTC)

#include "MessageReceiver.h"
#include "MessageSender.h"
#include "SharedPreferencesForWebProcess.h"
#include "WebPageProxyIdentifier.h"
#include "WebProcess.h"
#include <WebCore/Document.h>
#include <WebCore/GStreamerIceAgent.h>

namespace IPC {
class Connection;
class Decoder;
}

namespace WebKit {

struct GStreamerIceBackendIdentifierType { };

using GStreamerIceBackendIdentifier = ObjectIdentifier<GStreamerIceBackendIdentifierType>;

class GStreamerIceBackendProxy : public IPC::MessageSender, public IPC::MessageReceiver, public WebCore::GStreamerIceBackend, public RefCounted<GStreamerIceBackendProxy> {
public:
    static Ref<GStreamerIceBackendProxy> create(WebPageProxyIdentifier, WebCore::GStreamerIceBackendClient&);
    ~GStreamerIceBackendProxy();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);

    using RefCounted<GStreamerIceBackendProxy>::ref;
    using RefCounted<GStreamerIceBackendProxy>::deref;

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

    void refGStreamerIceBackend() final { ref(); }
    void derefGStreamerIceBackend() final { deref(); }

    // GStreamerIceBackendClient (Network -> Web)
    void notifyNewCandidate(unsigned, String&&);
    void notifyGatheringDone(unsigned);

    // MessageSender
    IPC::Connection *messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final;

    const Ref<IPC::Connection> m_connection;
    WebPageProxyIdentifier m_webPageProxyID;
    RefPtr<WebCore::GStreamerIceBackendClient> m_client;
    const GStreamerIceBackendIdentifier m_identifier;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
