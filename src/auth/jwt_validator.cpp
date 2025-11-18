#include "auth/jwt_validator.h"
#include "utils/hkdf_helper.h"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/sha.h>

#include <curl/curl.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace themis {
namespace auth {

namespace {
size_t curlWriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto total = size * nmemb;
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, total);
    return total;
}
}

JWTValidator::JWTValidator(const std::string& jwks_url)
    : cfg_{JWTValidatorConfig{jwks_url, "", "", std::chrono::seconds(600), std::chrono::seconds(60)}}
    , jwks_url_(jwks_url)
    , jwks_cache_time_(std::chrono::system_clock::time_point::min()) {}

JWTValidator::JWTValidator(const JWTValidatorConfig& cfg)
    : cfg_(cfg)
    , jwks_url_(cfg.jwks_url)
    , jwks_cache_time_(std::chrono::system_clock::time_point::min()) {}

std::vector<uint8_t> JWTValidator::decodeBase64Url(const std::string& input) {
    std::string base64 = input;
    std::replace(base64.begin(), base64.end(), '-', '+');
    std::replace(base64.begin(), base64.end(), '_', '/');
    while (base64.size() % 4 != 0) base64 += '=';
    BIO* bio = BIO_new_mem_buf(base64.data(), static_cast<int>(base64.size()));
    BIO* b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    std::vector<uint8_t> decoded(base64.size());
    int len = BIO_read(bio, decoded.data(), static_cast<int>(decoded.size()));
    BIO_free_all(bio);
    if (len < 0) return {};
    decoded.resize(len);
    return decoded;
}

std::string JWTValidator::decodeBase64UrlToString(const std::string& input) {
    auto bytes = decodeBase64Url(input);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

nlohmann::json JWTValidator::fetchJWKS() {
    auto now = std::chrono::system_clock::now();
    if (!jwks_cache_.empty() && now - jwks_cache_time_ < cfg_.cache_ttl) {
        return jwks_cache_;
    }
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to init curl for JWKS fetch");
    curl_easy_setopt(curl, CURLOPT_URL, jwks_url_.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK || code != 200) {
        throw std::runtime_error("JWKS HTTP error: " + std::to_string(code));
    }
    auto json = nlohmann::json::parse(response);
    if (!json.is_object() || !json.contains("keys")) {
        throw std::runtime_error("Invalid JWKS document (missing keys)");
    }
    jwks_cache_ = json;
    jwks_cache_time_ = now;
    return jwks_cache_;
}

const nlohmann::json* JWTValidator::findJwkForKid(const nlohmann::json& jwks, const std::string& kid) const {
    if (!jwks.contains("keys")) return nullptr;
    for (auto& k : jwks["keys"]) {
        if (k.is_object() && k.value("kid", std::string()) == kid) return &k;
    }
    return nullptr;
}

bool JWTValidator::verifySignatureRS256(const std::string& header_payload,
                                        const std::vector<uint8_t>& signature,
                                        const nlohmann::json& jwk) {
    if (jwk.value("kty", "") != "RSA") return false;
    auto n_b64 = jwk.value("n", "");
    auto e_b64 = jwk.value("e", "");
    if (n_b64.empty() || e_b64.empty()) return false;
    auto n_bytes = decodeBase64Url(n_b64);
    auto e_bytes = decodeBase64Url(e_b64);
    BIGNUM* n = BN_bin2bn(n_bytes.data(), (int)n_bytes.size(), nullptr);
    BIGNUM* e = BN_bin2bn(e_bytes.data(), (int)e_bytes.size(), nullptr);
    if (!n || !e) { if (n) BN_free(n); if (e) BN_free(e); return false; }
    
    // Use EVP_PKEY directly instead of deprecated RSA_new()
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) { BN_free(n); BN_free(e); return false; }
    
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)  // OpenSSL deprecated APIs
#endif
    RSA* rsa = RSA_new();
    if (!rsa || RSA_set0_key(rsa, n, e, nullptr) != 1) { 
        EVP_PKEY_free(pkey); 
        if (rsa) RSA_free(rsa); else { BN_free(n); BN_free(e); }
        return false; 
    }
    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) { RSA_free(rsa); EVP_PKEY_free(pkey); return false; }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    // Verify using EVP_DigestVerify to compute SHA256 and PKCS#1 v1.5
    EVP_MD_CTX* mctx = EVP_MD_CTX_new();
    if (!mctx) { EVP_PKEY_free(pkey); return false; }
    int ok = EVP_DigestVerifyInit(mctx, nullptr, EVP_sha256(), nullptr, pkey);
    if (ok != 1) { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); return false; }
    ok = EVP_DigestVerifyUpdate(mctx, header_payload.data(), header_payload.size());
    if (ok != 1) { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); return false; }
    ok = EVP_DigestVerifyFinal(mctx, signature.data(), signature.size());
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);
    return ok == 1;
}

bool JWTValidator::checkAudience(const nlohmann::json& payload) const {
    if (cfg_.expected_audience.empty()) return true;
    if (!payload.contains("aud")) return false;
    if (payload["aud"].is_string()) {
        return payload["aud"].get<std::string>() == cfg_.expected_audience;
    }
    if (payload["aud"].is_array()) {
        for (auto& v : payload["aud"]) {
            if (v.is_string() && v.get<std::string>() == cfg_.expected_audience) return true;
        }
        return false;
    }
    return false;
}

void JWTValidator::setJWKSForTesting(const nlohmann::json& jwks,
                                     std::chrono::system_clock::time_point t) {
    jwks_cache_ = jwks;
    jwks_cache_time_ = t;
}

JWTClaims JWTValidator::parseAndValidate(const std::string& token) {
    std::string jwt = token;
    if (jwt.rfind("Bearer ", 0) == 0) {
        jwt = jwt.substr(7);
    }
    std::vector<std::string> parts;
    std::stringstream ss(jwt);
    std::string part;
    while (std::getline(ss, part, '.')) {
        parts.push_back(part);
    }
    if (parts.size() != 3) {
        throw std::runtime_error("Invalid JWT format (expected 3 parts)");
    }
    auto header_json = decodeBase64UrlToString(parts[0]);
    auto payload_json = decodeBase64UrlToString(parts[1]);
    auto header = nlohmann::json::parse(header_json);
    auto payload = nlohmann::json::parse(payload_json);
    std::string alg = header.value("alg", "");
    std::string kid = header.value("kid", "");
    if (alg != "RS256") {
        throw std::runtime_error("Unsupported alg: " + alg);
    }
    JWTClaims claims;
    claims.sub = payload.value("sub", "");
    claims.email = payload.value("email", "");
    claims.issuer = payload.value("iss", "");
    if (payload.contains("groups")) {
        claims.groups = payload["groups"].get<std::vector<std::string>>();
    }
    if (payload.contains("roles")) {
        claims.roles = payload["roles"].get<std::vector<std::string>>();
    }
    auto now = std::chrono::system_clock::now();
    if (payload.contains("exp")) {
        int64_t exp = payload["exp"].get<int64_t>();
        claims.expiration = std::chrono::system_clock::time_point{std::chrono::seconds{exp}};
    } else {
        throw std::runtime_error("Missing exp claim");
    }
    if (payload.contains("nbf")) {
        int64_t nbf = payload["nbf"].get<int64_t>();
        claims.not_before = std::chrono::system_clock::time_point{std::chrono::seconds{nbf}};
        if (now + cfg_.clock_skew < *claims.not_before) {
            throw std::runtime_error("Token not yet valid (nbf)");
        }
    }
    if (payload.contains("iat")) {
        int64_t iat = payload["iat"].get<int64_t>();
        claims.issued_at = std::chrono::system_clock::time_point{std::chrono::seconds{iat}};
        if (now + cfg_.clock_skew < *claims.issued_at) {
            throw std::runtime_error("iat in future");
        }
    }
    if (payload.contains("aud")) {
        if (payload["aud"].is_string()) {
            claims.audience.push_back(payload["aud"].get<std::string>());
        } else if (payload["aud"].is_array()) {
            for (auto& v : payload["aud"]) if (v.is_string()) claims.audience.push_back(v.get<std::string>());
        }
    }
    if (claims.isExpired() && now > claims.expiration + cfg_.clock_skew) {
        throw std::runtime_error("Token expired");
    }
    if (!cfg_.expected_issuer.empty() && claims.issuer != cfg_.expected_issuer) {
        throw std::runtime_error("Issuer mismatch");
    }
    if (!checkAudience(payload)) {
        throw std::runtime_error("Audience mismatch");
    }
    auto jwks = fetchJWKS();
    auto sig_bytes = decodeBase64Url(parts[2]);
    std::string header_payload = parts[0] + "." + parts[1];
    const nlohmann::json* jwk = nullptr;
    if (!kid.empty()) {
        jwk = findJwkForKid(jwks, kid);
        if (!jwk) {
            jwks_cache_time_ = std::chrono::system_clock::time_point::min();
            jwks = fetchJWKS();
            jwk = findJwkForKid(jwks, kid);
        }
    }
    if (!jwk) throw std::runtime_error("JWK not found for kid");
    if (!verifySignatureRS256(header_payload, sig_bytes, *jwk)) {
        throw std::runtime_error("Signature verification failed");
    }
    return claims;
}

std::vector<uint8_t> JWTValidator::deriveUserKey(
    const std::vector<uint8_t>& dek,
    const JWTClaims& claims,
    const std::string& field_name) {
    std::vector<uint8_t> salt(claims.sub.begin(), claims.sub.end());
    std::string info = "user-field:" + field_name;
    return themis::utils::HKDFHelper::derive(dek, salt, info, 32);
}

bool JWTValidator::hasAccess(const JWTClaims& claims, const std::string& encryption_context) {
    if (claims.sub == encryption_context) {
        return true;
    }
    for (const auto& group : claims.groups) {
        if (group == encryption_context) {
            return true;
        }
    }
    return false;
}

} // namespace auth
} // namespace themis
