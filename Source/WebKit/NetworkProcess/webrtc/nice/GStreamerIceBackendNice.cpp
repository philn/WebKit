
#include "config.h"
#include "GStreamerIceBackendNice.h"

#if USE(GSTREAMER_WEBRTC) && USE(LIBNICE)

#include "GStreamerIceBackendProxyMessages.h"
#include "NetworkConnectionToWebProcessMessages.h"
#include "NetworkProcessConnection.h"
#include <gio/gio.h>
#include <glib.h>
#include <nice.h>
#include <wtf/CompletionHandler.h>
#include <wtf/glib/GThreadSafeWeakPtr.h>
#include <wtf/glib/WTFGType.h>

using namespace WebCore;

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
    g_signal_connect(m_agent.get(), "new-candidate-full", G_CALLBACK(+[](NiceAgent*, NiceCandidate* candidate, gpointer userData) {
        auto self = reinterpret_cast<GStreamerIceBackendNice*>(userData);
        self->notifyNewCandidate(*candidate);
    }), this);
}

GStreamerIceBackendNice::~GStreamerIceBackendNice()
{
    g_signal_handlers_disconnect_by_data(m_agent.get(), this);
}

void GStreamerIceBackendNice::notifyNewCandidate(const NiceCandidate& candidate)
{
    std::optional<unsigned> sessionId;
    for (const auto& stream : m_streams) {
        if (stream.streamId == candidate.stream_id) {
            sessionId = stream.sessionId;
            break;
        }
    }
    if (!sessionId) [[unlikely]]
        return;

    GUniqueOutPtr<NiceCandidate> filledCandidate;
    fillLocalCandidateCredentials(candidate, filledCandidate);

    GUniquePtr<char> sdp(nice_agent_generate_local_candidate_sdp(m_agent.get(), filledCandidate.get()));
    connection()->send(Messages::GStreamerIceBackendProxy::NotifyNewCandidate { *sessionId, String::fromUTF8(sdp.get()) }, 0);
}

void GStreamerIceBackendNice::fillLocalCandidateCredentials(const NiceCandidate& candidate, GUniqueOutPtr<NiceCandidate>& result)
{
    result.outPtr() = nice_candidate_copy(&candidate);
    if (candidate.username && candidate.password)
        return;

    GUniqueOutPtr<char> ufrag;
    GUniqueOutPtr<char> password;
    auto gotCredentials = nice_agent_get_local_credentials(m_agent.get(), candidate.stream_id, &ufrag.outPtr(), &password.outPtr());
    ASSERT(gotCredentials);

    if (!candidate.username)
        result->username = ufrag.release();
    if (!candidate.password)
        result->password = password.release();
}

void GStreamerIceBackendNice::setForceRelay(bool forceRelay)
{
    g_object_set(m_agent.get(), "force-relay", forceRelay, nullptr);
}

void GStreamerIceBackendNice::setStunServer(const String& uri)
{
    m_stunServer = uri;
}

void GStreamerIceBackendNice::addTurnServer(const String& uri)
{
    WTFLogAlways("woo %s line %d pid=%d", __FILE__, __LINE__, getpid());
}

void GStreamerIceBackendNice::addStream(unsigned sessionId, CompletionHandler<void(std::optional<unsigned>)>&& completionHandler)
{
    // TODO: Implement as in gst_webrtc_nice_add_stream
    auto streamId = nice_agent_add_stream(m_agent.get(), 1);
    if (!streamId) {
        completionHandler(std::nullopt);
        return;
    }

    m_streams.append({ sessionId, streamId });
    completionHandler({ streamId });
}

void GStreamerIceBackendNice::gatherCandidatesForStream(unsigned streamId, CompletionHandler<void(bool)>&& completionHandler)
{
    // TODO: Implement as in gst_webrtc_nice_stream_gather_candidates

    if (!nice_agent_gather_candidates(m_agent.get(), streamId)) {
        completionHandler(false);
        return;
    }

    completionHandler(true);
}

void GStreamerIceBackendNice::setIsController(bool isController)
{
    g_object_set(m_agent.get(), "controlling-mode", isController, nullptr);
}

Expected<GStreamerIceBackendNice::CandidateAddress, ExceptionData> GStreamerIceBackendNice::getCandidateAddress(StringView candidate)
{
    if (!candidate.startsWith("a=candidate:"_s))
        return makeUnexpected(ExceptionData { ExceptionCode::NotSupportedError, "Candidate does not start with \"a=candidate:\""_s});

    auto tokens = candidate.toStringWithoutCopying().substring(13).split(' ');
    if (tokens.size() < 6)
        return makeUnexpected(ExceptionData { ExceptionCode::DataError, makeString("Candidate \""_s, candidate, "\" tokenization resulted in not enough tokens"_s)});

    CandidateAddress result;
    result.address = tokens[4];

    StringBuilder prefixBuilder;
    for (unsigned i = 0; i < 4; i++)
        prefixBuilder.append(tokens[i]);
    result.prefix = prefixBuilder.toString();

    StringBuilder postfixBuilder;
    for (unsigned i = 5; i < tokens.size(); i++)
        postfixBuilder.append(tokens[i]);
    result.postfix = postfixBuilder.toString();
    return result;
}

void GStreamerIceBackendNice::addIceCandidateToAgent(NiceAgent* agent, unsigned streamId, NiceCandidate& candidate)
{
    if (candidate.component_id == 2) {
        // We only support rtcp-mux so rtcp candidates are useless for us.
        return;
    }

    GSList* candidates = nullptr;
    candidates = g_slist_append(candidates, &candidate);
    nice_agent_set_remote_candidates(agent, streamId, candidate.component_id, candidates);
    g_slist_free(candidates);
}

struct ResolveAddressData {
    GRefPtr<GResolver> resolver;
    String address;
    CompletionHandler<void(Expected<String, WebCore::ExceptionData>&&)> callback;
};
WEBKIT_DEFINE_ASYNC_DATA_STRUCT(ResolveAddressData);

struct ResolveAddressDataInner {
    CompletionHandler<void(Expected<String, WebCore::ExceptionData>&&)> callback;
};
WEBKIT_DEFINE_ASYNC_DATA_STRUCT(ResolveAddressDataInner);

void GStreamerIceBackendNice::resolveAddress(String&& address, CompletionHandler<void(Expected<String, WebCore::ExceptionData>&&)>&& completionHandler)
{
    auto data = createResolveAddressData();
    data->resolver = adoptGRef(g_resolver_get_default());
    data->address = WTFMove(address);
    data->callback = WTFMove(completionHandler);
    g_main_context_invoke_full(m_mainContext.get(), G_PRIORITY_DEFAULT, reinterpret_cast<GSourceFunc>(+[](gpointer userData) -> gboolean {
        auto data = reinterpret_cast<ResolveAddressData*>(userData);
        auto innerData = createResolveAddressDataInner();
        innerData->callback = WTFMove(data->callback);
        g_resolver_lookup_by_name_async(data->resolver.get(), data->address.utf8().data(), nullptr,
            reinterpret_cast<GAsyncReadyCallback>(+[](GResolver* resolver, GAsyncResult* result, gpointer userData) {
                auto data = reinterpret_cast<ResolveAddressDataInner*>(userData);
                GUniqueOutPtr<GError> error;
                GList* addresses = g_resolver_lookup_by_name_finish(resolver, result, &error.outPtr());
                if (!addresses) {
                    data->callback(makeUnexpected(ExceptionData { ExceptionCode::NetworkError, "Unable to resolve local address"_s }));
                    destroyResolveAddressDataInner(data);
                    return;
                }
                GUniquePtr<char> address(g_inet_address_to_string(G_INET_ADDRESS(addresses->data)));
                data->callback(String::fromUTF8(address.get()));
                g_resolver_free_addresses(addresses);
                destroyResolveAddressDataInner(data);
            }), innerData);
        return G_SOURCE_REMOVE;
    }), data, reinterpret_cast<GDestroyNotify>(destroyResolveAddressData));
}

void GStreamerIceBackendNice::addCandidate(unsigned streamId, const String& candidateSdp, CompletionHandler<void(Expected<bool, WebCore::ExceptionData>&&)>&& completionHandler)
{
    if (candidateSdp.isEmpty()) {
        nice_agent_peer_candidate_gathering_done(m_agent.get(), streamId);
        completionHandler(true);
        return;
    }

    GUniquePtr<NiceCandidate> candidate(nice_agent_parse_remote_candidate_sdp(m_agent.get(), streamId, candidateSdp.utf8().data()));
    if (candidate) {
        addIceCandidateToAgent(m_agent.get(), streamId, *candidate.get());
        completionHandler(true);
        return;
    }

    auto localAddressResult = getCandidateAddress(candidateSdp);
    if (!localAddressResult.has_value()) {
        completionHandler(makeUnexpected(ExceptionData { ExceptionCode::DataError, makeString("Failed to retrieve address from candidate: "_s, localAddressResult.error().message) }));
        return;
    }

    auto localAddress = localAddressResult.value();
    if (!localAddress.address.endsWith(".local"_s)) {
        completionHandler(makeUnexpected(ExceptionData { ExceptionCode::DataError, makeString("Candidate address \""_s, localAddress.address, "\" does not end with '.local'"_s) }));
        return;
    }

    resolveAddress(WTFMove(localAddress.address), [weakAgent = GThreadSafeWeakPtr(m_agent.get()), streamId, completionHandler = WTFMove(completionHandler), prefix = WTFMove(localAddress.prefix), postfix = WTFMove(localAddress.postfix)](auto&& result) mutable {
        if (!result.has_value()) {
            completionHandler(makeUnexpected(ExceptionData { ExceptionCode::DataError, makeString("Failed to resolve local candidate address: "_s, result.error().message) }));
            return;
        }

        GRefPtr agent = weakAgent.get();
        if (!agent) {
            completionHandler(makeUnexpected(ExceptionData { ExceptionCode::InvalidStateError, "ICE agent is gone"_s }));
            return;
        }

        auto address = result.value();
        auto newCandidate = makeString(WTFMove(prefix), ' ', address, ' ', WTFMove(postfix));
        GUniquePtr<NiceCandidate> candidate(nice_agent_parse_remote_candidate_sdp(agent.get(), streamId, newCandidate.utf8().data()));
        if (!candidate) {
            completionHandler(makeUnexpected(ExceptionData { ExceptionCode::DataError, makeString("Could not parse \""_s, newCandidate, '\"')}));
            return;
        }

        addIceCandidateToAgent(agent.get(), streamId, *candidate.get());
        completionHandler(true);
    });
}

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBNICE)
