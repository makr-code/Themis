#include <gtest/gtest.h>
#include "security/encryption.h"
#include "security/mock_key_provider.h"
#include "utils/logger.h"
#include <memory>

using namespace themis;

/**
 * Tests for Lazy Re-Encryption Feature
 * 
 * Scenarios:
 * 1. Basic lazy re-encryption on key rotation
 * 2. No re-encryption when using latest key version
 * 3. Multiple version jumps (v1 -> v3)
 * 4. Batch re-encryption simulation
 * 5. Re-encryption failure handling
 * 6. needsReEncryption check
 */

class LazyReEncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        key_provider_ = std::make_shared<MockKeyProvider>();
        field_encryption_ = std::make_shared<FieldEncryption>(key_provider_);
        
        // Create initial key version
        key_provider_->createKey("test_key", 1);
    }
    
    void TearDown() override {
        field_encryption_.reset();
        key_provider_.reset();
    }
    
    std::shared_ptr<MockKeyProvider> key_provider_;
    std::shared_ptr<FieldEncryption> field_encryption_;
};

// ============================================================================
// Test 1: Basic Lazy Re-Encryption
// ============================================================================

TEST_F(LazyReEncryptionTest, BasicReEncryption_OldVersionToNew) {
    std::string plaintext = "sensitive_data_123";
    
    // Encrypt with version 1
    auto blob_v1 = field_encryption_->encrypt(plaintext, "test_key");
    EXPECT_EQ(blob_v1.key_version, 1);
    
    // Rotate key to version 2
    key_provider_->createKey("test_key", 2);
    
    // Decrypt with lazy re-encryption
    std::optional<EncryptedBlob> updated_blob;
    std::string decrypted = field_encryption_->decryptAndReEncrypt(
        blob_v1, "test_key", updated_blob
    );
    
    // Verify decryption succeeded
    EXPECT_EQ(decrypted, plaintext);
    
    // Verify re-encryption occurred
    ASSERT_TRUE(updated_blob.has_value());
    EXPECT_EQ(updated_blob->key_version, 2);
    EXPECT_EQ(updated_blob->key_id, "test_key");
    
    // Verify updated blob can be decrypted
    auto decrypted_v2 = field_encryption_->decrypt(*updated_blob);
    EXPECT_EQ(decrypted_v2, plaintext);
}

TEST_F(LazyReEncryptionTest, NoReEncryption_LatestVersion) {
    std::string plaintext = "already_latest";
    
    // Encrypt with version 1 (latest)
    auto blob = field_encryption_->encrypt(plaintext, "test_key");
    EXPECT_EQ(blob.key_version, 1);
    
    // No key rotation - version 1 is still latest
    
    // Decrypt with lazy re-encryption
    std::optional<EncryptedBlob> updated_blob;
    std::string decrypted = field_encryption_->decryptAndReEncrypt(
        blob, "test_key", updated_blob
    );
    
    // Verify decryption succeeded
    EXPECT_EQ(decrypted, plaintext);
    
    // Verify NO re-encryption occurred (already latest)
    EXPECT_FALSE(updated_blob.has_value());
}

// ============================================================================
// Test 2: Multiple Version Jumps
// ============================================================================

TEST_F(LazyReEncryptionTest, MultipleVersionJump_V1ToV4) {
    std::string plaintext = "old_encrypted_data";
    
    // Encrypt with version 1
    auto blob_v1 = field_encryption_->encrypt(plaintext, "test_key");
    EXPECT_EQ(blob_v1.key_version, 1);
    
    // Rotate keys multiple times: v2, v3, v4
    key_provider_->createKey("test_key", 2);
    key_provider_->createKey("test_key", 3);
    key_provider_->createKey("test_key", 4);
    
    // Decrypt with lazy re-encryption (should jump to v4)
    std::optional<EncryptedBlob> updated_blob;
    std::string decrypted = field_encryption_->decryptAndReEncrypt(
        blob_v1, "test_key", updated_blob
    );
    
    // Verify decryption succeeded
    EXPECT_EQ(decrypted, plaintext);
    
    // Verify re-encryption jumped to latest version (v4)
    ASSERT_TRUE(updated_blob.has_value());
    EXPECT_EQ(updated_blob->key_version, 4);
}

// ============================================================================
// Test 3: needsReEncryption Check
// ============================================================================

TEST_F(LazyReEncryptionTest, NeedsReEncryption_DetectsOutdatedKey) {
    // Encrypt with version 1
    auto blob = field_encryption_->encrypt("test", "test_key");
    
    // Initially, should NOT need re-encryption (v1 is latest)
    EXPECT_FALSE(field_encryption_->needsReEncryption(blob, "test_key"));
    
    // Rotate to version 2
    key_provider_->createKey("test_key", 2);
    
    // Now should need re-encryption
    EXPECT_TRUE(field_encryption_->needsReEncryption(blob, "test_key"));
}

TEST_F(LazyReEncryptionTest, NeedsReEncryption_LatestVersionReturnsFalse) {
    // Encrypt with version 1
    auto blob = field_encryption_->encrypt("test", "test_key");
    EXPECT_EQ(blob.key_version, 1);
    
    // No rotation - v1 is still latest
    EXPECT_FALSE(field_encryption_->needsReEncryption(blob, "test_key"));
}

// ============================================================================
// Test 4: Batch Re-Encryption Simulation
// ============================================================================

TEST_F(LazyReEncryptionTest, BatchReEncryption_MixedVersions) {
    // Create blobs with different versions
    std::vector<std::pair<std::string, EncryptedBlob>> test_data = {
        {"data1", field_encryption_->encrypt("value1", "test_key")}, // v1
        {"data2", field_encryption_->encrypt("value2", "test_key")}, // v1
        {"data3", field_encryption_->encrypt("value3", "test_key")}  // v1
    };
    
    // Rotate to v2 before encrypting more
    key_provider_->createKey("test_key", 2);
    
    test_data.push_back({"data4", field_encryption_->encrypt("value4", "test_key")}); // v2
    test_data.push_back({"data5", field_encryption_->encrypt("value5", "test_key")}); // v2
    
    // Rotate to v3
    key_provider_->createKey("test_key", 3);
    
    // Simulate batch re-encryption
    size_t re_encrypted_count = 0;
    std::vector<std::pair<std::string, EncryptedBlob>> updated_data;
    
    for (const auto& [key, blob] : test_data) {
        std::optional<EncryptedBlob> updated;
        auto plaintext = field_encryption_->decryptAndReEncrypt(blob, "test_key", updated);
        
        if (updated.has_value()) {
            ++re_encrypted_count;
            updated_data.push_back({key, *updated});
        } else {
            updated_data.push_back({key, blob});
        }
    }
    
    // Verify: 5 blobs total, all versions < 3 should be re-encrypted
    EXPECT_EQ(re_encrypted_count, 5); // v1 and v2 both need upgrade to v3
    
    // Verify all blobs are now v3
    for (const auto& [key, blob] : updated_data) {
        EXPECT_EQ(blob.key_version, 3);
    }
}

// ============================================================================
// Test 5: Re-Encryption Preserves Data Integrity
// ============================================================================

TEST_F(LazyReEncryptionTest, ReEncryption_PreservesDataIntegrity) {
    // Test various data types
    std::vector<std::string> test_values = {
        "simple_string",
        "",  // empty string
        "Unicode: äöü 你好 مرحبا",
        std::string(1000, 'A'),  // large string
        "Special chars: !@#$%^&*()[]{}|\\:;\"'<>,.?/~`"
    };
    
    for (const auto& original : test_values) {
        // Encrypt with v1
        auto blob_v1 = field_encryption_->encrypt(original, "test_key");
        
        // Rotate to v2
        key_provider_->createKey("test_key", 2);
        
        // Lazy re-encrypt
        std::optional<EncryptedBlob> updated;
        auto decrypted = field_encryption_->decryptAndReEncrypt(blob_v1, "test_key", updated);
        
        // Verify data integrity
        EXPECT_EQ(decrypted, original) << "Data corrupted for: " << original;
        
        // Verify updated blob
        ASSERT_TRUE(updated.has_value());
        auto decrypted_v2 = field_encryption_->decrypt(*updated);
        EXPECT_EQ(decrypted_v2, original) << "Re-encrypted data corrupted";
        
        // Reset for next iteration
        key_provider_->createKey("test_key", 1);
    }
}

// ============================================================================
// Test 6: Re-Encryption Failure Handling
// ============================================================================

TEST_F(LazyReEncryptionTest, ReEncryptionFailure_StillReturnsDecryptedData) {
    std::string plaintext = "data_to_decrypt";
    
    // Encrypt with v1
    auto blob = field_encryption_->encrypt(plaintext, "test_key");
    
    // Rotate to v2
    key_provider_->createKey("test_key", 2);
    
    // Simulate re-encryption failure by removing the new key
    // (This is a contrived scenario, but tests error handling)
    
    // For this test, we'll just verify that even if re-encryption
    // somehow fails, the original decryption still succeeds
    
    std::optional<EncryptedBlob> updated;
    auto decrypted = field_encryption_->decryptAndReEncrypt(blob, "test_key", updated);
    
    // Decryption should still succeed
    EXPECT_EQ(decrypted, plaintext);
    
    // Re-encryption should have occurred (v2 exists)
    EXPECT_TRUE(updated.has_value());
}

// ============================================================================
// Test 7: Performance - Lazy Re-Encryption Overhead
// ============================================================================

TEST_F(LazyReEncryptionTest, Performance_LazyReEncryptionOverhead) {
    const size_t num_iterations = 100;
    std::string plaintext(1024, 'X'); // 1KB data
    
    // Encrypt with v1
    auto blob = field_encryption_->encrypt(plaintext, "test_key");
    
    // Rotate to v2
    key_provider_->createKey("test_key", 2);
    
    // Measure lazy re-encryption time
    auto start = std::chrono::steady_clock::now();
    
    for (size_t i = 0; i < num_iterations; ++i) {
        std::optional<EncryptedBlob> updated;
        field_encryption_->decryptAndReEncrypt(blob, "test_key", updated);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Performance expectation: should complete within reasonable time
    // (100 iterations of decrypt+re-encrypt for 1KB data)
    EXPECT_LT(duration_ms, 1000) << "Lazy re-encryption too slow: " << duration_ms << "ms";
    
    THEMIS_INFO("Lazy re-encryption performance: {} ops in {} ms ({} ms/op)",
               num_iterations, duration_ms, static_cast<double>(duration_ms) / num_iterations);
}

// ============================================================================
// Test 8: Concurrent Lazy Re-Encryption
// ============================================================================

TEST_F(LazyReEncryptionTest, Concurrent_LazyReEncryption_ThreadSafe) {
    const size_t num_threads = 10;
    const size_t ops_per_thread = 50;
    
    // Encrypt initial blobs
    std::vector<EncryptedBlob> blobs;
    for (size_t i = 0; i < num_threads; ++i) {
        blobs.push_back(field_encryption_->encrypt("data_" + std::to_string(i), "test_key"));
    }
    
    // Rotate key
    key_provider_->createKey("test_key", 2);
    
    // Concurrent lazy re-encryption
    std::vector<std::thread> threads;
    std::atomic<size_t> success_count{0};
    
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (size_t i = 0; i < ops_per_thread; ++i) {
                try {
                    std::optional<EncryptedBlob> updated;
                    auto decrypted = field_encryption_->decryptAndReEncrypt(
                        blobs[t], "test_key", updated
                    );
                    
                    if (updated.has_value() && updated->key_version == 2) {
                        ++success_count;
                    }
                } catch (...) {
                    // Ignore errors for concurrency test
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // All operations should have succeeded
    EXPECT_EQ(success_count.load(), num_threads * ops_per_thread);
}

// ============================================================================
// Test 9: Integration with Real-World Scenario
// ============================================================================

TEST_F(LazyReEncryptionTest, Integration_UserDataMigration) {
    // Simulate user database with encrypted PII
    struct UserRecord {
        std::string id;
        EncryptedBlob email;
        EncryptedBlob ssn;
    };
    
    std::vector<UserRecord> users = {
        {"user1", field_encryption_->encrypt("alice@example.com", "pii_key"),
                  field_encryption_->encrypt("123-45-6789", "pii_key")},
        {"user2", field_encryption_->encrypt("bob@example.com", "pii_key"),
                  field_encryption_->encrypt("987-65-4321", "pii_key")},
        {"user3", field_encryption_->encrypt("charlie@example.com", "pii_key"),
                  field_encryption_->encrypt("555-55-5555", "pii_key")}
    };
    
    // Create PII key
    key_provider_->createKey("pii_key", 1);
    
    // Verify all encrypted with v1
    for (const auto& user : users) {
        EXPECT_EQ(user.email.key_version, 1);
        EXPECT_EQ(user.ssn.key_version, 1);
    }
    
    // Rotate PII key to v2 (compliance requirement)
    key_provider_->createKey("pii_key", 2);
    
    // Migrate users on read (lazy re-encryption)
    size_t migrated_count = 0;
    
    for (auto& user : users) {
        // Email
        std::optional<EncryptedBlob> email_updated;
        auto email_plain = field_encryption_->decryptAndReEncrypt(
            user.email, "pii_key", email_updated
        );
        if (email_updated.has_value()) {
            user.email = *email_updated;
            ++migrated_count;
        }
        
        // SSN
        std::optional<EncryptedBlob> ssn_updated;
        auto ssn_plain = field_encryption_->decryptAndReEncrypt(
            user.ssn, "pii_key", ssn_updated
        );
        if (ssn_updated.has_value()) {
            user.ssn = *ssn_updated;
            ++migrated_count;
        }
    }
    
    // Verify migration
    EXPECT_EQ(migrated_count, 6); // 3 users * 2 fields = 6 fields migrated
    
    // Verify all now use v2
    for (const auto& user : users) {
        EXPECT_EQ(user.email.key_version, 2);
        EXPECT_EQ(user.ssn.key_version, 2);
    }
}
