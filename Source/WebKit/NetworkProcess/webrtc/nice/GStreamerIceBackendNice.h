
#pragma once

#if USE(GSTREAMER_WEBRTC) && USE(LIBNICE)

#include "GRefPtrNice.h"
#include "GUniquePtrNice.h"

#include <WebCore/ExceptionData.h>
#include <WebCore/ExceptionOr.h>
#include <wtf/Condition.h>
#include <wtf/Expected.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/Lock.h>
#include <wtf/RunLoop.h>
#include <wtf/URL.h>
#include <wtf/URLHash.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

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
    void addTurnServer(const String&, CompletionHandler<void(Expected<bool, WebCore::ExceptionData>&&)>&&);
    void addStream(unsigned, CompletionHandler<void(std::optional<unsigned>)>&&);
    void gatherCandidatesForStream(unsigned, CompletionHandler<void(bool)>&&);
    void setIsController(bool);
    void addCandidate(unsigned, const String&, CompletionHandler<void(Expected<bool, WebCore::ExceptionData>&&)>&&);

private:
    virtual IPC::Connection* connection() const = 0;

    void notifyNewCandidate(const NiceCandidate&);

    void fillLocalCandidateCredentials(const NiceCandidate&, GUniqueOutPtr<NiceCandidate>&);

    struct CandidateAddress {
        String prefix;
        String address;
        String postfix;
    };
    Expected<CandidateAddress, WebCore::ExceptionData> getCandidateAddress(StringView candidate);
    static void addIceCandidateToAgent(NiceAgent*, unsigned, NiceCandidate&);
    void resolveAddress(String&&, CompletionHandler<void(Expected<String, WebCore::ExceptionData>&&)>&&);

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
    Expected<URL, URLValidationError> validateTurnServerURL(const String&);
    void addTurnServerForStream(unsigned, const URL&);

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
    HashSet<URL> m_turnServers;
};

} // namespace WebKit

#endif // USE(GSTREAMER_WEBRTC) && USE(LIBNICE)
