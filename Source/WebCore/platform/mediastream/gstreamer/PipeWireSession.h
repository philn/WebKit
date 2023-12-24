/*
 * Copyright (C) 2024 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <pipewire/core.h>
#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>
#include <wtf/Forward.h>
#include <wtf/RefCounted.h>
#include <wtf/WeakPtr.h>

#include "GRefPtrGStreamer.h"

namespace WebCore {
class PipeWireNode;

struct PipeWireNodeData {
    WTF_MAKE_STRUCT_FAST_ALLOCATED;

    PipeWireNodeData(uint32_t objectId)
        : objectId(objectId)
        , persistentId(emptyString())
        , fd(-1)
        , path(emptyString())
        , label(emptyString())
    {
    }

    uint32_t objectId;
    String persistentId;
    int fd;
    GRefPtr<GstCaps> caps;
    String path;
    String label;
};

class PipeWireSession: public ThreadSafeRefCounted<PipeWireSession>, public CanMakeWeakPtr<PipeWireSession> {
    WTF_MAKE_FAST_ALLOCATED;
 public:

    static RefPtr<PipeWireSession> create(int fd);
    ~PipeWireSession();

    Vector<PipeWireNodeData> run();

    void finalSync();

private:
    PipeWireSession(struct pw_thread_loop*, struct pw_context*, struct pw_core*);

    void sync();

    struct pw_thread_loop* m_loop;
    struct pw_context* m_context;
    struct pw_core* m_core;
    struct spa_hook m_coreListener;
    int m_lastSeq;
    int m_pendingSeq;
    int m_lastError;

    struct pw_registry* m_registry { nullptr };
    struct spa_hook m_registryListener;
    int m_seq { 0 };
    bool m_loopDone { false };
    HashMap<uint32_t, std::unique_ptr<PipeWireNode>> m_nodes;
};

class PipeWireNode {
    WTF_MAKE_FAST_ALLOCATED;

public:
    PipeWireNode(uint32_t, struct pw_registry*, RefPtr<PipeWireSession>&&);
    ~PipeWireNode();

    const PipeWireNodeData& data() const { return m_data; }

private:
    RefPtr<PipeWireSession> m_session;
    PipeWireNodeData m_data;
    pw_proxy* m_proxy;
    spa_hook m_nodeListener;
};

} // namespace WebCore
