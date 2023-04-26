/*
 *  Copyright (C) 2023 Igalia S.L. All rights reserved.
 *  Copyright (C) 2023 Metrological Group B.V.
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
#include "RTCRtpSFrameTransformer.h"

#if ENABLE(WEB_RTC)

#include "CryptoAlgorithm.h"
#include "GCryptUtilities.h"
#include "SFrameUtils.h"
#include <gcrypt.h>
#include <wtf/Algorithms.h>

namespace WebCore {

ExceptionOr<Vector<uint8_t>> RTCRtpSFrameTransformer::computeSaltKey(const Vector<uint8_t>& rawKey)
{
    static auto s_usage = "SFrame10"_s;
    Vector<uint8_t> usage(s_usage.characters8(), 8);
    static auto s_info = "salt"_s;
    Vector<uint8_t> info(s_info.characters8(), 4);
    auto output = gcryptDeriveHKDFBits(rawKey, usage, info, 96 / 8, CryptoAlgorithmIdentifier::SHA_256);
    if (!output)
        return Exception { OperationError };

    return WTFMove(*output);
}

static ExceptionOr<Vector<uint8_t>> createBaseSFrameKey(const Vector<uint8_t>& rawKey)
{
    static auto s_usage = "SFrame10"_s;
    Vector<uint8_t> usage(s_usage.characters8(), 8);
    static auto s_info = "key"_s;
    Vector<uint8_t> info(s_info.characters8(), 3);
    auto output = gcryptDeriveHKDFBits(rawKey, usage, info, 128 / 8, CryptoAlgorithmIdentifier::SHA_256);
    if (!output)
        return Exception { OperationError };

    return WTFMove(*output);
}

ExceptionOr<Vector<uint8_t>> RTCRtpSFrameTransformer::computeAuthenticationKey(const Vector<uint8_t>& rawKey)
{
    auto key = createBaseSFrameKey(rawKey);
    if (key.hasException())
        return key;

    static auto s_usage = "SFrame10 AES CM AEAD"_s;
    Vector<uint8_t> usage(s_usage.characters8(), 20);
    static auto s_info = "auth"_s;
    Vector<uint8_t> info(s_info.characters8(), 4);
    auto output = gcryptDeriveHKDFBits(key.returnValue(), usage, info, 256 / 8, CryptoAlgorithmIdentifier::SHA_256);
    if (!output)
        return Exception { OperationError };

    return WTFMove(*output);
}

ExceptionOr<Vector<uint8_t>> RTCRtpSFrameTransformer::computeEncryptionKey(const Vector<uint8_t>& rawKey)
{
    auto key = createBaseSFrameKey(rawKey);
    if (key.hasException())
        return key;

    static auto s_usage = "SFrame10 AES CM AEAD"_s;
    Vector<uint8_t> usage(s_usage.characters8(), 20);
    static auto s_info = "enc"_s;
    Vector<uint8_t> info(s_info.characters8(), 3);
    auto output = gcryptDeriveHKDFBits(key.returnValue(), usage, info, 128 / 8, CryptoAlgorithmIdentifier::SHA_256);
    if (!output)
        return Exception { OperationError };

    return WTFMove(*output);
}

static std::optional<Vector<uint8_t>> gcryptAES_IV(PAL::GCrypt::CipherOperation operation, const Vector<uint8_t>& key, const Vector<uint8_t>& iv, size_t outputSize, const Vector<uint8_t>& inputText)
{
    auto algorithm = PAL::GCrypt::aesAlgorithmForKeySize(key.size() * 8);
    if (!algorithm)
        return std::nullopt;

    // Construct the libgcrypt cipher object and attach the key to it. Key information on this
    // cipher object will live through any gcry_cipher_reset() calls.
    PAL::GCrypt::Handle<gcry_cipher_hd_t> handle;
    gcry_error_t error = gcry_cipher_open(&handle, *algorithm, GCRY_CIPHER_MODE_GCM, 0);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_setkey(handle, key.data(), key.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    // Use the given IV for this cipher object.
    error = gcry_cipher_setiv(handle, iv.data(), iv.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }
    // Finalize the cipher object before performing the encryption.
    error = gcry_cipher_final(handle);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    Vector<uint8_t> output(outputSize);
    error = operation(handle, output.data(), output.size(), inputText.data(), inputText.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    return output;
}


ExceptionOr<Vector<uint8_t>> RTCRtpSFrameTransformer::decryptData(const uint8_t* rawData, size_t size, const Vector<uint8_t>& iv, const Vector<uint8_t>& key)
{
    Vector<uint8_t> data(rawData, size);
    auto output = gcryptAES_IV(gcry_cipher_decrypt, key, iv, size, data);
    if (!output)
        return Exception { OperationError };

    return WTFMove(*output);
}

ExceptionOr<Vector<uint8_t>> RTCRtpSFrameTransformer::encryptData(const uint8_t* rawData, size_t size, const Vector<uint8_t>& iv, const Vector<uint8_t>& key)
{
    Vector<uint8_t> data(rawData, size);
    auto output = gcryptAES_IV(gcry_cipher_encrypt, key, iv, size, data);
    if (!output)
        return Exception { OperationError };

    return WTFMove(*output);
}

static inline Vector<uint8_t, 8> encodeBigEndian(uint64_t value)
{
    Vector<uint8_t, 8> result(8);
    for (int i = 7; i >= 0; --i) {
        result.data()[i] = value & 0xff;
        value = value >> 8;
    }
    return result;
}

Vector<uint8_t> RTCRtpSFrameTransformer::computeEncryptedDataSignature(const Vector<uint8_t>& nonce, const uint8_t* header, size_t headerSize, const uint8_t* data, size_t dataSize, const Vector<uint8_t>& key)
{
    PAL::GCrypt::Handle<gcry_mac_hd_t> hd;
    size_t digestLength = gcry_mac_get_algo_maclen(GCRY_MAC_HMAC_SHA256);
    Vector<uint8_t> signature;
    signature.resize(digestLength);
    gcry_error_t err = gcry_mac_open(&hd, GCRY_MAC_HMAC_SHA256, 0, nullptr);
    if (err)
        return signature;

    err = gcry_mac_setkey(hd, key.data(), key.size());
    if (err)
        return signature;

    Vector<uint8_t> payload;

    auto headerLength = encodeBigEndian(headerSize);
    payload.appendVector(headerLength);

    auto dataLength = encodeBigEndian(dataSize);
    payload.appendVector(dataLength);

    payload.appendVector(nonce);

    Vector<uint8_t> headerVector(header, headerSize);
    payload.appendVector(headerVector);

    Vector<uint8_t> dataVector(data, dataSize);
    payload.appendVector(dataVector);

    err = gcry_mac_write(hd, payload.data(), payload.sizeInBytes());
    if (err)
        return signature;

    err = gcry_mac_read(hd, signature.data(), &digestLength);
    if (err)
        return signature;

    signature.resize(digestLength);
    return signature;
}

void RTCRtpSFrameTransformer::updateAuthenticationSize()
{
    size_t digestLength = gcry_mac_get_algo_maclen(GCRY_MAC_HMAC_SHA256);
    if (m_authenticationSize > digestLength)
        m_authenticationSize = digestLength;
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
