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

#include "config.h"
#include "WebResourceClient.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebResourceClient);

RefPtr<WebResourceClient> WebResourceClient::create(WebResourceClientParent& parent, PlatformMediaResourceLoader& loader, ResourceRequest&& request)
{
    auto resource = loader.requestResource(WTF::move(request), PlatformMediaResourceLoader::LoadOption::DisallowCaching);
    if (!resource)
        return nullptr;
    auto client = adoptRef(*new WebResourceClient { parent, Ref { *resource } });
    auto result = client.copyRef();
    resource->setClient(WTF::move(client));
    return result;
}

WebResourceClient::WebResourceClient(WebResourceClientParent& parent, Ref<PlatformMediaResource>&& resource)
    : m_parent(parent)
    , m_resource(WTF::move(resource))
{
}

void WebResourceClient::stop()
{
    if (!m_resource)
        return;

    auto resource = WTF::move(m_resource);
    resource->shutdown();
}

void WebResourceClient::responseReceived(PlatformMediaResource& resource, const ResourceResponse& response, CompletionHandler<void(ShouldContinuePolicyCheck)>&& completionHandler)
{
    RefPtr parent = m_parent.get();
    if (parent) {
        parent->responseReceived(resource, response);
        parent->dataLengthReceived(response.expectedContentLength());
    }
    completionHandler(parent ? ShouldContinuePolicyCheck::Yes : ShouldContinuePolicyCheck::No);
}

void WebResourceClient::redirectReceived(PlatformMediaResource& resource, ResourceRequest&& request, const ResourceResponse& response, CompletionHandler<void(ResourceRequest&&)>&& completionHandler)
{
    RefPtr parent = m_parent.get();
    if (parent)
        parent->redirectReceived(resource, response);
    completionHandler(WTF::move(request));
}

void WebResourceClient::dataReceived(PlatformMediaResource&, const SharedBuffer& buffer)
{
    if (RefPtr parent = m_parent.get())
        parent->dataReceived(buffer);
}

void WebResourceClient::loadFailed(PlatformMediaResource&, const ResourceError& error)
{
    if (RefPtr parent = m_parent.get())
        parent->loadFailed(error);
}

void WebResourceClient::loadFinished(PlatformMediaResource&, const NetworkLoadMetrics&)
{
    if (RefPtr parent = m_parent.get())
        parent->loadFinished();
}

} // namespace WebCore

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
