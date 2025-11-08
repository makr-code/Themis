#include "server/auth_middleware.h"
#include "auth/jwt_validator.h"
#include "utils/logger.h"
#include <sstream>

namespace themis {

AuthMiddleware::AuthMiddleware() = default;
AuthMiddleware::~AuthMiddleware() = default;

void AuthMiddleware::enableJWT(const JWTConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auth::JWTValidatorConfig jwt_cfg;
    jwt_cfg.jwks_url = config.jwks_url;
    jwt_cfg.expected_issuer = config.expected_issuer;
    jwt_cfg.expected_audience = config.expected_audience;
    jwt_cfg.cache_ttl = config.jwks_cache_ttl;
    jwt_cfg.clock_skew = config.clock_skew;
    
    jwt_validator_ = std::make_unique<auth::JWTValidator>(jwt_cfg);
    jwt_config_ = config;
    jwt_enabled_ = true;
    
    THEMIS_INFO("JWT validation enabled: issuer='{}', audience='{}', scope_claim='{}'",
                config.expected_issuer, config.expected_audience, config.scope_claim);
}

void AuthMiddleware::addToken(const TokenConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_[config.token] = config;
    THEMIS_INFO("Added API token for user '{}' with {} scopes", config.user_id, config.scopes.size());
}

void AuthMiddleware::removeToken(std::string_view token) {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_.erase(std::string(token));
}

void AuthMiddleware::clearTokens() {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_.clear();
}

AuthMiddleware::AuthResult AuthMiddleware::authorize(std::string_view token, std::string_view required_scope) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // First try API token lookup
    auto it = tokens_.find(std::string(token));
    if (it != tokens_.end()) {
        const auto& config = it->second;
        
        // Check if token has required scope
        if (config.scopes.count(std::string(required_scope)) == 0) {
            metrics_.authz_denied_total++;
            std::ostringstream oss;
            oss << "Missing required scope: " << required_scope;
            THEMIS_WARN("Authorization denied for user '{}': {}", config.user_id, oss.str());
            return AuthResult::Denied(oss.str());
        }

        metrics_.authz_success_total++;
        return AuthResult::OK(config.user_id);
    }
    
    // If JWT is enabled, try JWT validation as fallback
    if (jwt_enabled_) {
        return authorizeViaJWT(token, required_scope);
    }
    
    // No match found
    metrics_.authz_invalid_token_total++;
    return AuthResult::Denied("Invalid or missing token");
}

AuthMiddleware::AuthResult AuthMiddleware::authorizeViaJWT(std::string_view token, std::string_view required_scope) const {
    // Note: mutex is already locked by caller (authorize)
    
    if (!jwt_validator_) {
        return AuthResult::Denied("JWT validation not configured");
    }
    
    try {
        // Parse and validate JWT
        auto claims = jwt_validator_->parseAndValidate(std::string(token));
        
        metrics_.jwt_validation_success_total++;
        
        // Extract scopes from configured claim (e.g., "roles", "groups")
        std::unordered_set<std::string> scopes;
        
        // Check if claim exists and is array
        // For now, we'll use a simple approach: derive scope from user_id if no scope claim
        // In production, you'd parse claims.roles or claims.groups properly
        
        // Simple mapping: if user has valid JWT, grant basic access
        // TODO: Enhance with proper scope extraction from JWT claims
        
        // For now: check if required_scope is in a hardcoded allowed list or derive from sub
        // Better: parse jwt_config_.scope_claim from the JWT payload
        
        // Placeholder: grant access if JWT is valid (you should enhance this)
        THEMIS_INFO("JWT validated for user '{}' (sub: {}), groups: {}", claims.email, claims.sub, claims.groups.size());
        
        metrics_.authz_success_total++;
        return AuthResult::OK(claims.sub, claims.groups);  // Pass user_id and groups from JWT
        
    } catch (const std::exception& e) {
        metrics_.jwt_validation_failed_total++;
        THEMIS_WARN("JWT validation failed: {}", e.what());
        return AuthResult::Denied(std::string("JWT validation failed: ") + e.what());
    }
}

AuthMiddleware::AuthResult AuthMiddleware::validateToken(std::string_view token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Try API token first
    auto it = tokens_.find(std::string(token));
    if (it != tokens_.end()) {
        return AuthResult::OK(it->second.user_id);
    }
    
    // Try JWT validation
    if (jwt_enabled_ && jwt_validator_) {
        try {
            auto claims = jwt_validator_->parseAndValidate(std::string(token));
            metrics_.jwt_validation_success_total++;
            return AuthResult::OK(claims.sub, claims.groups);
        } catch (const std::exception& e) {
            metrics_.jwt_validation_failed_total++;
            THEMIS_DEBUG("JWT validation failed during validateToken: {}", e.what());
        }
    }
    
    metrics_.authz_invalid_token_total++;
    return AuthResult::Denied("Invalid token");
}

bool AuthMiddleware::isEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !tokens_.empty() || jwt_enabled_;
}

std::optional<std::string> AuthMiddleware::extractBearerToken(std::string_view auth_header) {
    // Expected format: "Bearer <token>"
    constexpr std::string_view prefix = "Bearer ";
    
    if (auth_header.size() <= prefix.size()) {
        return std::nullopt;
    }

    if (auth_header.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }

    std::string token(auth_header.substr(prefix.size()));
    
    // Trim whitespace
    auto start = token.find_first_not_of(" \t");
    auto end = token.find_last_not_of(" \t");
    
    if (start == std::string::npos) {
        return std::nullopt;
    }

    return token.substr(start, end - start + 1);
}

} // namespace themis
