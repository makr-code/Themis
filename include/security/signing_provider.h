#pragma once

#include "security/key_provider.h"
#include "security/signing.h"

namespace themis {

/**
 * Optional interface for KeyProvider implementations that can perform
 * signing operations on behalf of the caller (e.g., HSM, KMS).
 *
 * Implementations should avoid exporting raw private key material and
 * instead return a `SigningResult` for the provided data.
 */
class SigningProvider : public KeyProvider {
public:
    virtual ~SigningProvider() = default;

    // Sign data using the key identified by key_id. Returns a SigningResult
    // containing signature bytes and algorithm metadata.
    virtual SigningResult sign(const std::string& key_id, const std::vector<uint8_t>& data) = 0;
};

} // namespace themis
