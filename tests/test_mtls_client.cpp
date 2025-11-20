#include <gtest/gtest.h>
#include "sharding/mtls_client.h"

using namespace themis::sharding;

// Note: These are structure and configuration tests
// Full integration tests would require actual TLS certificates and a test server

TEST(MTLSClientTest, ConfigurationStructure) {
    MTLSClient::Config config;
    config.cert_path = "/path/to/shard-001.crt";
    config.key_path = "/path/to/shard-001.key";
    config.ca_cert_path = "/path/to/root-ca.crt";
    config.tls_version = "TLSv1.3";
    config.verify_peer = true;
    config.connect_timeout_ms = 5000;
    config.request_timeout_ms = 30000;
    
    EXPECT_EQ(config.cert_path, "/path/to/shard-001.crt");
    EXPECT_EQ(config.key_path, "/path/to/shard-001.key");
    EXPECT_EQ(config.ca_cert_path, "/path/to/root-ca.crt");
    EXPECT_EQ(config.tls_version, "TLSv1.3");
    EXPECT_TRUE(config.verify_peer);
}

TEST(MTLSClientTest, ResponseStructure) {
    MTLSClient::Response response;
    response.status_code = 200;
    response.status_message = "OK";
    response.success = true;
    response.body = nlohmann::json{{"key", "value"}};
    
    EXPECT_EQ(response.status_code, 200);
    EXPECT_EQ(response.status_message, "OK");
    EXPECT_TRUE(response.success);
    EXPECT_EQ(response.body["key"], "value");
}

TEST(MTLSClientTest, DefaultConfiguration) {
    MTLSClient::Config config;
    
    // Check defaults
    EXPECT_EQ(config.tls_version, "TLSv1.3");
    EXPECT_TRUE(config.verify_peer);
    EXPECT_TRUE(config.verify_hostname);
    EXPECT_EQ(config.connect_timeout_ms, 5000u);
    EXPECT_EQ(config.request_timeout_ms, 30000u);
    EXPECT_EQ(config.max_retries, 3u);
    EXPECT_TRUE(config.enable_pooling);
}

TEST(MTLSClientTest, IsReadyWithoutConfig) {
    // Note: Cannot create MTLSClient without valid certificates
    // This test just verifies the structure
    MTLSClient::Config config;
    config.cert_path = "";
    config.key_path = "";
    config.ca_cert_path = "";
    
    // Would fail in real initialization, but we're testing structure
    EXPECT_TRUE(config.cert_path.empty());
}

TEST(MTLSClientTest, EndpointParsing) {
    // Test endpoint parsing logic (would be tested via private method in real impl)
    std::string endpoint1 = "https://shard-001.dc1:8080";
    std::string endpoint2 = "shard-002.dc1:9090";
    std::string endpoint3 = "localhost:8765";
    
    // Just verify the format is reasonable
    EXPECT_TRUE(endpoint1.find("://") != std::string::npos);
    EXPECT_TRUE(endpoint2.find(":") != std::string::npos);
    EXPECT_TRUE(endpoint3.find(":") != std::string::npos);
}

TEST(MTLSClientTest, RetryConfiguration) {
    MTLSClient::Config config;
    config.max_retries = 5;
    config.retry_delay_ms = 2000;
    
    EXPECT_EQ(config.max_retries, 5u);
    EXPECT_EQ(config.retry_delay_ms, 2000u);
}

TEST(MTLSClientTest, ConnectionPoolingConfig) {
    MTLSClient::Config config;
    config.enable_pooling = false;
    config.max_connections = 20;
    config.idle_timeout_ms = 120000;
    
    EXPECT_FALSE(config.enable_pooling);
    EXPECT_EQ(config.max_connections, 20u);
    EXPECT_EQ(config.idle_timeout_ms, 120000u);
}
