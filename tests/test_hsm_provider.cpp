#include <gtest/gtest.h>
#include "security/hsm_provider.h"
#include <filesystem>
#include <fstream>

using namespace themis::security;

/**
 * HSM Provider Tests
 * 
 * These tests require SoftHSM2 to be installed:
 * 
 * Installation:
 * - Ubuntu/Debian: sudo apt-get install softhsm2
 * - macOS: brew install softhsm
 * - Windows: Download from https://github.com/opendnssec/SoftHSMv2/releases
 * 
 * Configuration (one-time setup):
 * ```bash
 * # Initialize token
 * softhsm2-util --init-token --slot 0 --label "themis-test" --pin 1234 --so-pin 5678
 * 
 * # Generate test key
 * pkcs11-tool --module /usr/lib/softhsm/libsofthsm2.so \
 *   --login --pin 1234 \
 *   --keypairgen --key-type RSA:2048 \
 *   --label "themis-signing-key"
 * ```
 * 
 * Environment Variables:
 * - THEMIS_TEST_HSM_LIBRARY: Path to PKCS#11 library (default: /usr/lib/softhsm/libsofthsm2.so)
 * - THEMIS_TEST_HSM_PIN: HSM PIN (default: 1234)
 */

class HSMProviderTest : public ::testing::Test {
protected:
    std::string hsm_library_path;
    std::string hsm_pin;
    uint32_t hsm_slot = 0;
    
    void SetUp() override {
        // Get HSM library path from environment or use default
        const char* env_lib = std::getenv("THEMIS_TEST_HSM_LIBRARY");
        if (env_lib) {
            hsm_library_path = env_lib;
        } else {
            // Try common locations
            std::vector<std::string> common_paths = {
                "/usr/lib/softhsm/libsofthsm2.so",                    // Debian/Ubuntu
                "/usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so",  // Ubuntu
                "/usr/local/lib/softhsm/libsofthsm2.so",             // macOS/Linux
                "/opt/homebrew/lib/softhsm/libsofthsm2.so",          // macOS M1
            };
            
            for (const auto& path : common_paths) {
                if (std::filesystem::exists(path)) {
                    hsm_library_path = path;
                    break;
                }
            }
        }
        
        // Get PIN from environment or use default
        const char* env_pin = std::getenv("THEMIS_TEST_HSM_PIN");
        hsm_pin = env_pin ? env_pin : "1234";
    }
    
    bool isHSMAvailable() const {
        return !hsm_library_path.empty() && 
               std::filesystem::exists(hsm_library_path);
    }
    
    HSMConfig createTestConfig() {
        HSMConfig config;
        config.library_path = hsm_library_path;
        config.slot_id = hsm_slot;
        config.pin = hsm_pin;
        config.key_label = "themis-signing-key";
        config.signature_algorithm = "RSA-SHA256";
        config.verbose = true;
        return config;
    }
};

TEST_F(HSMProviderTest, ConstructorDoesNotThrow) {
    HSMConfig config = createTestConfig();
    EXPECT_NO_THROW({
        HSMProvider hsm(config);
    });
}

TEST_F(HSMProviderTest, InitializeWithoutLibrary) {
    HSMConfig config;
    config.library_path = "/nonexistent/library.so";
    config.slot_id = 0;
    config.pin = "1234";
    
    HSMProvider hsm(config);
    EXPECT_FALSE(hsm.initialize());
    EXPECT_FALSE(hsm.isReady());
    EXPECT_FALSE(hsm.getLastError().empty());
}

TEST_F(HSMProviderTest, InitializeWithSoftHSM) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available. Install with: sudo apt-get install softhsm2";
    }
    
    HSMConfig config = createTestConfig();
    HSMProvider hsm(config);
    
    bool initialized = hsm.initialize();
    if (!initialized) {
        GTEST_SKIP() << "HSM initialization failed: " << hsm.getLastError() 
                     << "\nRun: softhsm2-util --init-token --slot 0 --label \"themis-test\" --pin 1234 --so-pin 5678";
    }
    
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(hsm.isReady());
    
    std::string info = hsm.getTokenInfo();
    EXPECT_FALSE(info.empty());
    EXPECT_NE(info.find("HSM Token Info"), std::string::npos);
    
    hsm.finalize();
    EXPECT_FALSE(hsm.isReady());
}

TEST_F(HSMProviderTest, SignAndVerifyStubMode) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    HSMConfig config = createTestConfig();
    HSMProvider hsm(config);
    
    if (!hsm.initialize()) {
        GTEST_SKIP() << "HSM initialization failed: " << hsm.getLastError();
    }
    
    // Test data
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o', ' ', 'H', 'S', 'M'};
    
    // Sign
    auto result = hsm.sign(data);
    
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.signature_b64.empty());
    EXPECT_EQ(result.algorithm, "RSA-SHA256");
    EXPECT_FALSE(result.key_id.empty());
    EXPECT_GT(result.timestamp_ms, 0);
    
    // Verify
    bool verified = hsm.verify(data, result.signature_b64);
    EXPECT_TRUE(verified);
    
    // Verify with modified data should fail
    std::vector<uint8_t> modified_data = data;
    modified_data[0] = 'X';
    bool verified_modified = hsm.verify(modified_data, result.signature_b64);
    EXPECT_FALSE(verified_modified);
}

TEST_F(HSMProviderTest, SignHashDirectly) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    HSMConfig config = createTestConfig();
    HSMProvider hsm(config);
    
    if (!hsm.initialize()) {
        GTEST_SKIP() << "HSM initialization failed";
    }
    
    // Pre-computed SHA-256 hash (32 bytes)
    std::vector<uint8_t> hash(32, 0xAB);
    
    auto result = hsm.signHash(hash);
    
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.signature_b64.empty());
    EXPECT_GT(result.timestamp_ms, 0);
}

TEST_F(HSMProviderTest, ListKeysStub) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    HSMConfig config = createTestConfig();
    HSMProvider hsm(config);
    
    if (!hsm.initialize()) {
        GTEST_SKIP() << "HSM initialization failed";
    }
    
    // Stub implementation returns empty list
    auto keys = hsm.listKeys();
    EXPECT_TRUE(keys.empty());  // Stub implementation
}

TEST_F(HSMProviderTest, GenerateKeyPairNotImplemented) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    HSMConfig config = createTestConfig();
    HSMProvider hsm(config);
    
    if (!hsm.initialize()) {
        GTEST_SKIP() << "HSM initialization failed";
    }
    
    // Stub implementation returns false
    bool generated = hsm.generateKeyPair("test-key", 2048, false);
    EXPECT_FALSE(generated);  // Stub implementation
}

TEST_F(HSMProviderTest, HSMPKIClientIntegration) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    HSMConfig config = createTestConfig();
    HSMPKIClient client(config);
    
    if (!client.isReady()) {
        GTEST_SKIP() << "HSM initialization failed";
    }
    
    // Test sign/verify workflow
    std::vector<uint8_t> data = {'T', 'e', 's', 't', ' ', 'D', 'a', 't', 'a'};
    
    auto sig_result = client.sign(data);
    EXPECT_TRUE(sig_result.success);
    EXPECT_FALSE(sig_result.signature_b64.empty());
    
    bool verified = client.verify(data, sig_result.signature_b64);
    EXPECT_TRUE(verified);
}

TEST_F(HSMProviderTest, MultipleSignOperations) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    HSMConfig config = createTestConfig();
    HSMProvider hsm(config);
    
    if (!hsm.initialize()) {
        GTEST_SKIP() << "HSM initialization failed";
    }
    
    // Perform multiple sign operations
    for (int i = 0; i < 10; ++i) {
        std::vector<uint8_t> data(100, static_cast<uint8_t>(i));
        auto result = hsm.sign(data);
        
        EXPECT_TRUE(result.success) << "Sign operation " << i << " failed";
        EXPECT_FALSE(result.signature_b64.empty());
    }
}

TEST_F(HSMProviderTest, DifferentAlgorithms) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    std::vector<std::string> algorithms = {"RSA-SHA256", "RSA-SHA384", "RSA-SHA512"};
    
    for (const auto& algo : algorithms) {
        HSMConfig config = createTestConfig();
        config.signature_algorithm = algo;
        
        HSMProvider hsm(config);
        if (!hsm.initialize()) {
            continue;
        }
        
        std::vector<uint8_t> data = {'T', 'e', 's', 't'};
        auto result = hsm.sign(data);
        
        EXPECT_TRUE(result.success) << "Algorithm " << algo << " failed";
        EXPECT_EQ(result.algorithm, algo);
    }
}

// Performance benchmark test (disabled by default)
TEST_F(HSMProviderTest, DISABLED_SignPerformanceBenchmark) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    HSMConfig config = createTestConfig();
    HSMProvider hsm(config);
    
    if (!hsm.initialize()) {
        GTEST_SKIP() << "HSM initialization failed";
    }
    
    const int num_operations = 100;
    std::vector<uint8_t> data(1024, 0xAA);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        auto result = hsm.sign(data);
        EXPECT_TRUE(result.success);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double ops_per_sec = (num_operations * 1000.0) / duration.count();
    
    std::cout << "HSM Sign Performance:\n"
              << "  Operations: " << num_operations << "\n"
              << "  Duration: " << duration.count() << " ms\n"
              << "  Operations/sec: " << ops_per_sec << "\n";
}

// Documentation test - shows example usage
TEST_F(HSMProviderTest, UsageExample) {
    if (!isHSMAvailable()) {
        GTEST_SKIP() << "SoftHSM2 not available";
    }
    
    // Example: How to use HSMProvider
    
    // 1. Configure HSM
    HSMConfig config;
    config.library_path = hsm_library_path;
    config.slot_id = 0;
    config.pin = hsm_pin;
    config.key_label = "themis-signing-key";
    config.signature_algorithm = "RSA-SHA256";
    
    // 2. Create provider
    auto hsm = std::make_unique<HSMProvider>(config);
    
    // 3. Initialize
    if (!hsm->initialize()) {
        GTEST_SKIP() << "Initialization failed: " << hsm->getLastError();
    }
    
    // 4. Sign data
    std::vector<uint8_t> data_to_sign = {'M', 'y', ' ', 'D', 'a', 't', 'a'};
    auto signature = hsm->sign(data_to_sign);
    
    ASSERT_TRUE(signature.success);
    
    // 5. Verify signature
    bool is_valid = hsm->verify(data_to_sign, signature.signature_b64);
    EXPECT_TRUE(is_valid);
    
    // 6. Cleanup (automatic via destructor)
    hsm->finalize();
}
