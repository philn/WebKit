/*
 * Copyright (C) 2026 Igalia S.L
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

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "PlatformMediaResourceLoader.h"
#include <wtf/AbstractThreadSafeRefCountedAndCanMakeWeakPtr.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class WebResourceClientParent : public AbstractThreadSafeRefCountedAndCanMakeWeakPtr {
public:
    virtual ~WebResourceClientParent() = default;

    virtual void responseReceived(PlatformMediaResource&, const ResourceResponse&) = 0;
    virtual void redirectReceived(PlatformMediaResource&, const ResourceResponse&) = 0;
    virtual void dataReceived(const SharedBuffer&) = 0;
    virtual void loadFailed(const ResourceError&) = 0;
    virtual void loadFinished() = 0;
    virtual void dataLengthReceived(size_t) = 0;
};

class WebResourceClient final
    : public PlatformMediaResourceClient {
    WTF_MAKE_TZONE_ALLOCATED(WebResourceClient);
public:
    static RefPtr<WebResourceClient> create(WebResourceClientParent&, PlatformMediaResourceLoader&, ResourceRequest&&);
    ~WebResourceClient() { stop(); }

    void stop();

private:
    WebResourceClient(WebResourceClientParent&, Ref<PlatformMediaResource>&&);

    void responseReceived(PlatformMediaResource&, const ResourceResponse&, CompletionHandler<void(ShouldContinuePolicyCheck)>&&) final;
    void redirectReceived(PlatformMediaResource&, ResourceRequest&&, const ResourceResponse&, CompletionHandler<void(ResourceRequest&&)>&&) final;
    void dataReceived(PlatformMediaResource&, const SharedBuffer&) final;
    void loadFailed(PlatformMediaResource&, const ResourceError&) final;
    void loadFinished(PlatformMediaResource&, const NetworkLoadMetrics&) final;

    ThreadSafeWeakPtr<WebResourceClientParent> m_parent;
    RefPtr<PlatformMediaResource> m_resource;
};

} // namespace WebCore

#endif //  ENABLE(VIDEO) && USE(GSTREAMER)
