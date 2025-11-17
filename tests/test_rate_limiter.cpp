#include <gtest/gtest.h>
#include "server/rate_limiter.h"
#include <thread>
#include <chrono>

using namespace themis::server;

class RateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.bucket_capacity = 10;
        config_.refill_rate = 10.0; // 10 requests per second
        config_.per_ip_enabled = true;
        config_.per_user_enabled = true;
    }
    
    RateLimitConfig config_;
};

// ============================================================================
// TokenBucket Tests
// ============================================================================

TEST_F(RateLimiterTest, TokenBucket_InitialCapacity) {
    TokenBucket bucket(10, 1.0);
    EXPECT_EQ(bucket.getTokens(), 10.0);
}

TEST_F(RateLimiterTest, TokenBucket_ConsumeTokens) {
    TokenBucket bucket(10, 1.0);
    
    EXPECT_TRUE(bucket.tryConsume(1));
    EXPECT_NEAR(bucket.getTokens(), 9.0, 0.1);
    
    EXPECT_TRUE(bucket.tryConsume(5));
    EXPECT_NEAR(bucket.getTokens(), 4.0, 0.1);
}

TEST_F(RateLimiterTest, TokenBucket_InsufficientTokens) {
    TokenBucket bucket(5, 1.0);
    
    EXPECT_TRUE(bucket.tryConsume(5));
    EXPECT_FALSE(bucket.tryConsume(1)); // No tokens left
}

TEST_F(RateLimiterTest, TokenBucket_Refill) {
    TokenBucket bucket(10, 10.0); // 10 tokens per second
    
    // Consume all tokens
    EXPECT_TRUE(bucket.tryConsume(10));
    EXPECT_FALSE(bucket.tryConsume(1));
    
    // Wait for refill
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Should have ~5 tokens after 0.5 seconds
    EXPECT_TRUE(bucket.tryConsume(4));
}

TEST_F(RateLimiterTest, TokenBucket_RefillCapped) {
    TokenBucket bucket(10, 100.0); // High refill rate
    
    EXPECT_TRUE(bucket.tryConsume(5));
    
    // Wait long enough to exceed capacity
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Should be capped at capacity (10)
    EXPECT_NEAR(bucket.getTokens(), 10.0, 0.5);
}

TEST_F(RateLimiterTest, TokenBucket_Reset) {
    TokenBucket bucket(10, 1.0);
    
    bucket.tryConsume(8);
    EXPECT_NEAR(bucket.getTokens(), 2.0, 0.1);
    
    bucket.reset();
    EXPECT_EQ(bucket.getTokens(), 10.0);
}

TEST_F(RateLimiterTest, TokenBucket_RetryAfter) {
    TokenBucket bucket(10, 10.0); // 10 tokens/sec
    
    // Consume all tokens
    bucket.tryConsume(10);
    
    // Should need ~100ms for 1 token
    uint64_t retry_ms = bucket.getRetryAfterMs();
    EXPECT_GT(retry_ms, 50);
    EXPECT_LT(retry_ms, 150);
}

// ============================================================================
// RateLimiter Tests
// ============================================================================

TEST_F(RateLimiterTest, AllowRequest_BasicIPLimit) {
    RateLimiter limiter(config_);
    
    // First 10 requests should succeed
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1")) 
            << "Request " << i << " should be allowed";
    }
    
    // 11th request should be rate limited
    EXPECT_FALSE(limiter.allowRequest("192.168.1.1"));
}

TEST_F(RateLimiterTest, AllowRequest_MultipleIPs) {
    RateLimiter limiter(config_);
    
    // Different IPs have independent buckets
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1"));
        EXPECT_TRUE(limiter.allowRequest("192.168.1.2"));
        EXPECT_TRUE(limiter.allowRequest("192.168.1.3"));
    }
    
    // Each IP exhausted their own bucket
    EXPECT_FALSE(limiter.allowRequest("192.168.1.1"));
    EXPECT_FALSE(limiter.allowRequest("192.168.1.2"));
    EXPECT_FALSE(limiter.allowRequest("192.168.1.3"));
}

TEST_F(RateLimiterTest, AllowRequest_PerUserLimit) {
    RateLimiter limiter(config_);
    
    // Same user from different IPs
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1", "user123"));
    }
    
    // User rate limit exceeded (even from different IP)
    EXPECT_FALSE(limiter.allowRequest("192.168.1.2", "user123"));
}

TEST_F(RateLimiterTest, AllowRequest_Whitelist) {
    config_.whitelist_ips.push_back("10.0.0.1");
    RateLimiter limiter(config_);
    
    // Whitelisted IP has unlimited requests
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(limiter.allowRequest("10.0.0.1"));
    }
}

TEST_F(RateLimiterTest, AllowRequest_CustomLimit) {
    config_.custom_limits["192.168.1.100"] = 50; // Higher limit for specific IP
    RateLimiter limiter(config_);
    
    // Regular IP limited to 10
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1"));
    }
    EXPECT_FALSE(limiter.allowRequest("192.168.1.1"));
    
    // Custom IP limited to 50
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.100"));
    }
    EXPECT_FALSE(limiter.allowRequest("192.168.1.100"));
}

TEST_F(RateLimiterTest, GetRetryAfter) {
    RateLimiter limiter(config_);
    
    // Exhaust bucket
    for (int i = 0; i < 10; ++i) {
        limiter.allowRequest("192.168.1.1");
    }
    
    // Should have retry-after time
    uint32_t retry_after = limiter.getRetryAfter("192.168.1.1");
    EXPECT_GT(retry_after, 0);
    EXPECT_LT(retry_after, 2); // With 10 req/sec, should be < 1 second
}

TEST_F(RateLimiterTest, Statistics) {
    RateLimiter limiter(config_);
    
    // Make some requests
    for (int i = 0; i < 5; ++i) {
        limiter.allowRequest("192.168.1.1");
    }
    for (int i = 0; i < 5; ++i) {
        limiter.allowRequest("192.168.1.2");
    }
    
    auto stats = limiter.getStatistics();
    EXPECT_EQ(stats.total_requests, 10);
    EXPECT_EQ(stats.allowed_requests, 10);
    EXPECT_EQ(stats.rejected_requests, 0);
    EXPECT_EQ(stats.active_ip_buckets, 2);
    
    // Trigger some rejections
    for (int i = 0; i < 5; ++i) {
        limiter.allowRequest("192.168.1.1");
    }
    
    stats = limiter.getStatistics();
    EXPECT_EQ(stats.total_requests, 15);
    EXPECT_EQ(stats.allowed_requests, 10);
    EXPECT_EQ(stats.rejected_requests, 5);
}

TEST_F(RateLimiterTest, Reset) {
    RateLimiter limiter(config_);
    
    // Exhaust buckets
    for (int i = 0; i < 10; ++i) {
        limiter.allowRequest("192.168.1.1");
    }
    EXPECT_FALSE(limiter.allowRequest("192.168.1.1"));
    
    // Reset should clear all buckets
    limiter.reset();
    
    // Should work again
    EXPECT_TRUE(limiter.allowRequest("192.168.1.1"));
    
    auto stats = limiter.getStatistics();
    EXPECT_EQ(stats.total_requests, 1);
}

TEST_F(RateLimiterTest, UpdateConfig) {
    RateLimiter limiter(config_);
    
    // Use up 10 requests
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1"));
    }
    EXPECT_FALSE(limiter.allowRequest("192.168.1.1"));
    
    // Increase limit
    config_.bucket_capacity = 20;
    limiter.updateConfig(config_);
    
    // New buckets will have new capacity
    EXPECT_TRUE(limiter.allowRequest("192.168.1.2"));
}

TEST_F(RateLimiterTest, PerIPDisabled) {
    config_.per_ip_enabled = false;
    config_.per_user_enabled = true;
    RateLimiter limiter(config_);
    
    // IP rate limiting disabled - unlimited for same IP without user
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1", ""));
    }
    
    // But user rate limiting still works
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1", "user1"));
    }
    EXPECT_FALSE(limiter.allowRequest("192.168.1.1", "user1"));
}

TEST_F(RateLimiterTest, Concurrency) {
    RateLimiter limiter(config_);
    
    std::atomic<int> allowed{0};
    std::atomic<int> rejected{0};
    
    // Spawn multiple threads making requests
    std::vector<std::thread> threads;
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&limiter, &allowed, &rejected, t]() {
            for (int i = 0; i < 10; ++i) {
                if (limiter.allowRequest("192.168.1.1")) {
                    allowed++;
                } else {
                    rejected++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Only 10 requests should be allowed total
    EXPECT_EQ(allowed.load(), 10);
    EXPECT_EQ(rejected.load(), 40);
}

TEST_F(RateLimiterTest, RefillOverTime) {
    config_.refill_rate = 5.0; // 5 tokens per second
    RateLimiter limiter(config_);
    
    // Use 5 tokens
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1"));
    }
    
    // Wait 1 second for ~5 tokens to refill
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Should have ~5 more tokens available
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.allowRequest("192.168.1.1"));
    }
}

TEST_F(RateLimiterTest, UsageExample) {
    // Realistic configuration
    RateLimitConfig prod_config;
    prod_config.bucket_capacity = 100;
    prod_config.refill_rate = 100.0 / 60.0; // 100 req/min
    prod_config.per_ip_enabled = true;
    prod_config.per_user_enabled = true;
    prod_config.whitelist_ips = {"127.0.0.1", "10.0.0.0/8"};
    
    RateLimiter limiter(prod_config);
    
    // Simulate API requests
    std::string client_ip = "203.0.113.42";
    std::string user_id = "alice";
    
    // Normal usage - should work
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(limiter.allowRequest(client_ip, user_id));
    }
    
    // Burst - should work up to capacity
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(limiter.allowRequest(client_ip, user_id));
    }
    
    // Exceeded limit
    EXPECT_FALSE(limiter.allowRequest(client_ip, user_id));
    
    // Get retry time
    uint32_t retry_after = limiter.getRetryAfter(client_ip, user_id);
    EXPECT_GT(retry_after, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
