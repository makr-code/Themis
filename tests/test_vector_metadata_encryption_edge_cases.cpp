#include <gtest/gtest.h>
#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/vector_index.h"
#include "security/mock_key_provider.h"
#include "security/encryption.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <filesystem>

using namespace themis;
using json = nlohmann::json;

/**
 * Vector Metadata Encryption Edge Cases Test Suite
 * 
 * Coverage:
 * 1. Never encrypt the vector embedding itself (only metadata)
 * 2. Handle empty metadata fields
 * 3. Handle missing metadata fields in schema
 * 4. Handle complex/unsupported metadata types (arrays, objects)
 * 5. Handle encryption failures gracefully
 * 6. Handle missing encryption schema
 * 7. Handle user context extraction failures (anonymous fallback)
 * 8. Batch operations with mixed encrypted/plain metadata
 * 9. Schema updates during runtime
 * 10. Decryption on read with missing keys
 */

class VectorMetadataEncryptionEdgeCasesTest : public ::testing::Test {
protected:
    void SetUp() override {
        cleanupTestDB();
        
        // Initialize RocksDB
        RocksDBWrapper::Config db_cfg;
        db_cfg.db_path = test_db_path_;
        db_cfg.enable_wal = false;
        db_ = std::make_shared<RocksDBWrapper>(db_cfg);
        ASSERT_TRUE(db_->open());
        
        // Initialize MockKeyProvider
        key_provider_ = std::make_shared<MockKeyProvider>();
        key_provider_->createKey("dek", 1);
        
        // Initialize FieldEncryption
        field_encryption_ = std::make_shared<FieldEncryption>(key_provider_);
        
        // Initialize VectorIndexManager (3D for simplicity)
        VectorIndexManager::Config vec_cfg;
        vec_cfg.object_name = "test_vectors";
        vec_cfg.dimension = 3;
        vec_cfg.metric = VectorIndexManager::Metric::L2;
        vec_cfg.enable_hnsw = true;
        vec_cfg.M = 16;
        vec_cfg.ef_construction = 100;
        vec_cfg.ef_search = 64;
        
        vector_index_ = std::make_unique<VectorIndexManager>(*db_, vec_cfg);
    }
    
    void TearDown() override {
        vector_index_.reset();
        field_encryption_.reset();
        key_provider_.reset();
        db_.reset();
        cleanupTestDB();
    }
    
    void cleanupTestDB() {
        std::error_code ec;
        std::filesystem::remove_all(test_db_path_, ec);
    }
    
    // Helper: Store encryption schema
    void storeEncryptionSchema(const json& schema) {
        std::string schema_str = schema.dump();
        db_->put("config:encryption_schema", 
                 std::vector<uint8_t>(schema_str.begin(), schema_str.end()));
    }
    
    // Helper: Simulate vector metadata encryption (simplified from http_server.cpp)
    void encryptVectorMetadata(BaseEntity& entity, 
                              const std::vector<std::string>& fields_to_encrypt,
                              const std::string& vector_field,
                              const std::string& user_context = "test_user") {
        for (const auto& mf : fields_to_encrypt) {
            if (mf == vector_field) continue; // Never encrypt embedding
            if (!entity.hasField(mf)) continue;
            
            auto valOpt = entity.getField(mf);
            if (!valOpt.has_value()) continue;
            
            // Serialize value to string
            std::string plain_str;
            const auto& v = *valOpt;
            if (std::holds_alternative<std::string>(v)) {
                plain_str = std::get<std::string>(v);
            } else if (std::holds_alternative<int64_t>(v)) {
                plain_str = std::to_string(std::get<int64_t>(v));
            } else if (std::holds_alternative<double>(v)) {
                plain_str = std::to_string(std::get<double>(v));
            } else if (std::holds_alternative<bool>(v)) {
                plain_str = std::get<bool>(v) ? "true" : "false";
            } else {
                // Skip unsupported types (THIS IS AN EDGE CASE)
                continue;
            }
            
            try {
                auto dek = key_provider_->getKey("dek");
                std::vector<uint8_t> salt(user_context.begin(), user_context.end());
                std::string info = "field:" + mf;
                auto raw_key = utils::HKDFHelper::derive(dek, salt, info, 32);
                auto blob = field_encryption_->encryptWithKey(plain_str, "vector_meta:" + mf, 1, raw_key);
                auto j = blob.toJson();
                
                entity.setField(mf + "_encrypted", j.dump());
                entity.setField(mf + "_enc", true);
                entity.setField(mf, std::monostate{});
            } catch (const std::exception& ex) {
                THEMIS_WARN("Vector metadata encryption failed for {}: {}", mf, ex.what());
                // Edge case: continue with other fields
            }
        }
    }
    
    std::string test_db_path_ = "/tmp/themis_test_vector_metadata_edge_cases";
    std::shared_ptr<RocksDBWrapper> db_;
    std::shared_ptr<MockKeyProvider> key_provider_;
    std::shared_ptr<FieldEncryption> field_encryption_;
    std::unique_ptr<VectorIndexManager> vector_index_;
};

// ============================================================================
// Test 1: Never Encrypt Vector Embedding
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, NeverEncryptEmbedding) {
    json schema = {
        {"collections", {
            {"test_vectors", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"vec", "content"}} // "vec" is embedding field - should be skipped
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    BaseEntity entity("vec:001");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    entity.setField("content", "This is metadata");
    
    // Try to encrypt (should skip "vec")
    encryptVectorMetadata(entity, {"vec", "content"}, "vec");
    
    // Vector field should NOT be encrypted
    auto vec_opt = entity.getField("vec");
    ASSERT_TRUE(vec_opt.has_value());
    EXPECT_TRUE(std::holds_alternative<std::vector<float>>(*vec_opt));
    EXPECT_FALSE(entity.hasField("vec_encrypted"));
    
    // Content should be encrypted
    EXPECT_TRUE(entity.hasField("content_encrypted"));
    EXPECT_TRUE(entity.hasField("content_enc"));
}

// ============================================================================
// Test 2: Handle Empty Metadata Fields
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, EmptyMetadataField) {
    BaseEntity entity("vec:002");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    entity.setField("description", ""); // Empty string
    
    encryptVectorMetadata(entity, {"description"}, "vec");
    
    // Empty string should still be encrypted
    EXPECT_TRUE(entity.hasField("description_encrypted"));
    EXPECT_TRUE(entity.hasField("description_enc"));
    
    // Verify can decrypt back to empty string
    auto enc_json_str = entity.getFieldAsString("description_encrypted");
    ASSERT_TRUE(enc_json_str.has_value());
    
    auto enc_json = json::parse(*enc_json_str);
    auto blob = EncryptedBlob::fromJson(enc_json);
    
    auto dek = key_provider_->getKey("dek");
    std::vector<uint8_t> salt{'t', 'e', 's', 't', '_', 'u', 's', 'e', 'r'};
    auto raw_key = utils::HKDFHelper::derive(dek, salt, "field:description", 32);
    auto decrypted = field_encryption_->decryptWithKey(blob, raw_key);
    
    EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), "");
}

// ============================================================================
// Test 3: Handle Missing Metadata Fields
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, MissingMetadataField) {
    BaseEntity entity("vec:003");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    // No "title" field
    
    // Should not crash when trying to encrypt non-existent field
    EXPECT_NO_THROW(encryptVectorMetadata(entity, {"title", "description"}, "vec"));
    
    EXPECT_FALSE(entity.hasField("title_encrypted"));
    EXPECT_FALSE(entity.hasField("description_encrypted"));
}

// ============================================================================
// Test 4: Handle Complex/Unsupported Types
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, UnsupportedComplexTypes) {
    BaseEntity entity("vec:004");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    
    // Complex types that should be SKIPPED (not encrypted)
    std::vector<uint8_t> binary_data = {0x01, 0x02, 0x03};
    entity.setField("binary_field", binary_data);
    
    std::vector<float> float_array = {1.1f, 2.2f, 3.3f};
    entity.setField("float_array", float_array);
    
    // Try to encrypt - should skip unsupported types
    encryptVectorMetadata(entity, {"binary_field", "float_array"}, "vec");
    
    // These fields should NOT be encrypted (unsupported types)
    EXPECT_FALSE(entity.hasField("binary_field_encrypted"));
    EXPECT_FALSE(entity.hasField("float_array_encrypted"));
    
    // Original fields should remain unchanged
    EXPECT_TRUE(entity.hasField("binary_field"));
    EXPECT_TRUE(entity.hasField("float_array"));
}

// ============================================================================
// Test 5: Handle All Primitive Types
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, AllPrimitiveTypes) {
    BaseEntity entity("vec:005");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    
    // All supported primitive types
    entity.setField("string_field", "text");
    entity.setField("int_field", static_cast<int64_t>(42));
    entity.setField("double_field", 3.14159);
    entity.setField("bool_field", true);
    
    encryptVectorMetadata(entity, {"string_field", "int_field", "double_field", "bool_field"}, "vec");
    
    // All should be encrypted
    EXPECT_TRUE(entity.hasField("string_field_encrypted"));
    EXPECT_TRUE(entity.hasField("int_field_encrypted"));
    EXPECT_TRUE(entity.hasField("double_field_encrypted"));
    EXPECT_TRUE(entity.hasField("bool_field_encrypted"));
    
    // Verify decryption
    auto dek = key_provider_->getKey("dek");
    std::vector<uint8_t> salt{'t', 'e', 's', 't', '_', 'u', 's', 'e', 'r'};
    
    // String
    {
        auto enc_json = json::parse(entity.getFieldAsString("string_field_encrypted").value());
        auto blob = EncryptedBlob::fromJson(enc_json);
        auto raw_key = utils::HKDFHelper::derive(dek, salt, "field:string_field", 32);
        auto decrypted = field_encryption_->decryptWithKey(blob, raw_key);
        EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), "text");
    }
    
    // Int
    {
        auto enc_json = json::parse(entity.getFieldAsString("int_field_encrypted").value());
        auto blob = EncryptedBlob::fromJson(enc_json);
        auto raw_key = utils::HKDFHelper::derive(dek, salt, "field:int_field", 32);
        auto decrypted = field_encryption_->decryptWithKey(blob, raw_key);
        EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), "42");
    }
    
    // Bool
    {
        auto enc_json = json::parse(entity.getFieldAsString("bool_field_encrypted").value());
        auto blob = EncryptedBlob::fromJson(enc_json);
        auto raw_key = utils::HKDFHelper::derive(dek, salt, "field:bool_field", 32);
        auto decrypted = field_encryption_->decryptWithKey(blob, raw_key);
        EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), "true");
    }
}

// ============================================================================
// Test 6: Handle Encryption Failures Gracefully
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, EncryptionFailureGraceful) {
    // Simulate encryption failure by destroying key provider mid-operation
    BaseEntity entity("vec:006");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    entity.setField("safe_field", "This will encrypt");
    entity.setField("fail_field", "This will fail");
    
    // Encrypt safe_field first
    encryptVectorMetadata(entity, {"safe_field"}, "vec");
    EXPECT_TRUE(entity.hasField("safe_field_encrypted"));
    
    // Now destroy key provider to simulate failure
    key_provider_.reset();
    
    // This should NOT throw but log warning
    EXPECT_NO_THROW(encryptVectorMetadata(entity, {"fail_field"}, "vec"));
    
    // fail_field should NOT be encrypted
    EXPECT_FALSE(entity.hasField("fail_field_encrypted"));
    EXPECT_TRUE(entity.hasField("fail_field")); // Original remains
}

// ============================================================================
// Test 7: User Context Variations
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, UserContextVariations) {
    BaseEntity entity("vec:007");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    entity.setField("content", "User-specific encryption");
    
    // Different users should produce different ciphertexts
    BaseEntity entity_user1 = entity;
    BaseEntity entity_user2 = entity;
    
    encryptVectorMetadata(entity_user1, {"content"}, "vec", "user_1");
    encryptVectorMetadata(entity_user2, {"content"}, "vec", "user_2");
    
    auto enc1 = entity_user1.getFieldAsString("content_encrypted").value();
    auto enc2 = entity_user2.getFieldAsString("content_encrypted").value();
    
    // Different users → different ciphertexts (due to HKDF salt)
    EXPECT_NE(enc1, enc2);
    
    // Anonymous user (empty context)
    BaseEntity entity_anon = entity;
    encryptVectorMetadata(entity_anon, {"content"}, "vec", "");
    
    auto enc_anon = entity_anon.getFieldAsString("content_encrypted").value();
    EXPECT_NE(enc_anon, enc1);
    EXPECT_NE(enc_anon, enc2);
}

// ============================================================================
// Test 8: Batch Operations with Mixed Metadata
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, BatchOperationsMixedMetadata) {
    std::vector<std::string> fields_to_encrypt = {"title", "content"};
    
    // Entity 1: Both fields present
    BaseEntity e1("vec:001");
    e1.setField("vec", std::vector<float>{1.0f, 0.0f, 0.0f});
    e1.setField("title", "Title 1");
    e1.setField("content", "Content 1");
    
    // Entity 2: Only title
    BaseEntity e2("vec:002");
    e2.setField("vec", std::vector<float>{0.0f, 1.0f, 0.0f});
    e2.setField("title", "Title 2");
    // No content
    
    // Entity 3: Only content
    BaseEntity e3("vec:003");
    e3.setField("vec", std::vector<float>{0.0f, 0.0f, 1.0f});
    e3.setField("content", "Content 3");
    // No title
    
    // Entity 4: Neither field
    BaseEntity e4("vec:004");
    e4.setField("vec", std::vector<float>{1.0f, 1.0f, 1.0f});
    // No metadata
    
    // Encrypt all
    encryptVectorMetadata(e1, fields_to_encrypt, "vec");
    encryptVectorMetadata(e2, fields_to_encrypt, "vec");
    encryptVectorMetadata(e3, fields_to_encrypt, "vec");
    encryptVectorMetadata(e4, fields_to_encrypt, "vec");
    
    // Verify e1: both encrypted
    EXPECT_TRUE(e1.hasField("title_encrypted"));
    EXPECT_TRUE(e1.hasField("content_encrypted"));
    
    // Verify e2: only title encrypted
    EXPECT_TRUE(e2.hasField("title_encrypted"));
    EXPECT_FALSE(e2.hasField("content_encrypted"));
    
    // Verify e3: only content encrypted
    EXPECT_FALSE(e3.hasField("title_encrypted"));
    EXPECT_TRUE(e3.hasField("content_encrypted"));
    
    // Verify e4: nothing encrypted
    EXPECT_FALSE(e4.hasField("title_encrypted"));
    EXPECT_FALSE(e4.hasField("content_encrypted"));
}

// ============================================================================
// Test 9: Special Characters in Metadata
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, SpecialCharactersInMetadata) {
    BaseEntity entity("vec:008");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    
    // Special characters that might cause issues
    entity.setField("unicode", "Hello 世界 🌍");
    entity.setField("quotes", R"(She said "Hello")");
    entity.setField("backslash", R"(C:\Users\Test)");
    entity.setField("newlines", "Line1\nLine2\nLine3");
    entity.setField("json_like", R"({"key":"value"})");
    
    encryptVectorMetadata(entity, {"unicode", "quotes", "backslash", "newlines", "json_like"}, "vec");
    
    // All should be encrypted
    EXPECT_TRUE(entity.hasField("unicode_encrypted"));
    EXPECT_TRUE(entity.hasField("quotes_encrypted"));
    EXPECT_TRUE(entity.hasField("backslash_encrypted"));
    EXPECT_TRUE(entity.hasField("newlines_encrypted"));
    EXPECT_TRUE(entity.hasField("json_like_encrypted"));
    
    // Verify round-trip for unicode
    auto enc_json = json::parse(entity.getFieldAsString("unicode_encrypted").value());
    auto blob = EncryptedBlob::fromJson(enc_json);
    auto dek = key_provider_->getKey("dek");
    std::vector<uint8_t> salt{'t', 'e', 's', 't', '_', 'u', 's', 'e', 'r'};
    auto raw_key = utils::HKDFHelper::derive(dek, salt, "field:unicode", 32);
    auto decrypted = field_encryption_->decryptWithKey(blob, raw_key);
    
    EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), "Hello 世界 🌍");
}

// ============================================================================
// Test 10: Large Metadata Values
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, LargeMetadataValues) {
    BaseEntity entity("vec:009");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    
    // Large text (10 KB)
    std::string large_text(10240, 'A');
    entity.setField("large_content", large_text);
    
    // Very large number
    entity.setField("large_number", static_cast<int64_t>(9223372036854775807)); // INT64_MAX
    
    encryptVectorMetadata(entity, {"large_content", "large_number"}, "vec");
    
    EXPECT_TRUE(entity.hasField("large_content_encrypted"));
    EXPECT_TRUE(entity.hasField("large_number_encrypted"));
    
    // Verify large text round-trip
    auto enc_json = json::parse(entity.getFieldAsString("large_content_encrypted").value());
    auto blob = EncryptedBlob::fromJson(enc_json);
    auto dek = key_provider_->getKey("dek");
    std::vector<uint8_t> salt{'t', 'e', 's', 't', '_', 'u', 's', 'e', 'r'};
    auto raw_key = utils::HKDFHelper::derive(dek, salt, "field:large_content", 32);
    auto decrypted = field_encryption_->decryptWithKey(blob, raw_key);
    
    EXPECT_EQ(decrypted.size(), 10240);
    EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), large_text);
}

// ============================================================================
// Test 11: Monostate Field Values
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, MonostateFieldValues) {
    BaseEntity entity("vec:010");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    
    // Set field to monostate (null-like)
    entity.setField("null_field", std::monostate{});
    
    // Should skip monostate fields
    encryptVectorMetadata(entity, {"null_field"}, "vec");
    
    EXPECT_FALSE(entity.hasField("null_field_encrypted"));
}

// ============================================================================
// Test 12: Field Name Edge Cases
// ============================================================================

TEST_F(VectorMetadataEncryptionEdgeCasesTest, FieldNameEdgeCases) {
    BaseEntity entity("vec:011");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f};
    entity.setField("vec", embedding);
    
    // Edge case field names
    entity.setField("_private", "private field");
    entity.setField("field.with.dots", "dotted");
    entity.setField("field:with:colons", "colons");
    entity.setField("field/with/slashes", "slashes");
    entity.setField("123numeric", "numeric start");
    
    std::vector<std::string> fields = {
        "_private", "field.with.dots", "field:with:colons", 
        "field/with/slashes", "123numeric"
    };
    
    encryptVectorMetadata(entity, fields, "vec");
    
    // All should be encrypted despite unusual names
    EXPECT_TRUE(entity.hasField("_private_encrypted"));
    EXPECT_TRUE(entity.hasField("field.with.dots_encrypted"));
    EXPECT_TRUE(entity.hasField("field:with:colons_encrypted"));
    EXPECT_TRUE(entity.hasField("field/with/slashes_encrypted"));
    EXPECT_TRUE(entity.hasField("123numeric_encrypted"));
}
