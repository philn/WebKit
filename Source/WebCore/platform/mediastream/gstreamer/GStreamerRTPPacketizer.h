/*
 *  Copyright (C) 2024 Igalia S.L. All rights reserved.
 *  Copyright (C) 2024 Metrological Group B.V.
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

#if USE(GSTREAMER_WEBRTC)

#include "GRefPtrGStreamer.h"
#include "GUniquePtrGStreamer.h"
#include <wtf/Condition.h>
#include <wtf/RefCounted.h>

namespace WebCore {

class GStreamerRTPPacketizer : public RefCounted<GStreamerRTPPacketizer> {
    WTF_MAKE_NONCOPYABLE(GStreamerRTPPacketizer);
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit GStreamerRTPPacketizer(GRefPtr<GstElement>&& encoder, GRefPtr<GstElement>&& payloader, GUniquePtr<GstStructure>&& encodingParameters);
    ~GStreamerRTPPacketizer();

    GstElement* bin() const { return m_bin.get(); }
    GstElement* payloader() const { return m_payloader.get(); }

    void stop();

    String rtpStreamId() const;
    int payloadType() const;

protected:
    GRefPtr<GstElement> m_bin;
    GRefPtr<GstElement> m_inputQueue;
    GRefPtr<GstElement> m_outputQueue;
    GRefPtr<GstElement> m_encoder;
    GRefPtr<GstElement> m_payloader;
    GRefPtr<GstElement> m_capsFilter;

    // std::optional<unsigned> m_rtpStreamId;
    GUniquePtr<GstStructure> m_encodingParameters;
    int m_payloadType;

private:
    struct PayloaderState {
        unsigned seqnum;
    };
    std::optional<PayloaderState> m_payloaderState;

    unsigned long m_padBlockedProbe { 0 };
    Lock m_eosLock;
    Condition m_eosCondition WTF_GUARDED_BY_LOCK(m_eosLock);
};

} // namespace WebCore

#endif // USE(GSTREAMER_WEBRTC)
