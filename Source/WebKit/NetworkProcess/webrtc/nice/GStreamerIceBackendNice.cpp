
#include "config.h"
#include "GStreamerIceBackendNice.h"

#if USE(GSTREAMER_WEBRTC) && USE(LIBNICE)
#include <nice.h>

namespace WebKit {

GStreamerIceBackendNice::GStreamerIceBackendNice()
{
    {
        Locker locker(m_lock);
        m_thread = Thread::create("webrtc-nice", [&] {
            Locker locker(m_lock);
            m_mainContext = adoptGRef(g_main_context_new());
            m_loop = adoptGRef(g_main_loop_new(m_mainContext.get(), FALSE));
            m_condition.notifyAll();
            g_main_context_invoke(m_mainContext.get(), reinterpret_cast<GSourceFunc>(+[](gpointer data) -> gboolean {
                reinterpret_cast<Locker<Lock>*>(data)->unlockEarly();
                return G_SOURCE_REMOVE;
            }), &locker);

            g_main_context_push_thread_default(m_mainContext.get());
            g_main_loop_run(m_loop.get());
            g_main_context_pop_thread_default(m_mainContext.get());
        });
        m_thread->detach();

        while (!m_loop)
            m_condition.wait(m_lock);
    }

    auto options = static_cast<NiceAgentOption>(NICE_AGENT_OPTION_ICE_TRICKLE | NICE_AGENT_OPTION_REGULAR_NOMINATION | NICE_AGENT_OPTION_CONSENT_FRESHNESS);
    m_agent = adoptGRef(nice_agent_new_full(m_mainContext.get(), NICE_COMPATIBILITY_RFC5245, options));
    g_signal_connect(m_agent.get(), "new-candidate-full", G_CALLBACK(+[](NiceAgent*, NiceCandidate*, gpointer) {

    }), this);
}

GStreamerIceBackendNice::~GStreamerIceBackendNice()
{
    g_signal_handlers_disconnect_by_data(m_agent.get(), this);
}

void GStreamerIceBackendNice::setForceRelay(bool forceRelay)
{
    g_object_set(m_agent.get(), "force-relay", forceRelay, nullptr);
}

void GStreamerIceBackendNice::addTurnServer(const String& uri)
{
    WTFLogAlways("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
}

std::optional<unsigned> GStreamerIceBackendNice::addStream()
{
    auto streamId = nice_agent_add_stream(m_agent.get(), 1);
    if (!streamId)
        return std::nullopt;

    return { streamId };
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBNICE)
