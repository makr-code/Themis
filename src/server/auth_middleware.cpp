#include "server/auth_middleware.h"
#include "utils/logger.h"
#include <sstream>

namespace themis {

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
    
    auto it = tokens_.find(std::string(token));
    if (it == tokens_.end()) {
        metrics_.authz_invalid_token_total++;
        return AuthResult::Denied("Invalid or missing token");
    }

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

AuthMiddleware::AuthResult AuthMiddleware::validateToken(std::string_view token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tokens_.find(std::string(token));
    if (it == tokens_.end()) {
        metrics_.authz_invalid_token_total++;
        return AuthResult::Denied("Invalid token");
    }

    return AuthResult::OK(it->second.user_id);
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

bool AuthMiddleware::isEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !tokens_.empty();
}

} // namespace themis
