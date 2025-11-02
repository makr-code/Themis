#include "auth/jwt_validator.h"
#include "utils/hkdf_helper.h"

#include <openssl/evp.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace themis {
namespace auth {

JWTValidator::JWTValidator(const std::string& jwks_url)
    : jwks_url_(jwks_url)
    , jwks_cache_time_(std::chrono::system_clock::time_point::min()) {
}

std::string JWTValidator::decodeBase64Url(const std::string& input) {
    std::string base64 = input;
    
    // Replace URL-safe characters
    std::replace(base64.begin(), base64.end(), '-', '+');
    std::replace(base64.begin(), base64.end(), '_', '/');
    
    // Add padding if needed
    while (base64.size() % 4 != 0) {
        base64 += '=';
    }
    
    // Decode using OpenSSL
    BIO* bio = BIO_new_mem_buf(base64.data(), base64.size());
    BIO* b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    
    std::vector<char> decoded(base64.size());
    int len = BIO_read(bio, decoded.data(), decoded.size());
    BIO_free_all(bio);
    
    return std::string(decoded.begin(), decoded.begin() + len);
}

nlohmann::json JWTValidator::fetchJWKS() {
    // Cache JWKS for 1 hour
    auto now = std::chrono::system_clock::now();
    if (now - jwks_cache_time_ < std::chrono::hours(1) && !jwks_cache_.empty()) {
        return jwks_cache_;
    }
    
    // In production: Use HTTP client to fetch from jwks_url_
    // For now, return mock JWKS
    jwks_cache_ = {
        {"keys", nlohmann::json::array()}
    };
    jwks_cache_time_ = now;
    
    return jwks_cache_;
}

bool JWTValidator::verifySignature(const std::string& header_payload,
                                  const std::string& signature,
                                  const nlohmann::json& jwks) {
    // In production: Verify RS256/ES256 signature using public key from JWKS
    // For now, skip verification (demo mode)
    return true;
}

JWTClaims JWTValidator::parseAndValidate(const std::string& token) {
    // Remove "Bearer " prefix if present
    std::string jwt = token;
    if (jwt.rfind("Bearer ", 0) == 0) {
        jwt = jwt.substr(7);
    }
    
    // Split into parts
    std::vector<std::string> parts;
    std::stringstream ss(jwt);
    std::string part;
    while (std::getline(ss, part, '.')) {
        parts.push_back(part);
    }
    
    if (parts.size() != 3) {
        throw std::runtime_error("Invalid JWT format (expected 3 parts)");
    }
    
    // Decode payload
    std::string payload_json = decodeBase64Url(parts[1]);
    auto payload = nlohmann::json::parse(payload_json);
    
    // Extract claims
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
    
    if (payload.contains("exp")) {
        int64_t exp = payload["exp"];
        claims.expiration = std::chrono::system_clock::time_point{
            std::chrono::seconds{exp}
        };
    } else {
        // No expiration → set to far future
        claims.expiration = std::chrono::system_clock::time_point::max();
    }
    
    // Check expiration
    if (claims.isExpired()) {
        throw std::runtime_error("JWT token expired");
    }
    
    // Verify signature
    auto jwks = fetchJWKS();
    std::string header_payload = parts[0] + "." + parts[1];
    if (!verifySignature(header_payload, parts[2], jwks)) {
        throw std::runtime_error("JWT signature verification failed");
    }
    
    return claims;
}

std::vector<uint8_t> JWTValidator::deriveUserKey(
    const std::vector<uint8_t>& dek,
    const JWTClaims& claims,
    const std::string& field_name) {
    
    // Derive per-user key: HKDF(DEK, salt=user_id, info=field_name)
    std::vector<uint8_t> salt(claims.sub.begin(), claims.sub.end());
    std::string info = "user-field:" + field_name;
    
    return themis::utils::HKDFHelper::derive(dek, salt, info, 32);
}

bool JWTValidator::hasAccess(const JWTClaims& claims, const std::string& encryption_context) {
    // Check if user_id matches
    if (claims.sub == encryption_context) {
        return true;
    }
    
    // Check if any group matches
    for (const auto& group : claims.groups) {
        if (group == encryption_context) {
            return true;
        }
    }
    
    return false;
}

} // namespace auth
} // namespace themis
