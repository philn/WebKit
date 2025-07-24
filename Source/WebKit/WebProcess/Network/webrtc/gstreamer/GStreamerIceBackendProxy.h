
#pragma once

#if USE(GSTREAMER_WEBRTC)

#include "MessageReceiver.h"
#include "MessageSender.h"
#include "SharedPreferencesForWebProcess.h"
#include "WebPageProxyIdentifier.h"
#include "WebProcess.h"
#include <WebCore/Document.h>
#include <WebCore/GStreamerIceBackend.h>

namespace IPC {
class Connection;
class Decoder;
}

namespace WebKit {

struct GStreamerIceBackendIdentifierType { };

using GStreamerIceBackendIdentifier = ObjectIdentifier<GStreamerIceBackendIdentifierType>;

class GStreamerIceBackendProxy : public IPC::MessageSender, public IPC::MessageReceiver, public WebCore::GStreamerIceBackend, public RefCounted<GStreamerIceBackendProxy> {
public:
    static Ref<GStreamerIceBackendProxy> create(WebPageProxyIdentifier);
    ~GStreamerIceBackendProxy();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);

    using RefCounted<GStreamerIceBackendProxy>::ref;
    using RefCounted<GStreamerIceBackendProxy>::deref;

private:
    GStreamerIceBackendProxy(Ref<IPC::Connection>&&, WebPageProxyIdentifier, GStreamerIceBackendIdentifier);

    // GStreamerIceBackend
    void setForceRelay(bool) final;
    void addTurnServer(const String&) final;
    std::optional<unsigned> addStream() final;
    void refGStreamerIceBackend() final { ref(); }
    void derefGStreamerIceBackend() final { deref(); }

    // MessageSender
    IPC::Connection *messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final;

    const Ref<IPC::Connection> m_connection;
    WebPageProxyIdentifier m_webPageProxyID;
    const GStreamerIceBackendIdentifier m_identifier;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
