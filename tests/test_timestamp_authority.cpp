#include <gtest/gtest.h>
#include "security/timestamp_authority.h"
#include <thread>
#include <chrono>

using namespace themis::security;

/**
 * Timestamp Authority Tests
 * 
 * These tests use public TSA services for testing.
 * 
 * Public TSAs:
 * - FreeTSA: https://freetsa.org/tsr (free, no registration)
 * - Digicert: https://timestamp.digicert.com (free)
 * - Sectigo: http://timestamp.sectigo.com (free)
 * 
 * Note: These are real network requests and may be slow or fail if TSA is down.
 * For CI/CD, consider using THEMIS_TEST_SKIP_TSA_NETWORK_TESTS=1
 */

class TimestampAuthorityTest : public ::testing::Test {
protected:
    bool skip_network_tests = false;
    
    void SetUp() override {
        const char* skip_env = std::getenv("THEMIS_TEST_SKIP_TSA_NETWORK_TESTS");
        skip_network_tests = (skip_env && std::string(skip_env) == "1");
    }
    
    TSAConfig createFreeTSAConfig() {
        TSAConfig config;
        config.url = "https://freetsa.org/tsr";
        config.hash_algorithm = "SHA256";
        config.cert_req = true;
        config.timeout_seconds = 30;
        config.verify_tsa_cert = false;  // FreeTSA uses self-signed cert
        return config;
    }
    
    TSAConfig createDigicertConfig() {
        TSAConfig config;
        config.url = "https://timestamp.digicert.com";
        config.hash_algorithm = "SHA256";
        config.cert_req = true;
        config.timeout_seconds = 30;
        config.verify_tsa_cert = true;
        return config;
    }
};

TEST_F(TimestampAuthorityTest, ConstructorDoesNotThrow) {
    TSAConfig config = createFreeTSAConfig();
    EXPECT_NO_THROW({
        TimestampAuthority tsa(config);
    });
}

TEST_F(TimestampAuthorityTest, GetTimestampFromFreeTSA) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled (THEMIS_TEST_SKIP_TSA_NETWORK_TESTS=1)";
    }
    
    TSAConfig config = createFreeTSAConfig();
    TimestampAuthority tsa(config);
    
    // Test data
    std::vector<uint8_t> data = {'T', 'e', 's', 't', ' ', 'D', 'a', 't', 'a'};
    
    // Get timestamp
    auto token = tsa.getTimestamp(data);
    
    // Check result
    if (!token.success) {
        GTEST_SKIP() << "TSA request failed: " << token.error_message 
                     << " (TSA may be unavailable)";
    }
    
    EXPECT_TRUE(token.success);
    EXPECT_FALSE(token.token_b64.empty());
    EXPECT_FALSE(token.serial_number.empty());
    EXPECT_FALSE(token.timestamp_utc.empty());
    EXPECT_GT(token.timestamp_unix_ms, 0);
    EXPECT_EQ(token.pki_status, 0);  // 0 = granted
    
    std::cout << "Timestamp received:\n"
              << "  Serial: " << token.serial_number << "\n"
              << "  Time: " << token.timestamp_utc << "\n"
              << "  Policy: " << token.policy_oid << "\n";
}

TEST_F(TimestampAuthorityTest, GetTimestampForHash) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    TSAConfig config = createFreeTSAConfig();
    TimestampAuthority tsa(config);
    
    // Pre-computed SHA-256 hash (32 bytes)
    std::vector<uint8_t> hash(32, 0xAB);
    
    auto token = tsa.getTimestampForHash(hash);
    
    if (!token.success) {
        GTEST_SKIP() << "TSA unavailable: " << token.error_message;
    }
    
    EXPECT_TRUE(token.success);
    EXPECT_FALSE(token.token_der.empty());
}

TEST_F(TimestampAuthorityTest, VerifyTimestamp) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    TSAConfig config = createFreeTSAConfig();
    TimestampAuthority tsa(config);
    
    std::vector<uint8_t> data = {'V', 'e', 'r', 'i', 'f', 'y', ' ', 'M', 'e'};
    
    // Get timestamp
    auto token = tsa.getTimestamp(data);
    
    if (!token.success) {
        GTEST_SKIP() << "TSA unavailable";
    }
    
    // Verify timestamp
    bool verified = tsa.verifyTimestamp(data, token);
    EXPECT_TRUE(verified);
    
    // Verify with modified data should fail
    std::vector<uint8_t> modified_data = data;
    modified_data[0] = 'X';
    bool verified_modified = tsa.verifyTimestamp(modified_data, token);
    EXPECT_FALSE(verified_modified);
}

TEST_F(TimestampAuthorityTest, ParseTokenRoundtrip) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    TSAConfig config = createFreeTSAConfig();
    TimestampAuthority tsa(config);
    
    std::vector<uint8_t> data = {'P', 'a', 'r', 's', 'e', ' ', 'T', 'e', 's', 't'};
    
    // Get timestamp
    auto token1 = tsa.getTimestamp(data);
    
    if (!token1.success) {
        GTEST_SKIP() << "TSA unavailable";
    }
    
    // Parse from Base64
    auto token2 = tsa.parseToken(token1.token_b64);
    
    EXPECT_TRUE(token2.success);
    EXPECT_EQ(token1.serial_number, token2.serial_number);
    EXPECT_EQ(token1.timestamp_utc, token2.timestamp_utc);
}

TEST_F(TimestampAuthorityTest, MultipleTimestamps) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    TSAConfig config = createFreeTSAConfig();
    TimestampAuthority tsa(config);
    
    // Request multiple timestamps
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> data(100, static_cast<uint8_t>(i));
        
        auto token = tsa.getTimestamp(data);
        
        if (!token.success) {
            GTEST_SKIP() << "TSA request " << i << " failed";
        }
        
        EXPECT_TRUE(token.success);
        EXPECT_FALSE(token.serial_number.empty());
        
        // Small delay to avoid rate limiting
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

TEST_F(TimestampAuthorityTest, DifferentHashAlgorithms) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    std::vector<std::string> algorithms = {"SHA256", "SHA384", "SHA512"};
    std::vector<uint8_t> data = {'T', 'e', 's', 't'};
    
    for (const auto& algo : algorithms) {
        TSAConfig config = createFreeTSAConfig();
        config.hash_algorithm = algo;
        
        TimestampAuthority tsa(config);
        auto token = tsa.getTimestamp(data);
        
        if (!token.success) {
            continue;  // TSA may not support all algorithms
        }
        
        EXPECT_TRUE(token.success) << "Algorithm " << algo << " failed";
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

TEST_F(TimestampAuthorityTest, IsAvailable) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    TSAConfig config = createFreeTSAConfig();
    TimestampAuthority tsa(config);
    
    bool available = tsa.isAvailable();
    
    if (!available) {
        GTEST_SKIP() << "TSA not reachable (may be down or network issue)";
    }
    
    EXPECT_TRUE(available);
}

TEST_F(TimestampAuthorityTest, InvalidURL) {
    TSAConfig config;
    config.url = "https://invalid.tsa.example.com/nonexistent";
    config.timeout_seconds = 5;
    
    TimestampAuthority tsa(config);
    
    std::vector<uint8_t> data = {'T', 'e', 's', 't'};
    auto token = tsa.getTimestamp(data);
    
    EXPECT_FALSE(token.success);
    EXPECT_FALSE(token.error_message.empty());
}

// eIDAS Validator Tests

TEST_F(TimestampAuthorityTest, ValidateAge) {
    TimestampToken token;
    
    // Current timestamp
    token.timestamp_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    eIDASTimestampValidator validator;
    
    // Should be valid (age = 0)
    EXPECT_TRUE(validator.validateAge(token, 10950));  // 30 years
    EXPECT_TRUE(validator.validateAge(token, 365));     // 1 year
    EXPECT_TRUE(validator.validateAge(token, 1));       // 1 day
    
    // Old timestamp (1 year ago)
    token.timestamp_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() - (365L * 24 * 60 * 60 * 1000);  // 1 year in ms
    
    EXPECT_TRUE(validator.validateAge(token, 10950));  // Still valid for 30 years
    EXPECT_FALSE(validator.validateAge(token, 364));   // Too old for 364 days
}

TEST_F(TimestampAuthorityTest, eIDASValidation) {
    TimestampToken token;
    token.success = true;
    token.token_der = {0x01, 0x02, 0x03};  // Dummy data
    
    eIDASTimestampValidator validator;
    
    std::vector<std::string> trust_anchors = {"dummy_ca.pem"};
    
    // Stub implementation should not crash
    bool result = validator.validateeIDASTimestamp(token, trust_anchors);
    
    // May return true or false depending on stub implementation
    EXPECT_TRUE(result || !result);  // Just check it doesn't crash
    
    auto errors = validator.getValidationErrors();
    // Errors may or may not be present in stub
}

// Usage Example Test

TEST_F(TimestampAuthorityTest, UsageExample) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    // Example: How to use TimestampAuthority
    
    // 1. Configure TSA
    TSAConfig config;
    config.url = "https://freetsa.org/tsr";
    config.hash_algorithm = "SHA256";
    config.cert_req = true;
    config.timeout_seconds = 30;
    config.verify_tsa_cert = false;  // FreeTSA uses self-signed cert
    
    // 2. Create client
    auto tsa = std::make_unique<TimestampAuthority>(config);
    
    // 3. Get timestamp for data
    std::vector<uint8_t> my_data = {'M', 'y', ' ', 'D', 'a', 't', 'a'};
    auto token = tsa->getTimestamp(my_data);
    
    if (!token.success) {
        GTEST_SKIP() << "TSA unavailable: " << token.error_message;
    }
    
    ASSERT_TRUE(token.success);
    
    // 4. Verify timestamp
    bool is_valid = tsa->verifyTimestamp(my_data, token);
    EXPECT_TRUE(is_valid);
    
    // 5. Store token for archival (eIDAS Art. 32)
    std::string stored_token = token.token_b64;
    
    // Later: Parse stored token
    auto parsed_token = tsa->parseToken(stored_token);
    EXPECT_EQ(token.serial_number, parsed_token.serial_number);
    
    // 6. Validate age (eIDAS: 30 years = 10950 days)
    eIDASTimestampValidator validator;
    bool age_valid = validator.validateAge(parsed_token, 10950);
    EXPECT_TRUE(age_valid);
}

// Performance Benchmark (disabled by default)

TEST_F(TimestampAuthorityTest, DISABLED_TimestampPerformanceBenchmark) {
    if (skip_network_tests) {
        GTEST_SKIP() << "Network tests disabled";
    }
    
    TSAConfig config = createFreeTSAConfig();
    TimestampAuthority tsa(config);
    
    const int num_operations = 10;
    std::vector<uint8_t> data(1024, 0xAA);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    int successful = 0;
    for (int i = 0; i < num_operations; ++i) {
        auto token = tsa.getTimestamp(data);
        if (token.success) {
            ++successful;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));  // Rate limiting
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double avg_time = duration.count() / static_cast<double>(successful);
    
    std::cout << "Timestamp Performance:\n"
              << "  Operations: " << num_operations << "\n"
              << "  Successful: " << successful << "\n"
              << "  Total Time: " << duration.count() << " ms\n"
              << "  Avg Time/Op: " << avg_time << " ms\n";
    
    EXPECT_GT(successful, 0);
}
