
#pragma once

#if USE(GSTREAMER_WEBRTC)

#include "MessageReceiver.h"
#include "MessageSender.h"
#include "WebPageProxyIdentifier.h"

#if USE(LIBNICE)
#include "GStreamerIceBackendNice.h"
#endif

#include <wtf/Forward.h>
#include <wtf/Identified.h>
#include <wtf/RefCounted.h>
#include <wtf/TZoneMalloc.h>

namespace IPC {
class Connection;
class Decoder;
}

namespace WebKit {

class NetworkConnectionToWebProcess;
struct SharedPreferencesForWebProcess;

struct GStreamerIceBackendIdentifierType;

using GStreamerIceBackendIdentifier = ObjectIdentifier<GStreamerIceBackendIdentifierType>;

class GStreamerIceBackend : public RefCounted<GStreamerIceBackend>, public IPC::MessageReceiver, public IPC::MessageSender, public Identified<GStreamerIceBackendIdentifier>
#if USE(LIBNICE)
    , public GStreamerIceBackendNice
#endif
    {
    WTF_MAKE_TZONE_ALLOCATED(GStreamerIceBackend);
public:
    static void initialize(NetworkConnectionToWebProcess&, WebKit::WebPageProxyIdentifier&&, CompletionHandler<void(RefPtr<GStreamerIceBackend>&&)>&&);
    ~GStreamerIceBackend();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);
    bool didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, UniqueRef<IPC::Encoder>&);
    std::optional<SharedPreferencesForWebProcess> sharedPreferencesForWebProcess() const;

private:
#if USE(LIBNICE)
    friend class GStreamerIceBackendNice;
#endif

    template<typename... Args> static Ref<GStreamerIceBackend> create(Args&&...args) { return adoptRef(*new GStreamerIceBackend(std::forward<Args>(args)...)); }

    GStreamerIceBackend(NetworkConnectionToWebProcess&);
    IPC::Connection *messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final;

    IPC::Connection* connection() const final { return messageSenderConnection(); }
    uint64_t destination() const final { return messageSenderDestinationID(); }

    WeakPtr<NetworkConnectionToWebProcess> m_connection;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC)
