
#pragma once

#if USE(GSTREAMER_WEBRTC) && USE(LIBNICE)

#include "GRefPtrNice.h"
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
    void addTurnServer(const String&);
    void addStream(CompletionHandler<void(std::optional<unsigned>)>&&);
    void gatherCandidatesForStream(unsigned, CompletionHandler<void(bool)>&&);

private:
    virtual IPC::Connection* connection() const = 0;

    GRefPtr<NiceAgent> m_agent;

    RefPtr<Thread> m_thread;
    GRefPtr<GMainContext> m_mainContext;
    GRefPtr<GMainLoop> m_loop;
    Lock m_lock;
    Condition m_condition;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBNICE)
