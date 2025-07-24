
#pragma once

#if USE(GSTREAMER_WEBRTC) && USE(LIBNICE)

#include "GRefPtrNice.h"

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

private:
    virtual IPC::Connection* connection() const = 0;

    GRefPtr<NiceAgent> m_agent;
    GRefPtr<GMainContext> m_mainContext;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBNICE)
