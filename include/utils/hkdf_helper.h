#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace themis {
namespace utils {

/**
 * @brief HKDF (HMAC-based Key Derivation Function) helper
 * 
 * Provides OpenSSL 3.0 compatible HKDF implementation.
 * Falls back to OpenSSL 1.1 EVP_PKEY_CTX API if 3.0 is not available.
 */
class HKDFHelper {
public:
    /**
     * @brief Derive key using HKDF-SHA256
     * 
     * @param ikm Input key material
     * @param salt Salt value (can be empty)
     * @param info Context and application specific information
     * @param output_length Desired output length in bytes
     * @return Derived key
     */
    static std::vector<uint8_t> derive(
        const std::vector<uint8_t>& ikm,
        const std::vector<uint8_t>& salt,
        const std::string& info,
        size_t output_length);
    
    /**
     * @brief Derive key using HKDF-SHA256 (simplified interface)
     * 
     * @param ikm_str Input key material as string
     * @param info Context string
     * @param output_length Desired output length in bytes
     * @return Derived key
     */
    static std::vector<uint8_t> deriveFromString(
        const std::string& ikm_str,
        const std::string& info,
        size_t output_length = 32);
};

} // namespace utils
} // namespace themis
