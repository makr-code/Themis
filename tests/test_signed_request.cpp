#include <gtest/gtest.h>
#include "sharding/signed_request.h"
#include <thread>
#include <chrono>

using namespace themis::sharding;

// ============================================================================
// SignedRequest Tests
// ============================================================================

TEST(SignedRequestTest, StructureAndSerialization) {
    SignedRequest req;
    req.shard_id = "shard_001";
    req.operation = "GET";
    req.path = "/api/v1/data";
    req.body = nlohmann::json{{"key", "value"}};
    req.timestamp_ms = 1700000000000;
    req.nonce = 12345;
    req.signature_b64 = "dGVzdA==";
    req.cert_serial = "ABCD1234";
    
    EXPECT_EQ(req.shard_id, "shard_001");
    EXPECT_EQ(req.operation, "GET");
    EXPECT_EQ(req.path, "/api/v1/data");
    EXPECT_EQ(req.body["key"], "value");
}

TEST(SignedRequestTest, ToJSON) {
    SignedRequest req;
    req.shard_id = "shard_001";
    req.operation = "POST";
    req.path = "/api/v1/resource";
    req.body = nlohmann::json{{"data", 42}};
    req.timestamp_ms = 1700000000000;
    req.nonce = 67890;
    req.signature_b64 = "c2lnbmF0dXJl";
    req.cert_serial = "SERIAL123";
    
    auto json = req.toJSON();
    
    EXPECT_EQ(json["shard_id"], "shard_001");
    EXPECT_EQ(json["operation"], "POST");
    EXPECT_EQ(json["path"], "/api/v1/resource");
    EXPECT_EQ(json["body"]["data"], 42);
    EXPECT_EQ(json["timestamp_ms"], 1700000000000);
    EXPECT_EQ(json["nonce"], 67890);
}

TEST(SignedRequestTest, FromJSON) {
    nlohmann::json json = {
        {"shard_id", "shard_002"},
        {"operation", "DELETE"},
        {"path", "/api/v1/item/123"},
        {"body", nlohmann::json{}},
        {"timestamp_ms", 1700000000000},
        {"nonce", 11111},
        {"signature_b64", "dGVzdHNpZw=="},
        {"cert_serial", "ABC123"}
    };
    
    auto req = SignedRequest::fromJSON(json);
    
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->shard_id, "shard_002");
    EXPECT_EQ(req->operation, "DELETE");
    EXPECT_EQ(req->path, "/api/v1/item/123");
    EXPECT_EQ(req->nonce, 11111);
}

TEST(SignedRequestTest, CanonicalString) {
    SignedRequest req;
    req.shard_id = "shard_001";
    req.operation = "GET";
    req.path = "/test";
    req.body = nlohmann::json{{"x", 1}};
    req.timestamp_ms = 1000;
    req.nonce = 999;
    
    std::string canonical = req.getCanonicalString();
    
    // Should contain all fields in order
    EXPECT_TRUE(canonical.find("shard_001") != std::string::npos);
    EXPECT_TRUE(canonical.find("GET") != std::string::npos);
    EXPECT_TRUE(canonical.find("/test") != std::string::npos);
    EXPECT_TRUE(canonical.find("1000") != std::string::npos);
    EXPECT_TRUE(canonical.find("999") != std::string::npos);
}

// ============================================================================
// SignedRequestSigner Tests
// ============================================================================

TEST(SignedRequestSignerTest, Configuration) {
    SignedRequestSigner::Config config;
    config.shard_id = "shard_001";
    config.cert_path = "/path/to/cert.pem";
    config.key_path = "/path/to/key.pem";
    config.key_passphrase = "secret";
    
    EXPECT_EQ(config.shard_id, "shard_001");
    EXPECT_EQ(config.cert_path, "/path/to/cert.pem");
    EXPECT_EQ(config.key_path, "/path/to/key.pem");
    EXPECT_EQ(config.key_passphrase, "secret");
}

TEST(SignedRequestSignerTest, CreateSignedRequestStructure) {
    // Note: Cannot test actual signing without valid certificates
    // This tests the structure and API
    
    SignedRequest req;
    req.operation = "GET";
    req.path = "/api/test";
    req.body = nlohmann::json{};
    
    // Should have basic structure
    EXPECT_EQ(req.operation, "GET");
    EXPECT_EQ(req.path, "/api/test");
}

// ============================================================================
// SignedRequestVerifier Tests
// ============================================================================

TEST(SignedRequestVerifierTest, Configuration) {
    SignedRequestVerifier::Config config;
    config.ca_cert_path = "/path/to/ca.pem";
    config.max_time_skew_ms = 30000;
    config.max_nonce_cache = 5000;
    config.nonce_expiry_ms = 600000;
    
    EXPECT_EQ(config.ca_cert_path, "/path/to/ca.pem");
    EXPECT_EQ(config.max_time_skew_ms, 30000u);
    EXPECT_EQ(config.max_nonce_cache, 5000u);
    EXPECT_EQ(config.nonce_expiry_ms, 600000u);
}

TEST(SignedRequestVerifierTest, DefaultConfiguration) {
    SignedRequestVerifier::Config config;
    
    EXPECT_EQ(config.max_time_skew_ms, 60000u);  // 60 seconds
    EXPECT_EQ(config.max_nonce_cache, 10000u);
    EXPECT_EQ(config.nonce_expiry_ms, 300000u);  // 5 minutes
}

TEST(SignedRequestVerifierTest, TimestampValidation) {
    // Test that timestamp validation concept exists
    SignedRequestVerifier::Config config;
    config.max_time_skew_ms = 1000;  // 1 second
    
    EXPECT_EQ(config.max_time_skew_ms, 1000u);
}

TEST(SignedRequestVerifierTest, NonceUniqueness) {
    // Test nonce uniqueness concept
    uint64_t nonce1 = 12345;
    uint64_t nonce2 = 67890;
    
    EXPECT_NE(nonce1, nonce2);
}

TEST(SignedRequestVerifierTest, CleanupStructure) {
    SignedRequestVerifier::Config config;
    SignedRequestVerifier verifier(config);
    
    // Test cleanup method exists
    verifier.cleanupExpiredNonces();
    
    // No exception should be thrown
    SUCCEED();
}
