#include <gtest/gtest.h>
#include "security/vault_key_provider.h"
#include "security/mock_key_provider.h"
#include "security/encryption.h"
#include "document/encrypted_entities.h"
#include <chrono>
#include <thread>

using namespace themis;

/**
 * @file test_vault_key_provider.cpp
 * 
 * NOTE: These tests require a running HashiCorp Vault instance.
 * 
 * Setup Instructions:
 * 1. Start Vault in dev mode:
 *    docker run --rm --cap-add=IPC_LOCK -e 'VAULT_DEV_ROOT_TOKEN_ID=myroot' -p 8200:8200 vault
 * 
 * 2. Set environment variables:
 *    export VAULT_ADDR=http://localhost:8200
 *    export VAULT_TOKEN=myroot
 * 
 * 3. Enable KV v2 secrets engine:
 *    vault secrets enable -version=2 -path=themis kv
 * 
 * 4. Create test key:
 *    vault kv put themis/keys/test_key key=$(openssl rand -base64 32) algorithm="AES-256-GCM" version=1
 * 
 * If Vault is not available, tests will be skipped.
 */

class VaultKeyProviderTest : public ::testing::Test {
protected:
    std::shared_ptr<VaultKeyProvider> provider_;
    bool vault_available_ = false;
    
    void SetUp() override {
        // Check if Vault is available
        const char* vault_addr = std::getenv("VAULT_ADDR");
        const char* vault_token = std::getenv("VAULT_TOKEN");
        
        if (!vault_addr || !vault_token) {
            GTEST_SKIP() << "Vault not configured. Set VAULT_ADDR and VAULT_TOKEN environment variables.";
            return;
        }
        
        try {
            VaultKeyProvider::Config config;
            config.vault_addr = vault_addr;
            config.vault_token = vault_token;
            config.kv_mount_path = "themis";
            config.verify_ssl = false;  // Dev mode uses self-signed cert
            
            provider_ = std::make_shared<VaultKeyProvider>(config);
            vault_available_ = true;
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to connect to Vault: " << e.what();
        }
    }
};

// ============================================================================
// Basic Key Operations
// ============================================================================

TEST_F(VaultKeyProviderTest, GetKey_ValidKey_ReturnsKeyBytes) {
    if (!vault_available_) GTEST_SKIP();
    
    // This assumes "test_key" was created in Vault setup
    auto key = provider_->getKey("test_key");
    
    EXPECT_EQ(32, key.size());  // 256 bits = 32 bytes
}

TEST_F(VaultKeyProviderTest, GetKey_NonexistentKey_ThrowsException) {
    if (!vault_available_) GTEST_SKIP();
    
    EXPECT_THROW(
        provider_->getKey("nonexistent_key_12345"),
        KeyNotFoundException
    );
}

TEST_F(VaultKeyProviderTest, GetKey_Cached_ReturnsSameKey) {
    if (!vault_available_) GTEST_SKIP();
    
    auto key1 = provider_->getKey("test_key");
    auto key2 = provider_->getKey("test_key");
    
    EXPECT_EQ(key1, key2);
    
    // Check cache stats
    auto stats = provider_->getCacheStats();
    EXPECT_GE(stats.cache_hits, 1);
    EXPECT_GT(stats.hit_rate, 0.0);
}

TEST_F(VaultKeyProviderTest, ClearCache_ForcesRefetch) {
    if (!vault_available_) GTEST_SKIP();
    
    auto key1 = provider_->getKey("test_key");
    
    provider_->clearCache();
    
    auto key2 = provider_->getKey("test_key");
    
    // Keys should be identical (same version from Vault)
    EXPECT_EQ(key1, key2);
}

TEST_F(VaultKeyProviderTest, GetKeyMetadata_ValidKey_ReturnsMetadata) {
    if (!vault_available_) GTEST_SKIP();
    
    auto meta = provider_->getKeyMetadata("test_key");
    
    EXPECT_EQ("test_key", meta.key_id);
    EXPECT_GT(meta.version, 0);
    EXPECT_EQ("AES-256-GCM", meta.algorithm);
    EXPECT_EQ(KeyStatus::ACTIVE, meta.status);
}

TEST_F(VaultKeyProviderTest, RotateKey_CreatesNewVersion) {
    if (!vault_available_) GTEST_SKIP();
    
    // Get current version
    auto meta_before = provider_->getKeyMetadata("test_key");
    uint32_t old_version = meta_before.version;
    
    // Rotate
    uint32_t new_version = provider_->rotateKey("test_key");
    
    EXPECT_EQ(old_version + 1, new_version);
    
    // Verify new version is active
    auto meta_after = provider_->getKeyMetadata("test_key");
    EXPECT_EQ(new_version, meta_after.version);
    
    // Old version should still be accessible
    auto old_key = provider_->getKey("test_key", old_version);
    EXPECT_EQ(32, old_key.size());
}

TEST_F(VaultKeyProviderTest, ListKeys_ReturnsAllKeys) {
    if (!vault_available_) GTEST_SKIP();
    
    auto keys = provider_->listKeys();
    
    EXPECT_GT(keys.size(), 0);
    
    // Should include test_key
    bool found_test_key = false;
    for (const auto& meta : keys) {
        if (meta.key_id == "test_key") {
            found_test_key = true;
            break;
        }
    }
    EXPECT_TRUE(found_test_key);
}

// ============================================================================
// Cache Performance
// ============================================================================

TEST_F(VaultKeyProviderTest, CacheHitRate_ImprovesOverTime) {
    if (!vault_available_) GTEST_SKIP();
    
    provider_->clearCache();
    
    // First request (cache miss)
    provider_->getKey("test_key");
    auto stats1 = provider_->getCacheStats();
    EXPECT_EQ(0, stats1.cache_hits);
    
    // Subsequent requests (cache hits)
    for (int i = 0; i < 10; i++) {
        provider_->getKey("test_key");
    }
    
    auto stats2 = provider_->getCacheStats();
    EXPECT_EQ(10, stats2.cache_hits);
    EXPECT_NEAR(0.91, stats2.hit_rate, 0.01);  // 10/11 = 0.909
}

// ============================================================================
// Integration with EncryptedField
// ============================================================================

TEST_F(VaultKeyProviderTest, Integration_EncryptDecryptWithVault) {
    if (!vault_available_) GTEST_SKIP();
    
    auto encryption = std::make_shared<FieldEncryption>(provider_);
    EncryptedField<std::string>::setFieldEncryption(encryption);
    
    EncryptedField<std::string> field;
    field.encrypt("secret_value_123", "test_key");
    
    std::string decrypted = field.decrypt();
    EXPECT_EQ("secret_value_123", decrypted);
}

TEST_F(VaultKeyProviderTest, Integration_UserEntity) {
    if (!vault_available_) GTEST_SKIP();
    
    auto encryption = std::make_shared<FieldEncryption>(provider_);
    EncryptedField<std::string>::setFieldEncryption(encryption);
    
    // Create user with encrypted PII
    User user;
    user.id = "user-001";
    user.username = "alice";
    user.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    user.email.encrypt("alice@example.com", "test_key");
    user.phone.encrypt("+1-555-0123", "test_key");
    user.ssn.encrypt("123-45-6789", "test_key");
    user.address.encrypt("123 Main St, NYC, NY 10001", "test_key");
    
    // Serialize to JSON
    nlohmann::json j = user.toJson();
    
    // Verify encrypted fields are base64-encoded
    EXPECT_TRUE(j.contains("email"));
    EXPECT_TRUE(j["email"].is_string());
    EXPECT_GT(j["email"].get<std::string>().size(), 50);  // Encrypted blob is larger
    
    // Deserialize and decrypt
    User loaded = User::fromJson(j);
    EXPECT_EQ("alice@example.com", loaded.email.decrypt());
    EXPECT_EQ("+1-555-0123", loaded.phone.decrypt());
    EXPECT_EQ("123-45-6789", loaded.ssn.decrypt());
    EXPECT_EQ("123 Main St, NYC, NY 10001", loaded.address.decrypt());
}

TEST_F(VaultKeyProviderTest, Integration_CustomerEntity) {
    if (!vault_available_) GTEST_SKIP();
    
    auto encryption = std::make_shared<FieldEncryption>(provider_);
    EncryptedField<std::string>::setFieldEncryption(encryption);
    EncryptedField<int64_t>::setFieldEncryption(encryption);
    EncryptedField<double>::setFieldEncryption(encryption);
    
    Customer customer;
    customer.customer_id = "cust-001";
    customer.account_type = "premium";
    customer.risk_tier = "medium";
    
    customer.credit_score.encrypt(720, "test_key");
    customer.annual_income.encrypt(95000.50, "test_key");
    customer.medical_record_id.encrypt("MR-2024-12345", "test_key");
    
    // Serialize
    nlohmann::json j = customer.toJson();
    
    // Deserialize and verify
    Customer loaded = Customer::fromJson(j);
    EXPECT_EQ(720, loaded.credit_score.decrypt());
    EXPECT_DOUBLE_EQ(95000.50, loaded.annual_income.decrypt());
    EXPECT_EQ("MR-2024-12345", loaded.medical_record_id.decrypt());
}

TEST_F(VaultKeyProviderTest, Integration_KeyRotation) {
    if (!vault_available_) GTEST_SKIP();
    
    auto encryption = std::make_shared<FieldEncryption>(provider_);
    EncryptedField<std::string>::setFieldEncryption(encryption);
    
    // Encrypt with current key version
    EncryptedField<std::string> field;
    field.encrypt("original_value", "test_key");
    
    auto blob_v1 = field.getBlob();
    uint32_t version_1 = blob_v1.key_version;
    
    // Rotate key
    uint32_t new_version = provider_->rotateKey("test_key");
    provider_->clearCache();  // Force re-fetch
    
    // Old data still decryptable
    std::string decrypted_old = field.decrypt();
    EXPECT_EQ("original_value", decrypted_old);
    
    // New encryption uses new version
    EncryptedField<std::string> field2;
    field2.encrypt("new_value", "test_key");
    
    auto blob_v2 = field2.getBlob();
    EXPECT_EQ(new_version, blob_v2.key_version);
    EXPECT_GT(blob_v2.key_version, version_1);
}

// ============================================================================
// Fallback Tests (using MockKeyProvider when Vault unavailable)
// ============================================================================

class EncryptedEntitiesTest : public ::testing::Test {
protected:
    std::shared_ptr<MockKeyProvider> mock_provider_;
    
    void SetUp() override {
        mock_provider_ = std::make_shared<MockKeyProvider>();
        mock_provider_->createKey("user_pii", 1);
        mock_provider_->createKey("user_sensitive", 1);
        mock_provider_->createKey("customer_financial", 1);
        
        auto encryption = std::make_shared<FieldEncryption>(mock_provider_);
        EncryptedField<std::string>::setFieldEncryption(encryption);
        EncryptedField<int64_t>::setFieldEncryption(encryption);
        EncryptedField<double>::setFieldEncryption(encryption);
    }
};

TEST_F(EncryptedEntitiesTest, UserEntity_EncryptDecryptRoundtrip) {
    User user;
    user.id = "user-123";
    user.username = "bob";
    user.status = "active";
    
    user.email.encrypt("bob@example.com", "user_pii");
    user.phone.encrypt("+1-555-9876", "user_pii");
    user.ssn.encrypt("987-65-4321", "user_sensitive");
    user.address.encrypt("456 Oak Ave, LA, CA 90001", "user_pii");
    
    // Serialize
    nlohmann::json j = user.toJson();
    std::string json_str = j.dump();
    
    // Deserialize
    User loaded = User::fromJson(nlohmann::json::parse(json_str));
    
    EXPECT_EQ("user-123", loaded.id);
    EXPECT_EQ("bob", loaded.username);
    EXPECT_EQ("bob@example.com", loaded.email.decrypt());
    EXPECT_EQ("+1-555-9876", loaded.phone.decrypt());
    EXPECT_EQ("987-65-4321", loaded.ssn.decrypt());
    EXPECT_EQ("456 Oak Ave, LA, CA 90001", loaded.address.decrypt());
}

TEST_F(EncryptedEntitiesTest, CustomerEntity_NumericTypes) {
    Customer customer;
    customer.customer_id = "cust-456";
    customer.account_type = "business";
    
    customer.credit_score.encrypt(810, "customer_financial");
    customer.annual_income.encrypt(250000.75, "customer_financial");
    customer.medical_record_id.encrypt("MR-XYZ-789", "customer_financial");
    
    nlohmann::json j = customer.toJson();
    Customer loaded = Customer::fromJson(j);
    
    EXPECT_EQ(810, loaded.credit_score.decrypt());
    EXPECT_DOUBLE_EQ(250000.75, loaded.annual_income.decrypt());
    EXPECT_EQ("MR-XYZ-789", loaded.medical_record_id.decrypt());
}

TEST_F(EncryptedEntitiesTest, SecureDocument_ContentEncryption) {
    SecureDocument doc;
    doc.id = "doc-001";
    doc.title = "Confidential Report";
    
    doc.content_preview.encrypt("This is a preview of confidential content...", "user_pii");
    doc.author.encrypt("Dr. Jane Smith", "user_pii");
    doc.classification.encrypt("confidential", "user_pii");
    
    nlohmann::json j = doc.toJson();
    SecureDocument loaded = SecureDocument::fromJson(j);
    
    EXPECT_EQ("This is a preview of confidential content...", loaded.content_preview.decrypt());
    EXPECT_EQ("Dr. Jane Smith", loaded.author.decrypt());
    EXPECT_EQ("confidential", loaded.classification.decrypt());
}

TEST_F(EncryptedEntitiesTest, Performance_BulkUserCreation) {
    const int NUM_USERS = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<User> users;
    for (int i = 0; i < NUM_USERS; i++) {
        User user;
        user.id = "user-" + std::to_string(i);
        user.username = "user" + std::to_string(i);
        user.email.encrypt("user" + std::to_string(i) + "@example.com", "user_pii");
        user.phone.encrypt("+1-555-" + std::to_string(1000 + i), "user_pii");
        users.push_back(user);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    EXPECT_LT(duration, 5000);  // Should complete in <5 seconds
    std::cout << "Created " << NUM_USERS << " encrypted users in " << duration << "ms ("
              << (double)duration/NUM_USERS << "ms per user)" << std::endl;
}
