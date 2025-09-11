/*
 *  Copyright (C) 2025 Igalia S.L. All rights reserved.
 *  Copyright (C) 2025 Metrological Group B.V.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "GStreamerIceAgent.h"

#if USE(GSTREAMER_WEBRTC) && USE(LIBRICE)

#include "GRefPtrGStreamer.h"
#include "GRefPtrRice.h"
#include "GStreamerIceStream.h"
#include "GStreamerIceUtilities.h"
#include "GStreamerRiceGioBackend.h"
#include "GUniquePtrRice.h"
#include "NotImplemented.h"
#include "ScriptExecutionContext.h"
#include "SocketProvider.h"
#include <gst/webrtc/webrtc.h>
#include <wtf/Condition.h>
#include <wtf/HashSet.h>
#include <wtf/Lock.h>
#include <wtf/MonotonicTime.h>
#include <wtf/RunLoop.h>
#include <wtf/URL.h>
#include <wtf/URLHash.h>
#include <wtf/glib/GThreadSafeWeakPtr.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/WTFString.h>

using namespace WTF;
using namespace WebCore;

typedef struct _WebKitGstRiceStream {
    unsigned sessionId;
    unsigned riceStreamId;
    GRefPtr<GstWebRTCICEStream> stream;
} WebKitGstRiceStream;

typedef struct _WebKitGstIceAgentPrivate {
    RefPtr<GStreamerIceBackendClient> backendClient;
    RefPtr<SocketProvider> socketProvider;
    GRefPtr<RiceAgent> agent;

    Vector<WebKitGstRiceStream> streams;

    RefPtr<Thread> thread;
    GRefPtr<GMainContext> mainContext;
    GRefPtr<GMainLoop> loop;
    Lock lock;
    Condition condition;

    bool agentIsClosed;
    GRefPtr<GstPromise> closePromise;

    GstWebRTCICEOnCandidateFunc onCandidate;
    gpointer onCandidateData;
    GDestroyNotify onCandidateNotify;

    RefPtr<GStreamerIceBackend> iceBackend;

    String stunServer;
    String turnServer;

    HashSet<URL> turnServers;
} WebKitGstIceAgentPrivate;

typedef struct _WebKitGstIceAgent {
    GstWebRTCICE parent;
    WebKitGstIceAgentPrivate* priv;
} WebKitGstIceAgent;

typedef struct _WebKitGstIceAgentClass {
    GstWebRTCICEClass parentClass;
} WebKitGstIceAgentClass;

GST_DEBUG_CATEGORY(webkit_webrtc_rice_debug);
#define GST_CAT_DEFAULT webkit_webrtc_rice_debug

WEBKIT_DEFINE_TYPE_WITH_CODE(WebKitGstIceAgent, webkit_gst_webrtc_ice_backend, GST_TYPE_WEBRTC_ICE, GST_DEBUG_CATEGORY_INIT(webkit_webrtc_rice_debug, "webkitwebrtcrice", 0, "WebRTC Rice ICE backend"))

using namespace WebCore;

static void webkitGstWebRTCIceAgentSetOnIceCandidate(GstWebRTCICE* ice, GstWebRTCICEOnCandidateFunc callback, gpointer userData, GDestroyNotify notifyCallback)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    auto priv = backend->priv;
    if (priv->onCandidateNotify)
        priv->onCandidateNotify(priv->onCandidateData);
    priv->onCandidateNotify = notifyCallback;
    priv->onCandidateData = userData;
    priv->onCandidate = callback;
}

static void webkitGstWebRTCIceAgentSetForceRelay(GstWebRTCICE*, gboolean)
{
    GST_FIXME("Not implemented yet.");
}

static void webkitGstWebRTCIceAgentSetStunServer(GstWebRTCICE* ice, const gchar* uri)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    backend->priv->stunServer = String::fromUTF8(uri);
    GST_INFO_OBJECT(ice, "Setting STUN server address to %s", backend->priv->stunServer.utf8().data());

    URL url(backend->priv->stunServer);
    ASSERT(url.isValid());
    const auto& host = url.host();
    auto port = url.port().value_or(3478);

    backend->priv->iceBackend->resolveAddress(host.toString(), [weakAgent = GThreadSafeWeakPtr(backend), port](auto&& result) mutable {
        auto agent = weakAgent.get();
        if (!agent) [[unlikely]]
            return;
        if (result.hasException()) {
            GST_WARNING_OBJECT(agent.get(), "Unable to configure STUN server on ICE agent: %s", result.exception().message().utf8().data());
            return;
        }
        const auto& iceAgent = agent->priv->agent;
        if (!iceAgent) [[unlikely]]
            return;

        auto& host = result.returnValue();
        auto address = makeString(host, ':', port);
        GST_DEBUG_OBJECT(agent.get(), "STUN server address resolved to %s", address.ascii().data());
        GUniquePtr<RiceAddress> stunAddress(rice_address_new_from_string(address.ascii().data()));
        if (stunAddress) {
            rice_agent_add_stun_server(iceAgent.get(), RICE_TRANSPORT_TYPE_UDP, stunAddress.get());
            rice_agent_add_stun_server(iceAgent.get(), RICE_TRANSPORT_TYPE_TCP, stunAddress.get());
        } else
            GST_WARNING_OBJECT(agent.get(), "Unable to make use of STUN server %s", address.ascii().data());
    });
}

static gchar* webkitGstWebRTCIceAgentGetStunServer(GstWebRTCICE* ice)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    return g_strdup(backend->priv->stunServer.utf8().data());
}

enum class ValidationErrorCode {
    ParseError,
    UnknownScheme,
    UnknownTransport,
    UnknownParameter,
    MissingUsername,
    MissingPassword
};

struct URLValidationError {
    ValidationErrorCode code;
    String data;
};

Expected<URL, URLValidationError> validateTurnServerURL(const String& turnUrl)
{
    URL url(turnUrl);

    if (!url.isValid())
        return makeUnexpected(URLValidationError { ValidationErrorCode::ParseError, { } });

    bool isTLS = false;
    if (url.protocolIs("turns"_s))
        isTLS = true;
    else if (url.protocol() != "turn"_s)
        return makeUnexpected(URLValidationError { ValidationErrorCode::UnknownScheme, url.protocol().toStringWithoutCopying() });

    for (const auto& [key, value] : queryParameters(url)) {
        if (key != "transport"_s)
            return makeUnexpected(URLValidationError { ValidationErrorCode::UnknownParameter, key });
        if (value != "udp"_s && value != "tcp"_s)
            return makeUnexpected(URLValidationError { ValidationErrorCode::UnknownTransport, value });
    }

    if (url.user().isEmpty())
        return makeUnexpected(URLValidationError { ValidationErrorCode::MissingUsername, { } });
    if (url.password().isEmpty())
        return makeUnexpected(URLValidationError { ValidationErrorCode::MissingPassword, { } });

    if (url.port())
        return url;

    if (isTLS)
        url.setPort(5349);
    else
        url.setPort(3478);

    return url;
}

static void addTurnServer(WebKitGstIceAgent* agent, const URL& url)
{
    GST_INFO_OBJECT(agent, "Adding TURN server %s", url.string().utf8().data());
    if (!url.host())
        return;

    std::array<RiceTransportType, 4> relays = { static_cast<RiceTransportType>(0), };
    unsigned nRelay = 0;

    if (url.protocolIs("turns"_s)) {
        GST_FIXME("Not implemented yet.");
        return;
    }

    ASSERT(url.protocolIs("turn"_s));
    StringView transport;
    for (const auto& [key, value] : queryParameters(url)) {
        if (key == "transport"_s) {
            transport = value;
            break;
        }
    }
    if (!transport || transport == "udp"_s)
        relays[nRelay++] = RICE_TRANSPORT_TYPE_UDP;
    if (!transport || transport == "tcp"_s)
        relays[nRelay++] = RICE_TRANSPORT_TYPE_TCP;

    RELEASE_ASSERT(url.port());
    auto port = url.port().value();
    agent->priv->iceBackend->resolveAddress(url.host().toString(), [weakAgent = GThreadSafeWeakPtr(agent), port, nRelay, user = url.user(), password = url.password(), relays = WTFMove(relays)](auto&& result) mutable {
        auto agent = weakAgent.get();
        if (!agent) [[unlikely]]
            return;
        if (result.hasException()) {
            GST_WARNING_OBJECT(agent.get(), "Unable to configure TURN server on ICE agent: %s", result.exception().message().utf8().data());
            return;
        }
        const auto& iceAgent = agent->priv->agent;
        if (!iceAgent) [[unlikely]]
            return;

        auto turnAddressString = makeString(result.returnValue(), ':', port);
        GST_DEBUG_OBJECT(agent.get(), "TURN address resolved to %s", turnAddressString.ascii().data());
        GUniquePtr<RiceAddress> turnAddress(rice_address_new_from_string(turnAddressString.utf8().data()));
        GUniquePtr<RiceCredentials> credentials(rice_credentials_new(g_strdup(user.utf8().data()), g_strdup(password.utf8().data())));
        for (unsigned i = 0; i < nRelay; i++)
            rice_agent_add_turn_server(agent->priv->agent.get(), relays[i], turnAddress.get(), credentials.get());
    });
}

static gboolean webkitGstWebRTCIceAgentAddTurnServer(GstWebRTCICE* ice, const gchar* uri)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    if (!backend->priv->iceBackend)
        return FALSE;

    auto validationResult = validateTurnServerURL(String::fromUTF8(uri));
    if (!validationResult.has_value()) {
        GST_ERROR_OBJECT(ice, "Error validating TURN URI: %s", validationResult.error().data.utf8().data());
        return FALSE;
    }
    auto url = *validationResult;
    auto wasAdded = backend->priv->turnServers.add(url).isNewEntry;
    if (!wasAdded) {
        GST_WARNING_OBJECT(ice, "%s was already registered, no need to add it again", uri);
        return FALSE;
    }

    addTurnServer(backend, url);
    return TRUE;
}

static void webkitGstWebRTCIceAgentSetTurnServer(GstWebRTCICE* ice, const gchar* uri)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    if (!backend->priv->iceBackend)
        return;

    auto validationResult = validateTurnServerURL(String::fromUTF8(uri));
    if (!validationResult.has_value()) {
        GST_ERROR_OBJECT(ice, "Error validating TURN URI: %s", validationResult.error().data.utf8().data());
        return;
    }
    backend->priv->turnServer = String::fromUTF8(uri);
}

static gchar* webkitGstWebRTCIceAgentGetTurnServer(GstWebRTCICE* ice)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    return g_strdup(backend->priv->turnServer.utf8().data());
}

static GstWebRTCICEStream* webkitGstWebRTCIceAgentAddStream(GstWebRTCICE* ice, guint sessionId)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    if (!backend->priv->iceBackend)
        return nullptr;

    auto riceStream = adoptGRef(rice_agent_add_stream(backend->priv->agent.get()));
    auto streamId = static_cast<unsigned>(rice_stream_get_id(riceStream.get()));
    [[maybe_unused]] auto component = adoptGRef(rice_stream_add_component(riceStream.get()));

    auto stream = GST_WEBRTC_ICE_STREAM(webkitGstWebRTCCreateIceStream(backend, WTFMove(riceStream)));
    backend->priv->streams.append({ sessionId, streamId, GRefPtr(stream) });
    return stream;
}

static gboolean webkitGstWebRTCIceAgentGetIsController(GstWebRTCICE* ice)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    return static_cast<gboolean>(rice_agent_get_controlling(backend->priv->agent.get()));
}

static void webkitGstWebRTCIceAgentSetIsController(GstWebRTCICE*, gboolean)
{
    GST_FIXME("Not implemented yet.");
}

struct CandidateAddress {
    String prefix;
    String address;
    String postfix;
};

static Expected<CandidateAddress, ExceptionData> getCandidateAddress(StringView candidate)
{
    if (!candidate.startsWith("a=candidate:"_s))
        return makeUnexpected(ExceptionData { ExceptionCode::NotSupportedError, "Candidate does not start with \"a=candidate:\""_s });

    auto tokens = candidate.toStringWithoutCopying().substring(12).split(' ');
    if (tokens.size() < 6)
        return makeUnexpected(ExceptionData { ExceptionCode::DataError, makeString("Candidate \""_s, candidate, "\" tokenization resulted in not enough tokens"_s) });

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

static void webkitGstWebRTCIceAgentAddCandidate(GstWebRTCICE* ice, GstWebRTCICEStream* iceStream, const gchar* candidateSdp, GstPromise* promise)
{
    GRefPtr riceStream = webkitGstWebRTCIceStreamGetRiceStream(WEBKIT_GST_WEBRTC_ICE_STREAM(iceStream));
    if (!riceStream) [[unlikely]]
        return;

    if (!candidateSdp) {
        rice_stream_end_of_remote_candidates(riceStream.get());
        return;
    }

    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    GUniquePtr<RiceCandidate> candidate(rice_candidate_new_from_sdp_string(candidateSdp));
    if (candidate) {
        rice_stream_add_remote_candidate(riceStream.get(), candidate.get());
        g_main_context_wakeup(backend->priv->mainContext.get());
        return;
    }

    auto iceBackend = backend->priv->iceBackend;
    if (!iceBackend) [[unlikely]]
        return;

    auto localAddressResult = getCandidateAddress(StringView::fromLatin1(candidateSdp));
    if (!localAddressResult.has_value()) {
        auto errorMessage = makeString("Failed to retrieve address from candidate: "_s, localAddressResult.error().message);
        GUniquePtr<GError> error(g_error_new(GST_WEBRTC_ERROR, GST_WEBRTC_ERROR_INTERNAL_FAILURE, "%s", errorMessage.utf8().data()));
        gst_promise_reply(promise, gst_structure_new("application/x-gst-promise", "error", error.get(), nullptr));
        return;
    }

    auto localAddress = localAddressResult.value();
    if (!localAddress.address.endsWith(".local"_s)) {
        auto errorMessage = makeString("Candidate address \""_s, localAddress.address, "\" does not end with '.local'"_s);
        GUniquePtr<GError> error(g_error_new(GST_WEBRTC_ERROR, GST_WEBRTC_ERROR_INTERNAL_FAILURE, "%s", errorMessage.utf8().data()));
        gst_promise_reply(promise, gst_structure_new("application/x-gst-promise", "error", error.get(), nullptr));
        return;
    }

    iceBackend->resolveAddress(WTFMove(localAddress.address), [promise = GRefPtr(promise), riceStream = WTFMove(riceStream), prefix = WTFMove(localAddress.prefix), postfix = WTFMove(localAddress.postfix)](auto&& result) mutable {
        if (result.hasException()) {
            auto& errorMessage = result.exception().message();
            GUniquePtr<GError> error(g_error_new(GST_WEBRTC_ERROR, GST_WEBRTC_ERROR_INTERNAL_FAILURE, "%s", errorMessage.utf8().data()));

            gst_promise_reply(promise.get(), gst_structure_new("application/x-gst-promise", "error", error.get(), nullptr));
            return;
        }

        auto newCandidateSdp = makeString(WTFMove(prefix), ' ', result.returnValue(), ' ', WTFMove(postfix));
        GUniquePtr<RiceCandidate> newCandidate(rice_candidate_new_from_sdp_string(newCandidateSdp.utf8().data()));
        if (newCandidate)
            rice_stream_add_remote_candidate(riceStream.get(), newCandidate.get());
    });
}

static GstWebRTCICETransport* webkitGstWebRTCIceAgentFindTransport(GstWebRTCICE*, GstWebRTCICEStream* stream, GstWebRTCICEComponent component)
{
    return webkitGstWebRTCIceStreamFindTransport(stream, component);
}

static void webkitGstWebRTCIceAgentSetTos(GstWebRTCICE*, GstWebRTCICEStream*, guint)
{
    GST_FIXME("Not implemented yet.");
}

static gboolean webkitGstWebRTCIceAgentSetLocalCredentials(GstWebRTCICE*, GstWebRTCICEStream* stream, const gchar* ufrag, const gchar* pwd)
{
    webkitGstWebRTCIceStreamSetLocalCredentials(WEBKIT_GST_WEBRTC_ICE_STREAM(stream), String::fromLatin1(ufrag), String::fromLatin1(pwd));
    return TRUE;
}

static gboolean webkitGstWebRTCIceAgentSetRemoteCredentials(GstWebRTCICE*, GstWebRTCICEStream* stream, const gchar* ufrag, const gchar* pwd)
{
    webkitGstWebRTCIceStreamSetRemoteCredentials(WEBKIT_GST_WEBRTC_ICE_STREAM(stream), String::fromLatin1(ufrag), String::fromLatin1(pwd));
    return TRUE;
}

static gboolean webkitGstWebRTCIceAgentGatherCandidates(GstWebRTCICE*, GstWebRTCICEStream* stream)
{
    return webkitGstWebRTCIceStreamGatherCandidates(WEBKIT_GST_WEBRTC_ICE_STREAM(stream));
}

static void webkitGstWebRTCIceAgentSetHttpProxy(GstWebRTCICE*, const gchar*)
{
    GST_FIXME("Not implemented yet.");
}

static gchar* webkitGstWebRTCIceAgentGetHttpProxy(GstWebRTCICE*)
{
    GST_FIXME("Not implemented yet.");
    return nullptr;
}

static ASCIILiteral getRelayProtocol(WebKitGstIceAgent* agent)
{
    if (agent->priv->turnServer.isEmpty())
        return "none"_s;

    URL url(agent->priv->turnServer);
    if (url.protocolIs("turns"_s))
        return "tls"_s;

    ASSERT(url.protocolIs("turn"_s));
    StringView transport;
    for (const auto& [key, value] : queryParameters(url)) {
        if (key == "transport"_s) {
            transport = value;
            break;
        }
    }
    if (!transport || transport == "udp"_s)
        return "udp"_s;
    if (!transport || transport == "tcp"_s)
        return "tcp"_s;

    return "none"_s;
}

static gboolean webkitGstWebRTCIceAgentGetSelectedPair(GstWebRTCICE* ice,
    GstWebRTCICEStream* stream, GstWebRTCICECandidateStats** localStats,
    GstWebRTCICECandidateStats** remoteStats)
{
    if (!stream)
        return FALSE;

    auto result = webkitGstWebRTCIceStreamGetSelectedPair(WEBKIT_GST_WEBRTC_ICE_STREAM(stream), localStats, remoteStats);
    if (!result)
        return result;

    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);
    auto relayProtocol = getRelayProtocol(backend);
    (*localStats)->relay_proto = relayProtocol;
    (*remoteStats)->relay_proto = relayProtocol;
    return TRUE;
}

void webkitGstWebRTCIceAgentClosed(WebKitGstIceAgent* agent)
{
    agent->priv->agentIsClosed = true;

    if (!agent->priv->closePromise)
        return;

    gst_promise_reply(agent->priv->closePromise.get(), nullptr);
    agent->priv->closePromise.clear();
}

static void webkitGstWebRTCIceAgentClose(GstWebRTCICE* ice, GstPromise* promise)
{
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(ice);

    backend->priv->closePromise = adoptGRef(promise);
    auto now = WTF::MonotonicTime::now().secondsSinceEpoch();
    rice_agent_close(backend->priv->agent.get(), now.nanoseconds());

    if (backend->priv->closePromise)
        return;

    while (!backend->priv->agentIsClosed)
        g_main_context_iteration(backend->priv->mainContext.get(), TRUE);
}

static void webkitGstWebRTCIceAgentDispose(GObject* object)
{
    gst_printerrln("dispose agent %p", object);
#if !GST_CHECK_VERSION(1, 27, 0)
    webkitGstWebRTCIceAgentClose(GST_WEBRTC_ICE(object), nullptr);
#endif
    G_OBJECT_CLASS(webkit_gst_webrtc_ice_backend_parent_class)->dispose(object);
}

static void webkitGstWebRTCIceAgentFinalize(GObject* object)
{
    gst_printerrln("finalize agent %p", object);
    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(object);
    {
        Locker locker(backend->priv->lock);
        g_main_loop_quit(backend->priv->loop.get());
        while (backend->priv->loop)
            backend->priv->condition.wait(backend->priv->lock);
    }

    if (backend->priv->onCandidateNotify)
        backend->priv->onCandidateNotify(backend->priv->onCandidateData);
    G_OBJECT_CLASS(webkit_gst_webrtc_ice_backend_parent_class)->finalize(object);
}

static void webkitGstWebRTCIceAgentConstructed(GObject* object)
{
    G_OBJECT_CLASS(webkit_gst_webrtc_ice_backend_parent_class)->constructed(object);

    auto backend = WEBKIT_GST_WEBRTC_ICE_BACKEND(object);
    auto priv = backend->priv;

    priv->agentIsClosed = false;

    static Atomic<uint32_t> counter = 0;
    auto id = counter.load();
    auto threadName = makeString("webrtc-rice-"_s, id);
    counter.exchangeAdd(1);

    {
        Locker locker(priv->lock);
        priv->thread = Thread::create(ASCIILiteral::fromLiteralUnsafe(threadName.ascii().data()), [&] {
            Locker locker(priv->lock);
            priv->mainContext = adoptGRef(g_main_context_new());
            priv->loop = adoptGRef(g_main_loop_new(priv->mainContext.get(), FALSE));
            priv->condition.notifyAll();
            g_main_context_invoke(priv->mainContext.get(), reinterpret_cast<GSourceFunc>(+[](gpointer data) -> gboolean {
                reinterpret_cast<Locker<Lock>*>(data)->unlockEarly();
                return G_SOURCE_REMOVE;
            }), &locker);

            g_main_context_push_thread_default(priv->mainContext.get());
            g_main_loop_run(priv->loop.get());
            g_main_context_pop_thread_default(priv->mainContext.get());

            {
                Locker locker(priv->lock);
                priv->loop = nullptr;
                priv->condition.notifyAll();
            }
        });
        priv->thread->detach();

        while (!priv->loop)
            priv->condition.wait(priv->lock);
    }

    priv->agent = adoptGRef(rice_agent_new(true, true));
}

static void webkitGstWebRTCIceAgentConfigure(WebKitGstIceAgent* backend, RefPtr<SocketProvider>&& socketProvider)
{
    auto priv = backend->priv;
    priv->socketProvider = WTFMove(socketProvider);
    priv->backendClient = GStreamerIceBackendClient::create();
    priv->iceBackend = priv->socketProvider->createGStreamerIceBackend(*priv->backendClient);
    priv->backendClient->setIncomingDataCallback([weakThis = GThreadSafeWeakPtr(backend)](unsigned streamId, RTCIceProtocol protocol, String&& from, String&& to, std::span<const uint8_t>&& data) mutable {
        auto self = weakThis.get();
        if (!self)
            return;
        for (const auto& stream : self->priv->streams) {
            if (stream.riceStreamId == streamId) {
                webkitGstWebRTCIceStreamHandleIncomingData(WEBKIT_GST_WEBRTC_ICE_STREAM(stream.stream.get()), protocol, WTFMove(from), WTFMove(to), WTFMove(data));
                return;
            }
        }
    });

    auto source = adoptGRef(agent_source_new(GThreadSafeWeakPtr(backend)));
    g_source_attach(source.get(), priv->mainContext.get());
}

static void webkit_gst_webrtc_ice_backend_class_init(WebKitGstIceAgentClass* klass)
{
    auto gobjectClass = G_OBJECT_CLASS(klass);
    gobjectClass->constructed = webkitGstWebRTCIceAgentConstructed;
    gobjectClass->dispose = webkitGstWebRTCIceAgentDispose;
    gobjectClass->finalize = webkitGstWebRTCIceAgentFinalize;

    auto iceClass = GST_WEBRTC_ICE_CLASS(klass);
    iceClass->set_on_ice_candidate = webkitGstWebRTCIceAgentSetOnIceCandidate;
    iceClass->set_force_relay = webkitGstWebRTCIceAgentSetForceRelay;
    iceClass->set_stun_server = webkitGstWebRTCIceAgentSetStunServer;
    iceClass->get_stun_server = webkitGstWebRTCIceAgentGetStunServer;
    iceClass->add_turn_server = webkitGstWebRTCIceAgentAddTurnServer;
    iceClass->add_stream = webkitGstWebRTCIceAgentAddStream;
    iceClass->get_is_controller = webkitGstWebRTCIceAgentGetIsController;
    iceClass->set_is_controller = webkitGstWebRTCIceAgentSetIsController;
    iceClass->add_candidate = webkitGstWebRTCIceAgentAddCandidate;
    iceClass->find_transport = webkitGstWebRTCIceAgentFindTransport;
    iceClass->gather_candidates = webkitGstWebRTCIceAgentGatherCandidates;
    iceClass->get_turn_server = webkitGstWebRTCIceAgentGetTurnServer;
    iceClass->set_turn_server = webkitGstWebRTCIceAgentSetTurnServer;
    iceClass->set_tos = webkitGstWebRTCIceAgentSetTos;
    iceClass->set_local_credentials = webkitGstWebRTCIceAgentSetLocalCredentials;
    iceClass->set_remote_credentials = webkitGstWebRTCIceAgentSetRemoteCredentials;
    iceClass->set_http_proxy = webkitGstWebRTCIceAgentSetHttpProxy;
    iceClass->get_http_proxy = webkitGstWebRTCIceAgentGetHttpProxy;
    iceClass->get_selected_pair = webkitGstWebRTCIceAgentGetSelectedPair;
    // TODO:
    // - get_local_candidates
    // - get_remote_candidates
#if GST_CHECK_VERSION(1, 27, 0)
    iceClass->close = webkitGstWebRTCIceAgentClose;
#endif
}

WebKitGstIceAgent* webkitGstWebRTCCreateIceAgent(StringView name, ScriptExecutionContext* context)
{
    if (!context)
        return nullptr;

    RefPtr socketProvider = context->socketProvider();
    if (!socketProvider)
        return nullptr;

    auto agent = reinterpret_cast<WebKitGstIceAgent*>(g_object_new(WEBKIT_TYPE_GST_WEBRTC_ICE_BACKEND, "name", name.toStringWithoutCopying().ascii().data(), nullptr));
    gst_object_ref_sink(agent);
    webkitGstWebRTCIceAgentConfigure(agent, WTFMove(socketProvider));
    return agent;
}

const GRefPtr<RiceAgent>& webkitGstWebRTCIceAgentGetRiceAgent(WebKitGstIceAgent* agent)
{
    return agent->priv->agent;
}

Vector<String> webkitGstWebRTCIceAgentGatherSocketAddresses(WebKitGstIceAgent* agent, unsigned streamId)
{
    auto backend = agent->priv->iceBackend;
    if (!backend)
        return { };

    return backend->gatherSocketAddresses(streamId);
}

GstWebRTCICETransport* webkitGstWebRTCIceAgentCreateTransport(WebKitGstIceAgent* agent, GThreadSafeWeakPtr<WebKitGstIceStream>&& stream, RTCIceComponent component)
{
    if (!agent->priv->iceBackend)
        return nullptr;

    GstWebRTCICEComponent gstComponent;
    switch (component) {
    case RTCIceComponent::Rtp:
        gstComponent = GST_WEBRTC_ICE_COMPONENT_RTP;
        break;
    case RTCIceComponent::Rtcp:
        gstComponent = GST_WEBRTC_ICE_COMPONENT_RTCP;
        break;
    };
    auto isController = webkitGstWebRTCIceAgentGetIsController(GST_WEBRTC_ICE(agent));
    return GST_WEBRTC_ICE_TRANSPORT(webkitGstWebRTCCreateIceTransport(agent, WTFMove(stream), gstComponent, isController));
}

void webkitGstWebRTCIceAgentSend(WebKitGstIceAgent* agent, unsigned streamId, RTCIceProtocol protocol, String from, String to, std::span<const uint8_t> data)
{
    auto backend = agent->priv->iceBackend;
    if (!backend)
        return;

    backend->send(streamId, protocol, from, to, data);
}

void webkitGstWebRTCIceAgentWakeup(WebKitGstIceAgent* agent)
{
    g_main_context_wakeup(agent->priv->mainContext.get());
}

void webkitGstWebRTCIceAgentFinalizeStream(WebKitGstIceAgent* agent, unsigned streamId)
{
    auto backend = agent->priv->iceBackend;
    if (!backend)
        return;

    backend->finalizeStream(streamId);

    agent->priv->streams.removeAllMatching([streamId](const auto& item) {
        return item.riceStreamId == streamId;
    });
}

static void findStreamAndApply(const Vector<WebKitGstRiceStream>& streams, unsigned streamId, Function<void(const WebKitGstRiceStream&)> callback)
{
    auto index = streams.findIf([streamId](const auto& item) {
        return item.riceStreamId == streamId;
    });
    if (index == notFound) [[unlikely]]
        return;
    callback(streams[index]);
}

void webkitGstWebRTCIceAgentGatheringDoneForStream(WebKitGstIceAgent* agent, unsigned streamId)
{
    findStreamAndApply(agent->priv->streams, streamId, [](const auto& stream) {
        webkitGstWebRTCIceStreamGatheringDone(WEBKIT_GST_WEBRTC_ICE_STREAM(stream.stream.get()));
    });
}

void webkitGstWebRTCIceAgentLocalCandidateGatheredForStream(WebKitGstIceAgent* agent, unsigned streamId, RiceAgentGatheredCandidate& candidate)
{
    findStreamAndApply(agent->priv->streams, streamId, [&](const auto& stream) {
        GUniquePtr<char> sdpCandidate(rice_candidate_to_sdp_string(&candidate.gathered.candidate));
        agent->priv->onCandidate(GST_WEBRTC_ICE(agent), streamId, sdpCandidate.get(), agent->priv->onCandidateData);
        webkitGstWebRTCIceStreamAddLocalGatheredCandidate(WEBKIT_GST_WEBRTC_ICE_STREAM(stream.stream.get()), candidate.gathered);
    });
}

void webkitGstWebRTCIceAgentNewSelectedPairForStream(WebKitGstIceAgent* agent, unsigned streamId, RiceAgentSelectedPair& selectedPair)
{
    findStreamAndApply(agent->priv->streams, streamId, [&](const auto& stream) {
        webkitGstWebRTCIceStreamNewSelectedPair(WEBKIT_GST_WEBRTC_ICE_STREAM(stream.stream.get()), selectedPair);
    });
}

void webkitGstWebRTCIceAgentComponentStateChangedForStream(WebKitGstIceAgent* agent, unsigned streamId, RiceAgentComponentStateChange& change)
{
    findStreamAndApply(agent->priv->streams, streamId, [&](const auto& stream) {
        webkitGstWebRTCIceStreamComponentStateChanged(WEBKIT_GST_WEBRTC_ICE_STREAM(stream.stream.get()), change);
    });
}

#undef GST_CAT_DEFAULT

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBRICE)
