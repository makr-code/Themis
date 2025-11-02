#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <optional>
#include <functional>

namespace themis {

/// Simple token-based authorization with scopes
/// MVP: API tokens with comma-separated scopes (e.g., "admin,config:write,cdc:read")
class AuthMiddleware {
public:
    struct AuthResult {
        bool authorized = false;
        std::string user_id;
        std::string reason; // for audit logs
        static AuthResult OK(std::string_view uid) { return {true, std::string(uid), ""}; }
        static AuthResult Denied(std::string msg) { return {false, "", std::move(msg)}; }
    };

    struct TokenConfig {
        std::string token;
        std::string user_id;
        std::unordered_set<std::string> scopes;
    };

    /// Configure allowed tokens (typically loaded from config file)
    void addToken(const TokenConfig& config);
    void removeToken(std::string_view token);
    void clearTokens();

    /// Check if token has required scope
    /// @param token Bearer token from Authorization header
    /// @param required_scope Required scope (e.g., "admin", "config:write", "cdc:read", "metrics:read")
    AuthResult authorize(std::string_view token, std::string_view required_scope) const;

    /// Check if token is valid (any scope)
    AuthResult validateToken(std::string_view token) const;

    /// Extract token from "Bearer <token>" header value
    static std::optional<std::string> extractBearerToken(std::string_view auth_header);

    /// Get metrics (for Prometheus)
    struct Metrics {
        std::atomic<uint64_t> authz_success_total{0};
        std::atomic<uint64_t> authz_denied_total{0};
        std::atomic<uint64_t> authz_invalid_token_total{0};
    };

    const Metrics& getMetrics() const { return metrics_; }

    // Returns true if at least one token is configured
    bool isEnabled() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TokenConfig> tokens_; // token -> config
    mutable Metrics metrics_;
};

} // namespace themis
