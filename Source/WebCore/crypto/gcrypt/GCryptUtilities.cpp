/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 * Copyright (C) 2017 Metrological Group B.V.
 * Copyright (C) 2017 Igalia S.L.
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
#include "GCryptUtilities.h"


namespace WebCore {

std::optional<const char*> hashAlgorithmName(CryptoAlgorithmIdentifier identifier)
{
    switch (identifier) {
    case CryptoAlgorithmIdentifier::SHA_1:
        return "sha1";
    case CryptoAlgorithmIdentifier::SHA_224:
        return "sha224";
    case CryptoAlgorithmIdentifier::SHA_256:
        return "sha256";
    case CryptoAlgorithmIdentifier::SHA_384:
        return "sha384";
    case CryptoAlgorithmIdentifier::SHA_512:
        return "sha512";
    default:
        return std::nullopt;
    }
}

std::optional<int> hmacAlgorithm(CryptoAlgorithmIdentifier identifier)
{
    switch (identifier) {
    case CryptoAlgorithmIdentifier::SHA_1:
        return GCRY_MAC_HMAC_SHA1;
    case CryptoAlgorithmIdentifier::SHA_224:
        return GCRY_MAC_HMAC_SHA224;
    case CryptoAlgorithmIdentifier::SHA_256:
        return GCRY_MAC_HMAC_SHA256;
    case CryptoAlgorithmIdentifier::SHA_384:
        return GCRY_MAC_HMAC_SHA384;
    case CryptoAlgorithmIdentifier::SHA_512:
        return GCRY_MAC_HMAC_SHA512;
    default:
        return std::nullopt;
    }
}

std::optional<int> digestAlgorithm(CryptoAlgorithmIdentifier identifier)
{
    switch (identifier) {
    case CryptoAlgorithmIdentifier::SHA_1:
        return GCRY_MD_SHA1;
    case CryptoAlgorithmIdentifier::SHA_224:
        return GCRY_MD_SHA224;
    case CryptoAlgorithmIdentifier::SHA_256:
        return GCRY_MD_SHA256;
    case CryptoAlgorithmIdentifier::SHA_384:
        return GCRY_MD_SHA384;
    case CryptoAlgorithmIdentifier::SHA_512:
        return GCRY_MD_SHA512;
    default:
        return std::nullopt;
    }
}

std::optional<PAL::CryptoDigest::Algorithm> hashCryptoDigestAlgorithm(CryptoAlgorithmIdentifier identifier)
{
    switch (identifier) {
    case CryptoAlgorithmIdentifier::SHA_1:
        return PAL::CryptoDigest::Algorithm::SHA_1;
    case CryptoAlgorithmIdentifier::SHA_224:
        return PAL::CryptoDigest::Algorithm::SHA_224;
    case CryptoAlgorithmIdentifier::SHA_256:
        return PAL::CryptoDigest::Algorithm::SHA_256;
    case CryptoAlgorithmIdentifier::SHA_384:
        return PAL::CryptoDigest::Algorithm::SHA_384;
    case CryptoAlgorithmIdentifier::SHA_512:
        return PAL::CryptoDigest::Algorithm::SHA_512;
    default:
        return std::nullopt;
    }
}

std::optional<size_t> mpiLength(gcry_mpi_t paramMPI)
{
    // Retrieve the MPI length for the unsigned format.
    size_t dataLength = 0;
    gcry_error_t error = gcry_mpi_print(GCRYMPI_FMT_USG, nullptr, 0, &dataLength, paramMPI);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    return dataLength;
}

std::optional<size_t> mpiLength(gcry_sexp_t paramSexp)
{
    // Retrieve the MPI value stored in the s-expression: (name mpi-data)
    PAL::GCrypt::Handle<gcry_mpi_t> paramMPI(gcry_sexp_nth_mpi(paramSexp, 1, GCRYMPI_FMT_USG));
    if (!paramMPI)
        return std::nullopt;

    return mpiLength(paramMPI);
}

std::optional<Vector<uint8_t>> mpiData(gcry_mpi_t paramMPI)
{
    // Retrieve the MPI length.
    auto length = mpiLength(paramMPI);
    if (!length)
        return std::nullopt;

    // Copy the MPI data into a properly-sized buffer.
    Vector<uint8_t> output(*length);
    gcry_error_t error = gcry_mpi_print(GCRYMPI_FMT_USG, output.data(), output.size(), nullptr, paramMPI);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    return output;
}

std::optional<Vector<uint8_t>> mpiZeroPrefixedData(gcry_mpi_t paramMPI, size_t targetLength)
{
    // Retrieve the MPI length. Bail if the retrieved length is longer than target length.
    auto length = mpiLength(paramMPI);
    if (!length || *length > targetLength)
        return std::nullopt;

    // Fill out the output buffer with zeros. Properly determine the zero prefix length,
    // and copy the MPI data into memory area following the prefix (if any).
    Vector<uint8_t> output(targetLength, 0);
    size_t prefixLength = targetLength - *length;
    gcry_error_t error = gcry_mpi_print(GCRYMPI_FMT_USG, output.data() + prefixLength, targetLength, nullptr, paramMPI);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    return output;
}

std::optional<Vector<uint8_t>> mpiData(gcry_sexp_t paramSexp)
{
    // Retrieve the MPI value stored in the s-expression: (name mpi-data)
    PAL::GCrypt::Handle<gcry_mpi_t> paramMPI(gcry_sexp_nth_mpi(paramSexp, 1, GCRYMPI_FMT_USG));
    if (!paramMPI)
        return std::nullopt;

    return mpiData(paramMPI);
}

std::optional<Vector<uint8_t>> mpiZeroPrefixedData(gcry_sexp_t paramSexp, size_t targetLength)
{
    // Retrieve the MPI value stored in the s-expression: (name mpi-data)
    PAL::GCrypt::Handle<gcry_mpi_t> paramMPI(gcry_sexp_nth_mpi(paramSexp, 1, GCRYMPI_FMT_USG));
    if (!paramMPI)
        return std::nullopt;

    return mpiZeroPrefixedData(paramMPI, targetLength);
}

std::optional<Vector<uint8_t>> mpiSignedData(gcry_mpi_t mpi)
{
    auto data = mpiData(mpi);
    if (!data)
        return std::nullopt;

    if (data->at(0) & 0x80)
        data->insert(0, 0x00);

    return data;
}

std::optional<Vector<uint8_t>> mpiSignedData(gcry_sexp_t paramSexp)
{
    auto data = mpiData(paramSexp);
    if (!data)
        return std::nullopt;

    if (data->at(0) & 0x80)
        data->insert(0, 0x00);

    return data;
}

// libgcrypt doesn't provide HKDF functionality, so we have to implement it manually.
// We should switch to the libgcrypt-provided implementation once it's available.
// https://bugs.webkit.org/show_bug.cgi?id=171536

std::optional<Vector<uint8_t>> gcryptDeriveHKDFBits(const Vector<uint8_t>& key, const Vector<uint8_t>& salt, const Vector<uint8_t>& info, size_t lengthInBytes, CryptoAlgorithmIdentifier identifier)
{
    // libgcrypt doesn't provide HKDF support, so we have to implement
    // the functionality ourselves as specified in RFC5869.
    // https://www.ietf.org/rfc/rfc5869.txt

    auto macAlgorithm = hmacAlgorithm(identifier);
    if (!macAlgorithm)
        return std::nullopt;

    // We can immediately discard invalid output lengths, otherwise needed for the expand step.
    size_t macLength = gcry_mac_get_algo_maclen(*macAlgorithm);
    if (lengthInBytes > macLength * 255)
        return std::nullopt;

    PAL::GCrypt::Handle<gcry_mac_hd_t> handle;
    gcry_error_t error = gcry_mac_open(&handle, *macAlgorithm, 0, nullptr);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    // Step 1 -- Extract. A pseudo-random key is generated with the specified algorithm
    // for the given salt value (used as a key) and the 'input keying material'.
    Vector<uint8_t> pseudoRandomKey(macLength);
    {
        // If the salt vector is empty, a zeroed-out key of macLength size should be used.
        if (salt.isEmpty()) {
            Vector<uint8_t> zeroedKey(macLength, 0);
            error = gcry_mac_setkey(handle, zeroedKey.data(), zeroedKey.size());
        } else
            error = gcry_mac_setkey(handle, salt.data(), salt.size());
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return std::nullopt;
        }

        error = gcry_mac_write(handle, key.data(), key.size());
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return std::nullopt;
        }

        size_t pseudoRandomKeySize = pseudoRandomKey.size();
        error = gcry_mac_read(handle, pseudoRandomKey.data(), &pseudoRandomKeySize);
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return std::nullopt;
        }

        // Something went wrong if libgcrypt didn't write out the proper amount of data.
        if (pseudoRandomKeySize != macLength)
            return std::nullopt;
    }

    // Step #2 -- Expand.
    Vector<uint8_t> output;
    {
        // Deduce the number of needed iterations to retrieve the necessary amount of data.
        size_t numIterations = (lengthInBytes + macLength) / macLength;
        // Block from the previous iteration is used in the current one, except
        // in the first iteration when it's empty.
        Vector<uint8_t> lastBlock(macLength);

        for (size_t i = 0; i < numIterations; ++i) {
            error = gcry_mac_reset(handle);
            if (error != GPG_ERR_NO_ERROR) {
                PAL::GCrypt::logError(error);
                return std::nullopt;
            }

            error = gcry_mac_setkey(handle, pseudoRandomKey.data(), pseudoRandomKey.size());
            if (error != GPG_ERR_NO_ERROR) {
                PAL::GCrypt::logError(error);
                return std::nullopt;
            }

            // T(0) = empty string (zero length) -- i.e. empty lastBlock
            // T(i) = HMAC-Hash(PRK, T(i-1) | info | hex(i)) -- | represents concatenation
            Vector<uint8_t> blockData;
            if (i)
                blockData.appendVector(lastBlock);
            blockData.appendVector(info);
            blockData.append(i + 1);

            error = gcry_mac_write(handle, blockData.data(), blockData.size());
            if (error != GPG_ERR_NO_ERROR) {
                PAL::GCrypt::logError(error);
                return std::nullopt;
            }

            size_t blockSize = lastBlock.size();
            error = gcry_mac_read(handle, lastBlock.data(), &blockSize);
            if (error != GPG_ERR_NO_ERROR) {
                PAL::GCrypt::logError(error);
                return std::nullopt;
            }

            // Something went wrong if libgcrypt didn't write out the proper amount of data.
            if (blockSize != lastBlock.size())
                return std::nullopt;

            // Append the current block data to the output vector.
            output.appendVector(lastBlock);
        }
    }

    // Clip output vector to the requested size.
    output.resize(lengthInBytes);
    return output;
}

// This is a helper function that resets the cipher object, sets the provided counter data,
// and executes the encrypt or decrypt operation, retrieving and returning the output data.
static std::optional<Vector<uint8_t>> callOperation(PAL::GCrypt::CipherOperation operation, gcry_cipher_hd_t handle, const Vector<uint8_t>& counter, const uint8_t* data, const size_t size)
{
    gcry_error_t error = gcry_cipher_reset(handle);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_setctr(handle, counter.data(), counter.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_final(handle);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    Vector<uint8_t> output(size);
    error = operation(handle, output.data(), output.size(), data, size);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    return output;
}

std::optional<Vector<uint8_t>> gcryptAES_CTR(PAL::GCrypt::CipherOperation operation, const Vector<uint8_t>& key, const Vector<uint8_t>& counter, size_t counterLength, const Vector<uint8_t>& inputText)
{
    constexpr size_t blockSize = 16;
    auto algorithm = PAL::GCrypt::aesAlgorithmForKeySize(key.size() * 8);
    if (!algorithm)
        return std::nullopt;

    // Construct the libgcrypt cipher object and attach the key to it. Key information on this
    // cipher object will live through any gcry_cipher_reset() calls.
    PAL::GCrypt::Handle<gcry_cipher_hd_t> handle;
    gcry_error_t error = gcry_cipher_open(&handle, *algorithm, GCRY_CIPHER_MODE_CTR, 0);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_setkey(handle, key.data(), key.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    // Calculate the block count: ((inputText.size() + blockSize - 1) / blockSize), remainder discarded.
    PAL::GCrypt::Handle<gcry_mpi_t> blockCountMPI(gcry_mpi_new(0));
    {
        PAL::GCrypt::Handle<gcry_mpi_t> blockSizeMPI(gcry_mpi_set_ui(nullptr, blockSize));
        PAL::GCrypt::Handle<gcry_mpi_t> roundedUpSize(gcry_mpi_set_ui(nullptr, inputText.size()));

        gcry_mpi_add_ui(roundedUpSize, roundedUpSize, blockSize - 1);
        gcry_mpi_div(blockCountMPI, nullptr, roundedUpSize, blockSizeMPI, 0);
    }

    // Calculate the counter limit for the specified counter length: (2 << counterLength).
    // (counterLimitMPI - 1) is the maximum value the counter can hold -- essentially it's
    // a bit-mask for valid counter values.
    PAL::GCrypt::Handle<gcry_mpi_t> counterLimitMPI(gcry_mpi_set_ui(nullptr, 1));
    gcry_mpi_mul_2exp(counterLimitMPI, counterLimitMPI, counterLength);

    // Counter values must not repeat for a given cipher text. If the counter limit (i.e.
    // the number of unique counter values we could produce for the specified counter
    // length) is lower than the deduced block count, we bail.
    if (gcry_mpi_cmp(counterLimitMPI, blockCountMPI) < 0)
        return std::nullopt;

    // If the counter length, in bits, matches the size of the counter data, we don't have to
    // use any part of the counter Vector<> as nonce. This allows us to directly encrypt or
    // decrypt all the provided data in a single step.
    if (counterLength == counter.size() * 8)
        return callOperation(operation, handle, counter, inputText.data(), inputText.size());

    // Scan the counter data into the MPI format. We'll do all the counter computations with
    // the MPI API.
    PAL::GCrypt::Handle<gcry_mpi_t> counterDataMPI;
    error = gcry_mpi_scan(&counterDataMPI, GCRYMPI_FMT_USG, counter.data(), counter.size(), nullptr);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    // Extract the counter MPI from the counterDataMPI: (counterDataMPI % counterLimitMPI).
    // This MPI represents solely the counter value, as initially provided.
    PAL::GCrypt::Handle<gcry_mpi_t> counterMPI(gcry_mpi_new(0));
    gcry_mpi_mod(counterMPI, counterDataMPI, counterLimitMPI);

    {
        // Calculate the leeway of the initially-provided counter: counterLimitMPI - counterMPI.
        // This is essentially the number of blocks we can encrypt/decrypt with that counter
        // (incrementing it after each operation) before the counter wraps around to 0.
        PAL::GCrypt::Handle<gcry_mpi_t> counterLeewayMPI(gcry_mpi_new(0));
        gcry_mpi_sub(counterLeewayMPI, counterLimitMPI, counterMPI);

        // If counterLeewayMPI is larger or equal to the deduced block count, we can directly
        // encrypt or decrypt the provided data in a single step since it's ensured that the
        // counter won't overflow.
        if (gcry_mpi_cmp(counterLeewayMPI, blockCountMPI) >= 0)
            return callOperation(operation, handle, counter, inputText.data(), inputText.size());
    }

    // From here onwards we're dealing with a counter of which the length doesn't match the
    // provided data, meaning we'll also have to manage the nonce data. The counter will also
    // wrap around, so we'll have to address that too.

    // Determine the nonce MPI that we'll use to reconstruct the counter data for each block:
    // (counterDataMPI - counterMPI). This is equivalent to counterDataMPI with the lowest
    // counterLength bits cleared.
    PAL::GCrypt::Handle<gcry_mpi_t> nonceMPI(gcry_mpi_new(0));
    gcry_mpi_sub(nonceMPI, counterDataMPI, counterMPI);

    // FIXME: This should be optimized further by first encrypting the amount of blocks for
    // which the counter won't yet wrap around, and then encrypting the rest of the blocks
    // starting from the counter set to 0.

    Vector<uint8_t> output;
    Vector<uint8_t> blockCounterData(16);
    size_t inputTextSize = inputText.size();

    for (size_t i = 0; i < inputTextSize; i += 16) {
        size_t blockInputSize = std::min<size_t>(16, inputTextSize - i);

        // Construct the block-specific counter: (nonceMPI + counterMPI).
        PAL::GCrypt::Handle<gcry_mpi_t> blockCounterMPI(gcry_mpi_new(0));
        gcry_mpi_add(blockCounterMPI, nonceMPI, counterMPI);

        error = gcry_mpi_print(GCRYMPI_FMT_USG, blockCounterData.data(), blockCounterData.size(), nullptr, blockCounterMPI);
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return std::nullopt;
        }

        // Encrypt/decrypt this single block with the block-specific counter. Output for this
        // single block is appended to the general output vector.
        auto blockOutput = callOperation(operation, handle, blockCounterData, inputText.data() + i, blockInputSize);
        if (!blockOutput)
            return std::nullopt;

        output.appendVector(*blockOutput);

        // Increment the counter. The modulus operation takes care of any wrap-around.
        PAL::GCrypt::Handle<gcry_mpi_t> counterIncrementMPI(gcry_mpi_new(0));
        gcry_mpi_add_ui(counterIncrementMPI, counterMPI, 1);
        gcry_mpi_mod(counterMPI, counterIncrementMPI, counterLimitMPI);
    }

    return output;
}

} // namespace WebCore
