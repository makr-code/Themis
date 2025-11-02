#include "utils/hkdf_helper.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <stdexcept>
#include <cstring>

namespace themis {
namespace utils {

std::vector<uint8_t> HKDFHelper::derive(
    const std::vector<uint8_t>& ikm,
    const std::vector<uint8_t>& salt,
    const std::string& info,
    size_t output_length) {
    
    std::vector<uint8_t> output(output_length);
    
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // OpenSSL 3.0+ API
    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) {
        throw std::runtime_error("EVP_KDF_fetch failed");
    }
    
    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    
    if (!kctx) {
        throw std::runtime_error("EVP_KDF_CTX_new failed");
    }
    
    // Build params for HKDF
    OSSL_PARAM params[5];
    const char *digest = "SHA256";
    
    params[0] = OSSL_PARAM_construct_utf8_string("digest", (char*)digest, 0);
    params[1] = OSSL_PARAM_construct_octet_string("key", (void*)ikm.data(), ikm.size());
    
    if (!salt.empty()) {
        params[2] = OSSL_PARAM_construct_octet_string("salt", (void*)salt.data(), salt.size());
    } else {
        params[2] = OSSL_PARAM_construct_end();
    }
    
    if (!info.empty()) {
        size_t param_idx = salt.empty() ? 2 : 3;
        params[param_idx] = OSSL_PARAM_construct_octet_string("info", (void*)info.data(), info.size());
        params[param_idx + 1] = OSSL_PARAM_construct_end();
    } else {
        size_t param_idx = salt.empty() ? 2 : 3;
        params[param_idx] = OSSL_PARAM_construct_end();
    }
    
    if (EVP_KDF_derive(kctx, output.data(), output.size(), params) <= 0) {
        EVP_KDF_CTX_free(kctx);
        throw std::runtime_error("EVP_KDF_derive failed");
    }
    
    EVP_KDF_CTX_free(kctx);
    
#else
    // OpenSSL 1.1 fallback API
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!pctx) {
        throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
    }
    
    if (EVP_PKEY_derive_init(pctx) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("EVP_PKEY_derive_init failed");
    }
    
    if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("EVP_PKEY_CTX_set_hkdf_md failed");
    }
    
    if (!salt.empty()) {
        if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt.data(), salt.size()) <= 0) {
            EVP_PKEY_CTX_free(pctx);
            throw std::runtime_error("EVP_PKEY_CTX_set1_hkdf_salt failed");
        }
    }
    
    if (EVP_PKEY_CTX_set1_hkdf_key(pctx, ikm.data(), ikm.size()) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("EVP_PKEY_CTX_set1_hkdf_key failed");
    }
    
    if (!info.empty()) {
        if (EVP_PKEY_CTX_add1_hkdf_info(pctx, 
                                         reinterpret_cast<const unsigned char*>(info.data()),
                                         info.size()) <= 0) {
            EVP_PKEY_CTX_free(pctx);
            throw std::runtime_error("EVP_PKEY_CTX_add1_hkdf_info failed");
        }
    }
    
    size_t outlen = output.size();
    if (EVP_PKEY_derive(pctx, output.data(), &outlen) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("EVP_PKEY_derive failed");
    }
    
    EVP_PKEY_CTX_free(pctx);
#endif
    
    return output;
}

std::vector<uint8_t> HKDFHelper::deriveFromString(
    const std::string& ikm_str,
    const std::string& info,
    size_t output_length) {
    
    std::vector<uint8_t> ikm(ikm_str.begin(), ikm_str.end());
    std::vector<uint8_t> salt;  // Empty salt
    
    return derive(ikm, salt, info, output_length);
}

} // namespace utils
} // namespace themis
