
#pragma once

#if USE(GSTREAMER_WEBRTC) && USE(LIBNICE)

#include "GRefPtrNice.h"
#include "GUniquePtrNice.h"
#include <wtf/Condition.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/Lock.h>
#include <wtf/RunLoop.h>
#include <wtf/Vector.h>

namespace IPC {
class Connection;
}

namespace WebKit {

class GStreamerIceBackendNice {
public:
    GStreamerIceBackendNice();
    ~GStreamerIceBackendNice();

    void setForceRelay(bool);
    void setStunServer(const String&);
    void addTurnServer(const String&);
    void addStream(unsigned sessionId, CompletionHandler<void(std::optional<unsigned>)>&&);
    void gatherCandidatesForStream(unsigned, CompletionHandler<void(bool)>&&);
    void setIsController(bool);

private:
    virtual IPC::Connection* connection() const = 0;

    void notifyNewCandidate(const NiceCandidate&);

    void fillLocalCandidateCredentials(const NiceCandidate&, GUniqueOutPtr<NiceCandidate>&);

    GRefPtr<NiceAgent> m_agent;

    RefPtr<Thread> m_thread;
    GRefPtr<GMainContext> m_mainContext;
    GRefPtr<GMainLoop> m_loop;
    Lock m_lock;
    Condition m_condition;

    String m_stunServer;

    struct StreamItem {
        unsigned sessionId;
        unsigned streamId;
    };
    Vector<StreamItem> m_streams;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBNICE)
