#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <memory>

namespace themis {
namespace server {

/**
 * @brief Token Bucket configuration for rate limiting
 */
struct RateLimitConfig {
    // Maximum number of tokens in bucket (burst capacity)
    size_t bucket_capacity = 100;
    
    // Tokens refilled per second
    double refill_rate = 100.0 / 60.0; // 100 requests per minute default
    
    // Time window for rate limit (seconds)
    uint32_t window_seconds = 60;
    
    // Enable per-IP rate limiting
    bool per_ip_enabled = true;
    
    // Enable per-user rate limiting (requires authentication)
    bool per_user_enabled = true;
    
    // Whitelist IPs (no rate limiting)
    std::vector<std::string> whitelist_ips;
    
    // Custom rate limits for specific IPs/users
    std::unordered_map<std::string, size_t> custom_limits;
};

/**
 * @brief Token Bucket for rate limiting
 * 
 * Implements the Token Bucket algorithm:
 * - Bucket holds tokens (capacity limit)
 * - Tokens refill at constant rate
 * - Each request consumes 1 token
 * - Request rejected if no tokens available
 */
class TokenBucket {
public:
    TokenBucket(size_t capacity, double refill_rate);
    
    /**
     * @brief Try to consume tokens from bucket
     * @param tokens Number of tokens to consume (default: 1)
     * @return true if tokens consumed, false if insufficient tokens
     */
    bool tryConsume(size_t tokens = 1);
    
    /**
     * @brief Get current token count
     */
    double getTokens() const;
    
    /**
     * @brief Get time until next token available (milliseconds)
     */
    uint64_t getRetryAfterMs() const;
    
    /**
     * @brief Reset bucket to full capacity
     */
    void reset();

private:
    void refill();
    
    size_t capacity_;
    double tokens_;
    double refill_rate_;
    std::chrono::steady_clock::time_point last_refill_;
    mutable std::mutex mutex_;
};

/**
 * @brief Rate Limiter with per-IP and per-user tracking
 * 
 * Features:
 * - Token bucket algorithm for smooth rate limiting
 * - Per-IP and per-user buckets
 * - Configurable limits and whitelists
 * - Thread-safe
 * - Automatic cleanup of old buckets
 */
class RateLimiter {
public:
    explicit RateLimiter(const RateLimitConfig& config = RateLimitConfig());
    
    /**
     * @brief Check if request is allowed
     * @param ip Client IP address
     * @param user_id Optional user identifier (from JWT/auth)
     * @return true if request allowed, false if rate limit exceeded
     */
    bool allowRequest(const std::string& ip, const std::string& user_id = "");
    
    /**
     * @brief Get retry-after time in seconds for rate-limited client
     * @param ip Client IP address
     * @param user_id Optional user identifier
     * @return Seconds until next request allowed (0 if not rate limited)
     */
    uint32_t getRetryAfter(const std::string& ip, const std::string& user_id = "") const;
    
    /**
     * @brief Check if IP is whitelisted
     */
    bool isWhitelisted(const std::string& ip) const;
    
    /**
     * @brief Update configuration at runtime
     */
    void updateConfig(const RateLimitConfig& config);
    
    /**
     * @brief Get current statistics
     */
    struct Statistics {
        size_t total_requests = 0;
        size_t allowed_requests = 0;
        size_t rejected_requests = 0;
        size_t active_ip_buckets = 0;
        size_t active_user_buckets = 0;
    };
    
    Statistics getStatistics() const;
    
    /**
     * @brief Clear all buckets (for testing)
     */
    void reset();
    
    /**
     * @brief Cleanup old inactive buckets (called periodically)
     */
    void cleanup();

private:
    std::shared_ptr<TokenBucket> getOrCreateBucket(
        const std::string& key,
        std::unordered_map<std::string, std::shared_ptr<TokenBucket>>& buckets
    );
    
    RateLimitConfig config_;
    
    // Per-IP buckets
    std::unordered_map<std::string, std::shared_ptr<TokenBucket>> ip_buckets_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> ip_last_access_;
    
    // Per-user buckets
    std::unordered_map<std::string, std::shared_ptr<TokenBucket>> user_buckets_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> user_last_access_;
    
    // Statistics
    mutable Statistics stats_;
    
    mutable std::mutex mutex_;
    
    // Cleanup interval (5 minutes)
    static constexpr uint32_t CLEANUP_INTERVAL_SECONDS = 300;
    std::chrono::steady_clock::time_point last_cleanup_;
};

} // namespace server
} // namespace themis
