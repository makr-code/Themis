#include <gtest/gtest.h>
#include "security/mock_key_provider.h"
#include "security/encryption.h"
#include <memory>
#include <thread>
#include <vector>

using namespace themis;

// ===== MockKeyProvider Tests =====

class MockKeyProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        provider_ = std::make_shared<MockKeyProvider>();
    }
    
    std::shared_ptr<MockKeyProvider> provider_;
};

TEST_F(MockKeyProviderTest, CreateKey_GeneratesRandomKey) {
    provider_->createKey("test_key", 1);
    
    auto key = provider_->getKey("test_key", 1);
    EXPECT_EQ(key.size(), 32);  // 256 bits
}

TEST_F(MockKeyProviderTest, CreateKey_DuplicateThrowsException) {
    provider_->createKey("test_key", 1);
    
    EXPECT_THROW(
        provider_->createKey("test_key", 1),
        KeyOperationException
    );
}

TEST_F(MockKeyProviderTest, CreateKeyWithBytes_StoresExactBytes) {
    std::vector<uint8_t> expected_key(32, 0xAB);
    provider_->createKeyWithBytes("test_key", 1, expected_key);
    
    auto key = provider_->getKey("test_key", 1);
    EXPECT_EQ(key, expected_key);
}

TEST_F(MockKeyProviderTest, CreateKeyWithBytes_InvalidSize_ThrowsException) {
    std::vector<uint8_t> invalid_key(16);  // Only 128 bits
    
    EXPECT_THROW(
        provider_->createKeyWithBytes("test_key", 1, invalid_key),
        std::invalid_argument
    );
}

TEST_F(MockKeyProviderTest, GetKey_NonExistent_ThrowsException) {
    EXPECT_THROW(
        provider_->getKey("nonexistent"),
        KeyNotFoundException
    );
}

TEST_F(MockKeyProviderTest, GetKey_ReturnsLatestActive) {
    provider_->createKey("test_key", 1);
    provider_->createKey("test_key", 2);
    provider_->createKey("test_key", 3);
    
    // All are ACTIVE initially
    auto metadata = provider_->getKeyMetadata("test_key");
    EXPECT_EQ(metadata.version, 3);  // Latest
}

TEST_F(MockKeyProviderTest, GetKey_WithVersion_ReturnsSpecificVersion) {
    provider_->createKey("test_key", 1);
    provider_->createKey("test_key", 2);
    
    auto key_v1 = provider_->getKey("test_key", 1);
    auto key_v2 = provider_->getKey("test_key", 2);
    
    EXPECT_NE(key_v1, key_v2);  // Different keys
}

TEST_F(MockKeyProviderTest, RotateKey_CreatesNewVersion) {
    provider_->createKey("test_key", 1);
    
    uint32_t new_version = provider_->rotateKey("test_key");
    
    EXPECT_EQ(new_version, 2);
}

TEST_F(MockKeyProviderTest, RotateKey_DeprecatesOldVersion) {
    provider_->createKey("test_key", 1);
    provider_->rotateKey("test_key");
    
    auto metadata_v1 = provider_->getKeyMetadata("test_key", 1);
    auto metadata_v2 = provider_->getKeyMetadata("test_key", 2);
    
    EXPECT_EQ(metadata_v1.status, KeyStatus::DEPRECATED);
    EXPECT_EQ(metadata_v2.status, KeyStatus::ACTIVE);
}

TEST_F(MockKeyProviderTest, RotateKey_NonExistent_ThrowsException) {
    EXPECT_THROW(
        provider_->rotateKey("nonexistent"),
        KeyNotFoundException
    );
}

TEST_F(MockKeyProviderTest, ListKeys_ReturnsAllVersions) {
    provider_->createKey("key1", 1);
    provider_->createKey("key1", 2);
    provider_->createKey("key2", 1);
    
    auto keys = provider_->listKeys();
    
    EXPECT_EQ(keys.size(), 3);
}

TEST_F(MockKeyProviderTest, GetKeyMetadata_ReturnsCorrectInfo) {
    provider_->createKey("test_key", 1);
    
    auto metadata = provider_->getKeyMetadata("test_key", 1);
    
    EXPECT_EQ(metadata.key_id, "test_key");
    EXPECT_EQ(metadata.version, 1);
    EXPECT_EQ(metadata.algorithm, "AES-256-GCM");
    EXPECT_EQ(metadata.status, KeyStatus::ACTIVE);
    EXPECT_GT(metadata.created_at_ms, 0);
}

TEST_F(MockKeyProviderTest, DeleteKey_ActiveKey_ThrowsException) {
    provider_->createKey("test_key", 1);
    
    EXPECT_THROW(
        provider_->deleteKey("test_key", 1),
        KeyOperationException
    );
}

TEST_F(MockKeyProviderTest, DeleteKey_DeprecatedKey_Succeeds) {
    provider_->createKey("test_key", 1);
    provider_->rotateKey("test_key");  // v1 becomes DEPRECATED
    
    EXPECT_NO_THROW(provider_->deleteKey("test_key", 1));
    
    auto metadata = provider_->getKeyMetadata("test_key", 1);
    EXPECT_EQ(metadata.status, KeyStatus::DELETED);
}

TEST_F(MockKeyProviderTest, GetKey_DeletedKey_ThrowsException) {
    provider_->createKey("test_key", 1);
    provider_->rotateKey("test_key");
    provider_->deleteKey("test_key", 1);
    
    EXPECT_THROW(
        provider_->getKey("test_key", 1),
        KeyOperationException
    );
}

TEST_F(MockKeyProviderTest, Clear_RemovesAllKeys) {
    provider_->createKey("key1", 1);
    provider_->createKey("key2", 1);
    
    provider_->clear();
    
    auto keys = provider_->listKeys();
    EXPECT_EQ(keys.size(), 0);
}

TEST_F(MockKeyProviderTest, ThreadSafety_ConcurrentCreates) {
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([this, i]() {
            provider_->createKey("key_" + std::to_string(i), 1);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto keys = provider_->listKeys();
    EXPECT_EQ(keys.size(), 10);
}

// ===== KeyCache Tests =====

class KeyCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache_ = std::make_unique<KeyCache>(100, 1000);  // 100 keys, 1 second TTL
    }
    
    std::unique_ptr<KeyCache> cache_;
};

TEST_F(KeyCacheTest, Get_EmptyCache_ReturnsFalse) {
    std::vector<uint8_t> key;
    bool found = cache_->get("test_key", 1, key);
    
    EXPECT_FALSE(found);
}

TEST_F(KeyCacheTest, PutAndGet_ReturnsKey) {
    std::vector<uint8_t> expected_key(32, 0xAB);
    cache_->put("test_key", 1, expected_key);
    
    std::vector<uint8_t> retrieved_key;
    bool found = cache_->get("test_key", 1, retrieved_key);
    
    EXPECT_TRUE(found);
    EXPECT_EQ(retrieved_key, expected_key);
}

TEST_F(KeyCacheTest, Get_DifferentVersion_ReturnsFalse) {
    std::vector<uint8_t> key(32, 0xAB);
    cache_->put("test_key", 1, key);
    
    std::vector<uint8_t> retrieved;
    bool found = cache_->get("test_key", 2, retrieved);  // Different version
    
    EXPECT_FALSE(found);
}

TEST_F(KeyCacheTest, Evict_RemovesKey) {
    std::vector<uint8_t> key(32, 0xAB);
    cache_->put("test_key", 1, key);
    
    cache_->evict("test_key", 1);
    
    std::vector<uint8_t> retrieved;
    EXPECT_FALSE(cache_->get("test_key", 1, retrieved));
}

TEST_F(KeyCacheTest, Evict_AllVersions_RemovesAll) {
    std::vector<uint8_t> key(32, 0xAB);
    cache_->put("test_key", 1, key);
    cache_->put("test_key", 2, key);
    
    cache_->evict("test_key", 0);  // All versions
    
    std::vector<uint8_t> retrieved;
    EXPECT_FALSE(cache_->get("test_key", 1, retrieved));
    EXPECT_FALSE(cache_->get("test_key", 2, retrieved));
}

TEST_F(KeyCacheTest, Clear_RemovesAll) {
    std::vector<uint8_t> key(32, 0xAB);
    cache_->put("key1", 1, key);
    cache_->put("key2", 1, key);
    
    cache_->clear();
    
    EXPECT_EQ(cache_->size(), 0);
}

TEST_F(KeyCacheTest, HitRate_TracksAccurately) {
    std::vector<uint8_t> key(32, 0xAB);
    cache_->put("test_key", 1, key);
    
    std::vector<uint8_t> retrieved;
    cache_->get("test_key", 1, retrieved);  // Hit
    cache_->get("test_key", 2, retrieved);  // Miss
    cache_->get("test_key", 1, retrieved);  // Hit
    
    // 2 hits / 3 total = 0.666...
    EXPECT_NEAR(cache_->getHitRate(), 0.667, 0.01);
}

TEST_F(KeyCacheTest, Size_ReflectsEntries) {
    std::vector<uint8_t> key(32, 0xAB);
    
    EXPECT_EQ(cache_->size(), 0);
    
    cache_->put("key1", 1, key);
    EXPECT_EQ(cache_->size(), 1);
    
    cache_->put("key2", 1, key);
    EXPECT_EQ(cache_->size(), 2);
}

// ===== FieldEncryption Tests =====

class FieldEncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        provider_ = std::make_shared<MockKeyProvider>();
        provider_->createKey("test_key", 1);
        
        encryption_ = std::make_shared<FieldEncryption>(provider_);
    }
    
    std::shared_ptr<MockKeyProvider> provider_;
    std::shared_ptr<FieldEncryption> encryption_;
};

TEST_F(FieldEncryptionTest, Constructor_NullProvider_ThrowsException) {
    EXPECT_THROW(
        FieldEncryption(nullptr),
        std::invalid_argument
    );
}

TEST_F(FieldEncryptionTest, EncryptDecrypt_Roundtrip_String) {
    std::string plaintext = "Hello, World!";
    
    auto blob = encryption_->encrypt(plaintext, "test_key");
    auto decrypted = encryption_->decryptToString(blob);
    
    EXPECT_EQ(decrypted, plaintext);
}

TEST_F(FieldEncryptionTest, EncryptDecrypt_Roundtrip_Binary) {
    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    auto blob = encryption_->encrypt(plaintext, "test_key");
    auto decrypted = encryption_->decryptToBytes(blob);
    
    EXPECT_EQ(decrypted, plaintext);
}

TEST_F(FieldEncryptionTest, Encrypt_GeneratesRandomIV) {
    std::string plaintext = "test";
    
    auto blob1 = encryption_->encrypt(plaintext, "test_key");
    auto blob2 = encryption_->encrypt(plaintext, "test_key");
    
    // Same plaintext, different IVs
    EXPECT_NE(blob1.iv, blob2.iv);
}

TEST_F(FieldEncryptionTest, Encrypt_GeneratesAuthTag) {
    std::string plaintext = "test";
    
    auto blob = encryption_->encrypt(plaintext, "test_key");
    
    EXPECT_EQ(blob.tag.size(), 16);  // 128 bits
}

TEST_F(FieldEncryptionTest, Encrypt_SetsKeyMetadata) {
    std::string plaintext = "test";
    
    auto blob = encryption_->encrypt(plaintext, "test_key");
    
    EXPECT_EQ(blob.key_id, "test_key");
    EXPECT_EQ(blob.key_version, 1);
}

TEST_F(FieldEncryptionTest, Decrypt_TamperedCiphertext_ThrowsException) {
    std::string plaintext = "test";
    auto blob = encryption_->encrypt(plaintext, "test_key");
    
    // Tamper with ciphertext
    blob.ciphertext[0] ^= 0xFF;
    
    EXPECT_THROW(
        encryption_->decryptToString(blob),
        DecryptionException
    );
}

TEST_F(FieldEncryptionTest, Decrypt_TamperedTag_ThrowsException) {
    std::string plaintext = "test";
    auto blob = encryption_->encrypt(plaintext, "test_key");
    
    // Tamper with tag
    blob.tag[0] ^= 0xFF;
    
    EXPECT_THROW(
        encryption_->decryptToString(blob),
        DecryptionException
    );
}

TEST_F(FieldEncryptionTest, Decrypt_WrongKey_ThrowsException) {
    provider_->createKey("key1", 1);
    provider_->createKey("key2", 1);
    
    std::string plaintext = "test";
    auto blob = encryption_->encrypt(plaintext, "key1");
    
    // Try to decrypt with different key
    blob.key_id = "key2";
    
    EXPECT_THROW(
        encryption_->decryptToString(blob),
        DecryptionException
    );
}

TEST_F(FieldEncryptionTest, EncryptWithKey_UsesProvidedKey) {
    auto key = provider_->getKey("test_key", 1);
    std::string plaintext = "test";
    
    auto blob = encryption_->encryptWithKey(plaintext, "test_key", 1, key);
    auto decrypted = encryption_->decryptToString(blob);
    
    EXPECT_EQ(decrypted, plaintext);
}

TEST_F(FieldEncryptionTest, Decrypt_OldKeyVersion_AfterRotation) {
    std::string plaintext = "test";
    auto blob = encryption_->encrypt(plaintext, "test_key");  // v1
    
    provider_->rotateKey("test_key");  // Create v2
    
    // Should still decrypt with v1
    auto decrypted = encryption_->decryptToString(blob);
    EXPECT_EQ(decrypted, plaintext);
}

TEST_F(FieldEncryptionTest, Encrypt_EmptyString_Works) {
    std::string plaintext = "";
    
    auto blob = encryption_->encrypt(plaintext, "test_key");
    auto decrypted = encryption_->decryptToString(blob);
    
    EXPECT_EQ(decrypted, plaintext);
}

TEST_F(FieldEncryptionTest, Encrypt_LargeData_Works) {
    std::string plaintext(10000, 'A');  // 10KB
    
    auto blob = encryption_->encrypt(plaintext, "test_key");
    auto decrypted = encryption_->decryptToString(blob);
    
    EXPECT_EQ(decrypted, plaintext);
}

TEST_F(FieldEncryptionTest, EncryptedBlob_SerializeBase64_Roundtrip) {
    std::string plaintext = "test data";
    auto blob = encryption_->encrypt(plaintext, "test_key");
    
    std::string b64 = blob.toBase64();
    auto parsed_blob = EncryptedBlob::fromBase64(b64);
    
    EXPECT_EQ(parsed_blob.key_id, blob.key_id);
    EXPECT_EQ(parsed_blob.key_version, blob.key_version);
    EXPECT_EQ(parsed_blob.iv, blob.iv);
    EXPECT_EQ(parsed_blob.ciphertext, blob.ciphertext);
    EXPECT_EQ(parsed_blob.tag, blob.tag);
}

TEST_F(FieldEncryptionTest, EncryptedBlob_SerializeJson_Roundtrip) {
    std::string plaintext = "test data";
    auto blob = encryption_->encrypt(plaintext, "test_key");
    
    auto json = blob.toJson();
    auto parsed_blob = EncryptedBlob::fromJson(json);
    
    auto decrypted = encryption_->decryptToString(parsed_blob);
    EXPECT_EQ(decrypted, plaintext);
}

// ===== EncryptedField Tests =====

class EncryptedFieldTest : public ::testing::Test {
protected:
    void SetUp() override {
        provider_ = std::make_shared<MockKeyProvider>();
        provider_->createKey("test_key", 1);
        
        encryption_ = std::make_shared<FieldEncryption>(provider_);
        EncryptedField<std::string>::setFieldEncryption(encryption_);
        EncryptedField<int64_t>::setFieldEncryption(encryption_);
        EncryptedField<double>::setFieldEncryption(encryption_);
    }
    
    std::shared_ptr<MockKeyProvider> provider_;
    std::shared_ptr<FieldEncryption> encryption_;
};

TEST_F(EncryptedFieldTest, StringField_EncryptDecrypt) {
    EncryptedField<std::string> field("alice@example.com", "test_key");
    
    std::string decrypted = field.decrypt();
    EXPECT_EQ(decrypted, "alice@example.com");
}

TEST_F(EncryptedFieldTest, Int64Field_EncryptDecrypt) {
    EncryptedField<int64_t> field(123456789, "test_key");
    
    int64_t decrypted = field.decrypt();
    EXPECT_EQ(decrypted, 123456789);
}

TEST_F(EncryptedFieldTest, DoubleField_EncryptDecrypt) {
    EncryptedField<double> field(3.141592653589793, "test_key");
    
    double decrypted = field.decrypt();
    EXPECT_NEAR(decrypted, 3.141592653589793, 1e-15);
}

TEST_F(EncryptedFieldTest, HasValue_EmptyField_ReturnsFalse) {
    EncryptedField<std::string> field;
    EXPECT_FALSE(field.hasValue());
}

TEST_F(EncryptedFieldTest, HasValue_AfterEncrypt_ReturnsTrue) {
    EncryptedField<std::string> field("test", "test_key");
    EXPECT_TRUE(field.hasValue());
}

TEST_F(EncryptedFieldTest, ToBase64_FromBase64_Roundtrip) {
    EncryptedField<std::string> field("test data", "test_key");
    
    std::string b64 = field.toBase64();
    auto loaded = EncryptedField<std::string>::fromBase64(b64);
    
    EXPECT_EQ(loaded.decrypt(), "test data");
}

TEST_F(EncryptedFieldTest, ToJson_FromJson_Roundtrip) {
    EncryptedField<std::string> field("test data", "test_key");
    
    auto json = field.toJson();
    auto loaded = EncryptedField<std::string>::fromJson(json);
    
    EXPECT_EQ(loaded.decrypt(), "test data");
}

TEST_F(EncryptedFieldTest, Encrypt_UpdatesValue) {
    EncryptedField<std::string> field("original", "test_key");
    
    field.encrypt("updated", "test_key");
    
    EXPECT_EQ(field.decrypt(), "updated");
}

// ===== Key Rotation Scenario Tests =====

TEST_F(EncryptedFieldTest, KeyRotation_OldDataStillDecryptable) {
    // Encrypt with v1
    EncryptedField<std::string> field("data v1", "test_key");
    std::string b64_v1 = field.toBase64();
    
    // Rotate key
    provider_->rotateKey("test_key");
    
    // New data uses v2
    EncryptedField<std::string> new_field("data v2", "test_key");
    
    // Old data still decryptable
    auto old_field = EncryptedField<std::string>::fromBase64(b64_v1);
    EXPECT_EQ(old_field.decrypt(), "data v1");
    
    // New data also decryptable
    EXPECT_EQ(new_field.decrypt(), "data v2");
}

// ===== Performance Test =====

TEST_F(FieldEncryptionTest, Performance_1000EncryptDecrypt) {
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; i++) {
        std::string data = "test data " + std::to_string(i);
        auto blob = encryption_->encrypt(data, "test_key");
        encryption_->decryptToString(blob);
    }
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start
    ).count();
    
    // Should complete in <2000ms (2ms per operation target)
    EXPECT_LT(duration, 2000);
    
    std::cout << "1000 encrypt/decrypt operations: " << duration << "ms" << std::endl;
}
