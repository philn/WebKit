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

#pragma once

#if USE(LIBRICE)

#include <WebCore/ExceptionData.h>
#include <WebCore/ExceptionOr.h>
#include <WebCore/GRefPtrRice.h>
#include <WebCore/GUniquePtrRice.h>
#include <WebCore/RTCIceComponent.h>
#include <WebCore/RTCIceProtocol.h>
#include <wtf/Expected.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/RunLoop.h>
#include <wtf/URL.h>
#include <wtf/URLHash.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace IPC {
class Connection;
}

namespace WebKit {

class GStreamerIceBackendRice {
public:
    GStreamerIceBackendRice();
    ~GStreamerIceBackendRice();

    using ResolveCallback = CompletionHandler<void(Expected<String, WebCore::ExceptionData>&&)>;
    void resolveAddress(const String&, ResolveCallback&&);

    void sendData(unsigned, WebCore::RTCIceProtocol, String, String, std::span<const uint8_t>);
    void finalizeStream(unsigned);

    void gatherSocketAddresses(unsigned, CompletionHandler<void(Vector<String>&&)>&&);

    GRefPtr<RiceSockets> getSocketsForStream(unsigned);
    GRefPtr<GSource> getRecvSourceForStream(unsigned);

    void notifyIncomingData(unsigned streamId, WebCore::RTCIceProtocol, String&&, String&&, std::span<const uint8_t>&&);

private:
    virtual IPC::Connection* connection() const = 0;
    virtual uint64_t destination() const = 0;

    RefPtr<RunLoop> m_runLoop;

    struct SocketData {
        GRefPtr<RiceSockets> sockets;
        GRefPtr<GSource> source;
    };
    HashMap<unsigned, SocketData, WTF::IntHash<unsigned>, WTF::UnsignedWithZeroKeyHashTraits<unsigned>> m_sockets;

    HashMap<unsigned, Vector<GUniquePtr<RiceAddress>>, WTF::IntHash<unsigned>, WTF::UnsignedWithZeroKeyHashTraits<unsigned>> m_udpAddresses;

    HashMap<String, GUniquePtr<RiceAddress>> m_addressCache;

    const RiceAddress* ensureRiceAddressFromCache(const String&);
};

} // namespace WebKit

#endif // USE(LIBRICE)
