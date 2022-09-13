/*
 * Copyright (C) 2020 Sony Interactive Entertainment Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CryptoAlgorithmHKDF.h"

#if ENABLE(WEB_CRYPTO)

#include "CryptoAlgorithmHkdfParams.h"
#include "CryptoKeyRaw.h"
#include "OpenSSLUtilities.h"
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#include <openssl/hkdf.h>
#else
#include <openssl/kdf.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#endif

namespace WebCore {

ExceptionOr<Vector<uint8_t>> CryptoAlgorithmHKDF::platformDeriveBits(const CryptoAlgorithmHkdfParams& parameters, const CryptoKeyRaw& key, size_t length)
{
    Vector<uint8_t> output(length / 8);
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    auto algorithm = digestAlgorithm(parameters.hashIdentifier);
    if (!algorithm)
        return Exception { NotSupportedError };

    if (HKDF(output.data(), output.size(), algorithm, key.key().data(), key.key().size(), parameters.saltVector().data(), parameters.saltVector().size(), parameters.infoVector().data(), parameters.infoVector().size()) <= 0)
        return Exception { OperationError };
#else
    auto kdf = EVPKDFPtr(EVP_KDF_fetch(nullptr, "hkdf", nullptr));
    if (!kdf)
        return Exception { OperationError };

    auto kctx = EVPKDFCtxPtr(EVP_KDF_CTX_new(kdf.get()));
    if (!kctx)
        return Exception { OperationError };

    auto paramsBuilder = OsslParamBldPtr(OSSL_PARAM_BLD_new());
    if (!paramsBuilder)
        return Exception { OperationError };

    const char* digest = digestAlgorithmName(parameters.hashIdentifier);
    if (!digest)
        return Exception { NotSupportedError };

    if (!OSSL_PARAM_BLD_push_utf8_string(paramsBuilder.get(), "digest", digest, 0))
        return Exception { OperationError };

    if (!OSSL_PARAM_BLD_push_octet_string(paramsBuilder.get(), "salt", parameters.saltVector().data(), parameters.saltVector().size()))
        return Exception { OperationError };

    if (!OSSL_PARAM_BLD_push_octet_string(paramsBuilder.get(), "key", key.key().data(), key.key().size()))
        return Exception { OperationError };

    if (!OSSL_PARAM_BLD_push_octet_string(paramsBuilder.get(), "info", parameters.infoVector().data(), parameters.infoVector().size()))
        return Exception { OperationError };

    auto params = OsslParamPtr(OSSL_PARAM_BLD_to_param(paramsBuilder.get()));
    if (!params)
        return Exception { OperationError };

    if (EVP_KDF_CTX_set_params(kctx.get(), params.get()) <= 0)
        return Exception { OperationError };

    if (EVP_KDF_derive(kctx.get(), output.data(), output.sizeInBytes(), nullptr) <= 0)
        return Exception { OperationError };
#endif
    return output;
}

} // namespace WebCore

#endif // ENABLE(WEB_CRYPTO)
