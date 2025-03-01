/*
 * Copyright (C) 2025 Igalia S.L
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
#include "MediaPlayerPrivateGStreamerHarness.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GStreamerEMEUtilities.h"
#include "GStreamerRegistryScanner.h"
#include "Logging.h"

GST_DEBUG_CATEGORY(webkit_media_player_harness_debug);
#define GST_CAT_DEFAULT webkit_media_player_harness_debug

namespace WebCore {

class MediaPlayerFactoryGStreamerHarness final : public MediaPlayerFactory {
private:
    MediaPlayerEnums::MediaEngineIdentifier identifier() const final { return MediaPlayerEnums::MediaEngineIdentifier::GStreamer; };

    Ref<MediaPlayerPrivateInterface> createMediaEnginePlayer(MediaPlayer* player) const final
    {
        return MediaPlayerPrivateGStreamerHarness::create(*player);
    }

    void getSupportedTypes(HashSet<String>& types) const final
    {
        return MediaPlayerPrivateGStreamerHarness::getSupportedTypes(types);
    }

    MediaPlayer::SupportsType supportsTypeAndCodecs(const MediaEngineSupportParameters& parameters) const final
    {
        return MediaPlayerPrivateGStreamerHarness::supportsType(parameters);
    }

    bool supportsKeySystem(const String& keySystem, const String& mimeType) const final
    {
        return MediaPlayerPrivateGStreamerHarness::supportsKeySystem(keySystem, mimeType);
    }
};

void MediaPlayerPrivateGStreamerHarness::registerMediaEngine(MediaEngineRegistrar registrar)
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_media_player_harness_debug, "webkitmediaplayerharness", 0, "WebKit media player");
    });

    auto useHarnessPlayer = StringView::fromLatin1(std::getenv("WEBKIT_GST_USE_PLAYER_HARNESS"));
    if (useHarnessPlayer.isEmpty() || useHarnessPlayer != "1"_s)
        return;

    registrar(makeUnique<MediaPlayerFactoryGStreamerHarness>());
}

bool MediaPlayerPrivateGStreamerHarness::supportsKeySystem(const String& keySystem, [[maybe_unused]] const String& mimeType)
{
    bool result = false;

#if ENABLE(ENCRYPTED_MEDIA)
    result = GStreamerEMEUtilities::isClearKeyKeySystem(keySystem);
#endif

    GST_DEBUG("checking for KeySystem support with %s and type %s: %s", keySystem.utf8().data(), mimeType.utf8().data(), boolForPrinting(result));
    return result;
}

void MediaPlayerPrivateGStreamerHarness::getSupportedTypes(HashSet<String>& types)
{
    GStreamerRegistryScanner::getSupportedDecodingTypes(types);
}

MediaPlayer::SupportsType MediaPlayerPrivateGStreamerHarness::supportsType(const MediaEngineSupportParameters& parameters)
{
    MediaPlayer::SupportsType result = MediaPlayer::SupportsType::IsNotSupported;
#if ENABLE(MEDIA_SOURCE)
    // MediaPlayerPrivateGStreamerMSE is in charge of mediasource playback, not us.
    if (parameters.isMediaSource)
        return result;
#endif

    if (parameters.isMediaStream) {
#if ENABLE(MEDIA_STREAM)
        return MediaPlayer::SupportsType::IsSupported;
#else
        return result;
#endif
    }

    if (parameters.type.isEmpty())
        return result;

    // This player doesn't support pictures rendering.
    if (parameters.type.raw().startsWith("image"_s))
        return result;

#if USE(EXTERNAL_HOLEPUNCH)
    HashSet<String> externalHolePunchTypes;
    MediaPlayerPrivateHolePunch::getSupportedTypes(externalHolePunchTypes);
    if (externalHolePunchTypes.contains(parameters.type.containerType()))
        return result;
#endif

    if (!ensureGStreamerInitialized())
        return result;

    GST_DEBUG("Checking mime-type \"%s\"", parameters.type.raw().utf8().data());

    registerWebKitGStreamerElements();

    auto& gstRegistryScanner = GStreamerRegistryScanner::singleton();
    result = gstRegistryScanner.isContentTypeSupported(GStreamerRegistryScanner::Configuration::Decoding, parameters.type, parameters.contentTypesRequiringHardwareSupport);

    GST_DEBUG("Supported: %s", convertEnumerationToString(result).utf8().data());
    return result;
}

MediaPlayerPrivateGStreamerHarness::MediaPlayerPrivateGStreamerHarness(MediaPlayer& player)
    : m_player(player)
#if !RELEASE_LOG_DISABLED
    , m_logger(player.mediaPlayerLogger())
    , m_logIdentifier(player.mediaPlayerLogIdentifier())
#endif
{
}

MediaPlayerPrivateGStreamerHarness::~MediaPlayerPrivateGStreamerHarness()
{
}

void MediaPlayerPrivateGStreamerHarness::load(const String& urlString)
{
    URL url { urlString };
    if (url.protocolIsAbout()) {
        loadingFailed(MediaPlayer::NetworkState::FormatError, MediaPlayer::ReadyState::HaveNothing, true);
        return;
    }

    if (!ensureGStreamerInitialized()) {
        loadingFailed(MediaPlayer::NetworkState::FormatError, MediaPlayer::ReadyState::HaveNothing, true);
        return;
    }

    RefPtr player = m_player.get();
    if (!player) {
        loadingFailed(MediaPlayer::NetworkState::FormatError, MediaPlayer::ReadyState::HaveNothing, true);
        return;
    }

    registerWebKitGStreamerElements();

    // TODO
    gst_printerrln("woot %s", urlString.ascii().data());
}

void MediaPlayerPrivateGStreamerHarness::loadingFailed(MediaPlayer::NetworkState networkError, MediaPlayer::ReadyState readyState, bool forceNotifications)
{
    GST_WARNING("Loading failed, error: %s", convertEnumerationToString(networkError).utf8().data());

    // TODO
    // RefPtr player = m_player.get();

    // m_didErrorOccur = true;
    // if (forceNotifications || m_networkState != networkError) {
    //     m_networkState = networkError;
    //     if (player)
    //         player->networkStateChanged();
    // }
    // if (forceNotifications || m_readyState != readyState) {
    //     m_readyState = readyState;
    //     if (player)
    //         player->readyStateChanged();
    // }

    // // Loading failed, remove ready timer.
    // m_pausedTimerHandler.stop();
}

#if !RELEASE_LOG_DISABLED
WTFLogChannel& MediaPlayerPrivateGStreamerHarness::logChannel() const
{
    return WebCore::LogMedia;
}
#endif

} // namespace WebCore

#undef GST_CAT_DEFAULT

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
