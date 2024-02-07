/*
 * Copyright (C) 2023 Igalia S.L
 * Copyright (C) 2023 Metrological Group B.V.
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
#include "GStreamerQuirks.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "GStreamerQuirkAmLogic.h"
#include "GStreamerQuirkBcmNexus.h"
#include "GStreamerQuirkBroadcom.h"
#include "GStreamerQuirkRealtek.h"
#include "GStreamerQuirkWesteros.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/OptionSet.h>
#include <wtf/text/StringView.h>

namespace WebCore {

GST_DEBUG_CATEGORY_STATIC(webkit_quirks_debug);
#define GST_CAT_DEFAULT webkit_quirks_debug

GStreamerQuirksManager& GStreamerQuirksManager::singleton()
{
    static NeverDestroyed<GStreamerQuirksManager> sharedInstance;
    return sharedInstance;
}

GStreamerQuirksManager::GStreamerQuirksManager()
{
    GST_DEBUG_CATEGORY_INIT(webkit_quirks_debug, "webkitquirks", 0, "WebKit Quirks");

    // For the time being keep this disabled on non-WPE platforms. GTK on desktop shouldn't require
    // quirks, for instance.
#if !PLATFORM(WPE)
    return;
#endif

    const char* quirksList = g_getenv("WEBKIT_GST_QUIRKS");
    GST_DEBUG("Attempting to parse requested quirks: %s", GST_STR_NULL(quirksList));
    if (!quirksList)
        return;

    StringView quirks { quirksList, static_cast<unsigned>(strlen(quirksList)) };
    if (WTF::equalLettersIgnoringASCIICase(quirks, "help"_s)) {
        WTFLogAlways("Supported quirks for WEBKIT_GST_QUIRKS are: amlogic, broadcom, bcmnexus, realtek, westeros");
        return;
    }

    for (const auto& identifier : quirks.split(',')) {
        std::unique_ptr<GStreamerQuirk> quirk;
        if (WTF::equalLettersIgnoringASCIICase(identifier, "amlogic"_s))
            quirk = std::make_unique<GStreamerQuirkAmLogic>();
        else if (WTF::equalLettersIgnoringASCIICase(identifier, "broadcom"_s))
            quirk = std::make_unique<GStreamerQuirkBroadcom>();
        else if (WTF::equalLettersIgnoringASCIICase(identifier, "bcmnexus"_s))
            quirk = std::make_unique<GStreamerQuirkBcmNexus>();
        else if (WTF::equalLettersIgnoringASCIICase(identifier, "realtek"_s))
            quirk = std::make_unique<GStreamerQuirkRealtek>();
        else if (WTF::equalLettersIgnoringASCIICase(identifier, "westeros"_s))
            quirk = std::make_unique<GStreamerQuirkWesteros>();
        else {
            GST_WARNING("Unknown quirk requested: %s. Skipping", identifier.toStringWithoutCopying().ascii().data());
            continue;
        }

        if (!quirk->isPlatformSupported()) {
            GST_WARNING("Quirk %s was requested but is not supported on this platform. Skipping", quirk->identifier());
            continue;
        }
        m_quirks.append(WTFMove(quirk));
    }
}

bool GStreamerQuirksManager::isEnabled() const
{
    return !m_quirks.isEmpty();
}

GstElement* GStreamerQuirksManager::createWebAudioSink()
{
    for (const auto& quirk : m_quirks) {
        auto* sink = quirk->createWebAudioSink();
        if (!sink)
            continue;

        GST_DEBUG("Using WebAudioSink from quirk %s : %" GST_PTR_FORMAT, quirk->identifier(), sink);
        return sink;
    }

    GST_DEBUG("Quirks didn't specify a WebAudioSink, falling back to default sink");
    return createPlatformAudioSink("music"_s);
}

GstElement* GStreamerQuirksManager::createHolePunchVideoSink(bool isLegacyPlaybin, const MediaPlayer* player)
{
    RELEASE_ASSERT_WITH_MESSAGE(isEnabled(), "createHolePunchVideoSink() should be called only if at least one quirk was requested");
    for (const auto& quirk : m_quirks) {
        auto* sink = quirk->createHolePunchVideoSink(isLegacyPlaybin, player);
        if (!sink)
            continue;

        GST_DEBUG("Using HolePunchSink from quirk %s : %" GST_PTR_FORMAT, quirk->identifier(), sink);
        return sink;
    }

    GST_DEBUG("None of the quirks requested a HolePunchSink");
    return nullptr;
}

void GStreamerQuirksManager::setHolePunchVideoRectangle(GstElement* videoSink, const IntRect& rect)
{
    RELEASE_ASSERT_WITH_MESSAGE(supportsVideoHolePunchRendering(), "setHolePunchVideoRectangle() should be called only if at least one quirk supports HolePunch rendering");
    for (const auto& quirk : m_quirks) {
        if (quirk->setHolePunchVideoRectangle(videoSink, rect))
            return;
    }
}

void GStreamerQuirksManager::configureElement(GstElement* element, OptionSet<ElementRuntimeCharacteristics>&& characteristics)
{
    GST_DEBUG("Configuring element %" GST_PTR_FORMAT, element);
    for (const auto& quirk : m_quirks) {
        if (quirk->configureElement(element, characteristics))
            return;
    }
}

std::optional<bool> GStreamerQuirksManager::isHardwareAccelerated(GstElementFactory* factory) const
{
    for (const auto& quirk : m_quirks) {
        auto result = quirk->isHardwareAccelerated(factory);
        if (!result)
            continue;

        GST_DEBUG("Setting %" GST_PTR_FORMAT " as %s accelerated from quirk %s", factory, quirk->identifier(), *result ? "hardware" : "software");
        return *result;
    }

    return std::nullopt;
}

bool GStreamerQuirksManager::supportsVideoHolePunchRendering() const
{
    for (const auto& quirk : m_quirks) {
        if (quirk->supportsVideoHolePunchRendering()) {
            GST_DEBUG("Quirk %s supports video punch hole rendering", quirk->identifier());
            return true;
        }
    }

    GST_DEBUG("None of the quirks supports video punch hole rendering");
    return false;
}

GstElementFactoryListType GStreamerQuirksManager::audioVideoDecoderFactoryListType() const
{
    for (const auto& quirk : m_quirks) {
        auto result = quirk->audioVideoDecoderFactoryListType();
        if (!result)
            continue;

        GST_DEBUG("Quirk %s requests audio/video decoder factory list override to %" G_GUINT32_FORMAT, quirk->identifier(), static_cast<uint32_t>(*result));
        return *result;
    }

    return GST_ELEMENT_FACTORY_TYPE_DECODER;
}

Vector<String> GStreamerQuirksManager::disallowedWebAudioDecoders() const
{
    Vector<String> result;
    for (const auto& quirk : m_quirks)
        result.appendVector(quirk->disallowedWebAudioDecoders());

    return result;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER)
