#include <gtest/gtest.h>
#include "server/auth_middleware.h"

using namespace themis;

class AuthMiddlewareTest : public ::testing::Test {
protected:
    AuthMiddleware auth_;
    
    void SetUp() override {
        // Add test tokens
        AuthMiddleware::TokenConfig admin_token{
            .token = "admin-token-123",
            .user_id = "admin",
            .scopes = {"admin", "config:write", "config:read", "cdc:read", "metrics:read"}
        };
        
        AuthMiddleware::TokenConfig readonly_token{
            .token = "readonly-token-456",
            .user_id = "viewer",
            .scopes = {"cdc:read", "metrics:read"}
        };
        
        auth_.addToken(admin_token);
        auth_.addToken(readonly_token);
    }
};

TEST_F(AuthMiddlewareTest, ExtractBearerToken) {
    auto token = AuthMiddleware::extractBearerToken("Bearer abc123");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, "abc123");
    
    token = AuthMiddleware::extractBearerToken("Bearer   xyz789  ");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, "xyz789");
    
    token = AuthMiddleware::extractBearerToken("InvalidFormat");
    EXPECT_FALSE(token.has_value());
    
    token = AuthMiddleware::extractBearerToken("");
    EXPECT_FALSE(token.has_value());
}

TEST_F(AuthMiddlewareTest, ValidateToken_Valid) {
    auto result = auth_.validateToken("admin-token-123");
    EXPECT_TRUE(result.authorized);
    EXPECT_EQ(result.user_id, "admin");
    
    result = auth_.validateToken("readonly-token-456");
    EXPECT_TRUE(result.authorized);
    EXPECT_EQ(result.user_id, "viewer");
}

TEST_F(AuthMiddlewareTest, ValidateToken_Invalid) {
    auto result = auth_.validateToken("invalid-token");
    EXPECT_FALSE(result.authorized);
    EXPECT_FALSE(result.reason.empty());
}

TEST_F(AuthMiddlewareTest, Authorize_AdminHasAllScopes) {
    auto result = auth_.authorize("admin-token-123", "admin");
    EXPECT_TRUE(result.authorized);
    
    result = auth_.authorize("admin-token-123", "config:write");
    EXPECT_TRUE(result.authorized);
    
    result = auth_.authorize("admin-token-123", "cdc:read");
    EXPECT_TRUE(result.authorized);
}

TEST_F(AuthMiddlewareTest, Authorize_ReadonlyLimitedScopes) {
    auto result = auth_.authorize("readonly-token-456", "cdc:read");
    EXPECT_TRUE(result.authorized);
    
    result = auth_.authorize("readonly-token-456", "metrics:read");
    EXPECT_TRUE(result.authorized);
    
    // Readonly should NOT have admin or config:write
    result = auth_.authorize("readonly-token-456", "admin");
    EXPECT_FALSE(result.authorized);
    EXPECT_FALSE(result.reason.empty());
    
    result = auth_.authorize("readonly-token-456", "config:write");
    EXPECT_FALSE(result.authorized);
}

TEST_F(AuthMiddlewareTest, Authorize_InvalidToken) {
    auto result = auth_.authorize("invalid-token", "admin");
    EXPECT_FALSE(result.authorized);
}

TEST_F(AuthMiddlewareTest, Metrics_TrackAuthAttempts) {
    auto& metrics = auth_.getMetrics();
    auto initial_success = metrics.authz_success_total.load();
    auto initial_denied = metrics.authz_denied_total.load();
    auto initial_invalid = metrics.authz_invalid_token_total.load();
    
    // Success
    auth_.authorize("admin-token-123", "admin");
    EXPECT_EQ(metrics.authz_success_total.load(), initial_success + 1);
    
    // Denied (valid token, missing scope)
    auth_.authorize("readonly-token-456", "admin");
    EXPECT_EQ(metrics.authz_denied_total.load(), initial_denied + 1);
    
    // Invalid token
    auth_.authorize("bad-token", "admin");
    EXPECT_EQ(metrics.authz_invalid_token_total.load(), initial_invalid + 1);
}

TEST_F(AuthMiddlewareTest, RemoveToken) {
    auto result = auth_.validateToken("admin-token-123");
    EXPECT_TRUE(result.authorized);
    
    auth_.removeToken("admin-token-123");
    
    result = auth_.validateToken("admin-token-123");
    EXPECT_FALSE(result.authorized);
}

TEST_F(AuthMiddlewareTest, ClearTokens) {
    auth_.clearTokens();
    
    auto result = auth_.validateToken("admin-token-123");
    EXPECT_FALSE(result.authorized);
    
    result = auth_.validateToken("readonly-token-456");
    EXPECT_FALSE(result.authorized);
}
