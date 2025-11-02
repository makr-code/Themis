#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <chrono>

namespace themis {
namespace auth {

/**
 * @brief JWT token claims
 */
struct JWTClaims {
    std::string sub;                          // Subject (user ID)
    std::string email;
    std::vector<std::string> groups;
    std::vector<std::string> roles;
    std::string issuer;
    std::chrono::system_clock::time_point expiration;
    
    bool isExpired() const {
        return std::chrono::system_clock::now() > expiration;
    }
};

/**
 * @brief JWT Validator for Keycloak OIDC tokens
 * 
 * Features:
 * - Parse JWT tokens (header.payload.signature)
 * - Validate signature using JWKS from Keycloak
 * - Check expiration and issuer
 * - Extract claims for access control
 */
class JWTValidator {
public:
    /**
     * @brief Initialize with Keycloak JWKS endpoint
     * @param jwks_url URL to Keycloak JWKS endpoint
     *        Example: https://keycloak.vcc.local/realms/vcc/protocol/openid-connect/certs
     */
    explicit JWTValidator(const std::string& jwks_url);
    
    /**
     * @brief Parse and validate JWT token
     * @param token Bearer token (with or without "Bearer " prefix)
     * @return Parsed claims if valid
     * @throws std::runtime_error if invalid/expired
     */
    JWTClaims parseAndValidate(const std::string& token);
    
    /**
     * @brief Derive user-specific encryption key from DEK
     * @param dek Base data encryption key
     * @param claims JWT claims for user context
     * @param field_name Field identifier for HKDF context
     * @return User-specific field key
     */
    static std::vector<uint8_t> deriveUserKey(
        const std::vector<uint8_t>& dek,
        const JWTClaims& claims,
        const std::string& field_name
    );
    
    /**
     * @brief Check if user has access to group-encrypted data
     * @param claims User's JWT claims
     * @param encryption_context Context used for encryption (user_id or group name)
     */
    static bool hasAccess(const JWTClaims& claims, const std::string& encryption_context);

private:
    std::string decodeBase64Url(const std::string& input);
    nlohmann::json fetchJWKS();
    bool verifySignature(const std::string& header_payload, 
                        const std::string& signature,
                        const nlohmann::json& jwks);
    
    std::string jwks_url_;
    nlohmann::json jwks_cache_;
    std::chrono::system_clock::time_point jwks_cache_time_;
};

} // namespace auth
} // namespace themis
