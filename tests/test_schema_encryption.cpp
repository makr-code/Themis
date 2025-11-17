#include <gtest/gtest.h>
#include "security/mock_key_provider.h"
#include "security/encryption.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "utils/logger.h"
#include <memory>
#include <filesystem>
#include <nlohmann/json.hpp>

using namespace themis;
using json = nlohmann::json;

/**
 * Comprehensive Schema-Based Encryption End-to-End Tests
 * 
 * Coverage:
 * 1. Collection-level schema configuration and validation
 * 2. Field-level encryption/decryption with schema directives
 * 3. Automatic encryption on write, decryption on read
 * 4. Schema updates and migration scenarios
 * 5. Edge cases: missing fields, wrong types, encryption failures
 * 6. Performance: batch operations with schema encryption
 * 7. Security: schema isolation, key rotation with schema
 * 8. Integration: secondary indexes with encrypted fields
 */

class SchemaEncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        cleanupTestDB();
        
        // Initialize RocksDB
        RocksDBWrapper::Config db_cfg;
        db_cfg.db_path = test_db_path_;
        db_cfg.enable_wal = false; // Faster for tests
        db_ = std::make_shared<RocksDBWrapper>(db_cfg);
        ASSERT_TRUE(db_->open());
        
        // Initialize MockKeyProvider
        key_provider_ = std::make_shared<MockKeyProvider>();
        key_provider_->createKey("dek", 1);
        
        // Initialize FieldEncryption
        field_encryption_ = std::make_shared<FieldEncryption>(key_provider_);
        
        // Initialize SecondaryIndexManager
        sec_idx_ = std::make_unique<SecondaryIndexManager>(*db_);
    }
    
    void TearDown() override {
        sec_idx_.reset();
        field_encryption_.reset();
        key_provider_.reset();
        db_.reset();
        cleanupTestDB();
    }
    
    void cleanupTestDB() {
        std::error_code ec;
        std::filesystem::remove_all(test_db_path_, ec);
    }
    
    // Helper: Store encryption schema in DB
    void storeEncryptionSchema(const json& schema) {
        std::string schema_str = schema.dump();
        db_->put("config:encryption_schema", 
                 std::vector<uint8_t>(schema_str.begin(), schema_str.end()));
    }
    
    // Helper: Load encryption schema from DB
    std::optional<json> loadEncryptionSchema() {
        auto schema_bytes = db_->get("config:encryption_schema");
        if (!schema_bytes) return std::nullopt;
        
        std::string schema_str(schema_bytes->begin(), schema_bytes->end());
        return json::parse(schema_str);
    }
    
    // Helper: Get fields to encrypt for a collection
    std::vector<std::string> getEncryptedFields(const std::string& collection) {
        auto schema = loadEncryptionSchema();
        if (!schema) return {};
        
        if (!schema->contains("collections")) return {};
        auto& collections = (*schema)["collections"];
        if (!collections.contains(collection)) return {};
        
        auto& coll_config = collections[collection];
        if (!coll_config.contains("encryption")) return {};
        
        auto& enc_config = coll_config["encryption"];
        if (!enc_config.contains("enabled") || !enc_config["enabled"].get<bool>()) return {};
        
        if (!enc_config.contains("fields") || !enc_config["fields"].is_array()) return {};
        
        std::vector<std::string> fields;
        for (const auto& f : enc_config["fields"]) {
            if (f.is_string()) fields.push_back(f.get<std::string>());
        }
        return fields;
    }
    
    // Helper: Encrypt entity fields based on schema
    void encryptEntityBySchema(BaseEntity& entity, const std::string& collection) {
        auto fields = getEncryptedFields(collection);
        for (const auto& field : fields) {
            if (!entity.hasField(field)) continue;
            
            // Get field value as string (simplified for test)
            auto value_opt = entity.getFieldAsString(field);
            if (!value_opt) continue;
            
            // Encrypt
            auto blob = field_encryption_->encrypt(*value_opt, collection + "." + field);
            
            // Store encrypted version and mark field
            entity.setField(field + "_encrypted", blob.toJson().dump());
            entity.setField(field + "_enc", true);
            entity.setField(field, std::monostate{}); // Clear plaintext
        }
    }
    
    // Helper: Decrypt entity fields based on schema
    void decryptEntityBySchema(BaseEntity& entity, const std::string& collection) {
        auto fields = getEncryptedFields(collection);
        for (const auto& field : fields) {
            if (!entity.hasField(field + "_encrypted")) continue;
            
            auto enc_json_str = entity.getFieldAsString(field + "_encrypted");
            if (!enc_json_str) continue;
            
            auto enc_json = json::parse(*enc_json_str);
            auto blob = EncryptedBlob::fromJson(enc_json);
            
            auto decrypted = field_encryption_->decrypt(blob, collection + "." + field);
            entity.setField(field, std::string(decrypted.begin(), decrypted.end()));
            
            // Remove encryption markers
            entity.setField(field + "_encrypted", std::monostate{});
            entity.setField(field + "_enc", std::monostate{});
        }
    }
    
    std::string test_db_path_ = "/tmp/themis_test_schema_encryption";
    std::shared_ptr<RocksDBWrapper> db_;
    std::shared_ptr<MockKeyProvider> key_provider_;
    std::shared_ptr<FieldEncryption> field_encryption_;
    std::unique_ptr<SecondaryIndexManager> sec_idx_;
};

// ============================================================================
// Test 1: Basic Schema Configuration
// ============================================================================

TEST_F(SchemaEncryptionTest, StoreAndLoadSchema) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "ssn", "phone"}}
                }}
            }},
            {"orders", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"credit_card"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    auto loaded = loadEncryptionSchema();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, schema);
}

TEST_F(SchemaEncryptionTest, GetEncryptedFieldsForCollection) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "ssn"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    auto fields = getEncryptedFields("users");
    EXPECT_EQ(fields.size(), 2);
    EXPECT_TRUE(std::find(fields.begin(), fields.end(), "email") != fields.end());
    EXPECT_TRUE(std::find(fields.begin(), fields.end(), "ssn") != fields.end());
    
    // Non-existent collection
    auto empty_fields = getEncryptedFields("products");
    EXPECT_TRUE(empty_fields.empty());
}

TEST_F(SchemaEncryptionTest, SchemaDisabled) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", false},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    auto fields = getEncryptedFields("users");
    EXPECT_TRUE(fields.empty()); // Should return empty when disabled
}

// ============================================================================
// Test 2: Automatic Encryption/Decryption
// ============================================================================

TEST_F(SchemaEncryptionTest, AutoEncryptOnWrite) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "ssn"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // Create entity
    BaseEntity entity("user:alice");
    entity.setField("id", "alice");
    entity.setField("username", "alice_wonderland");
    entity.setField("email", "alice@example.com");
    entity.setField("ssn", "123-45-6789");
    entity.setField("age", 30);
    
    // Encrypt based on schema
    encryptEntityBySchema(entity, "users");
    
    // Verify encrypted fields exist and plaintext removed
    EXPECT_TRUE(entity.hasField("email_encrypted"));
    EXPECT_TRUE(entity.hasField("email_enc"));
    EXPECT_FALSE(entity.hasField("email") && 
                 std::holds_alternative<std::string>(*entity.getField("email")));
    
    EXPECT_TRUE(entity.hasField("ssn_encrypted"));
    EXPECT_TRUE(entity.hasField("ssn_enc"));
    
    // Non-encrypted fields remain unchanged
    EXPECT_EQ(entity.getFieldAsString("username").value(), "alice_wonderland");
    EXPECT_EQ(entity.getFieldAsInt("age").value(), 30);
}

TEST_F(SchemaEncryptionTest, AutoDecryptOnRead) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // Create and encrypt
    BaseEntity entity("user:bob");
    entity.setField("id", "bob");
    entity.setField("email", "bob@secret.com");
    
    encryptEntityBySchema(entity, "users");
    
    // Store
    db_->put("user:bob", entity.serialize());
    
    // Retrieve
    auto retrieved_opt = db_->get("user:bob");
    ASSERT_TRUE(retrieved_opt.has_value());
    
    auto retrieved = BaseEntity::deserialize("user:bob", *retrieved_opt);
    
    // Decrypt
    decryptEntityBySchema(retrieved, "users");
    
    // Verify decrypted value
    EXPECT_EQ(retrieved.getFieldAsString("email").value(), "bob@secret.com");
    EXPECT_FALSE(retrieved.hasField("email_encrypted"));
}

// ============================================================================
// Test 3: Multiple Fields and Collections
// ============================================================================

TEST_F(SchemaEncryptionTest, MultipleFieldsMultipleCollections) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "phone", "ssn"}}
                }}
            }},
            {"medical_records", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"diagnosis", "prescription", "notes"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // User entity
    BaseEntity user("user:charlie");
    user.setField("id", "charlie");
    user.setField("email", "charlie@example.com");
    user.setField("phone", "+1-555-1234");
    user.setField("ssn", "987-65-4321");
    user.setField("age", 45);
    
    encryptEntityBySchema(user, "users");
    
    EXPECT_TRUE(user.hasField("email_encrypted"));
    EXPECT_TRUE(user.hasField("phone_encrypted"));
    EXPECT_TRUE(user.hasField("ssn_encrypted"));
    EXPECT_EQ(user.getFieldAsInt("age").value(), 45); // Not encrypted
    
    // Medical record entity
    BaseEntity record("record:001");
    record.setField("id", "001");
    record.setField("patient_id", "charlie");
    record.setField("diagnosis", "Hypertension");
    record.setField("prescription", "Lisinopril 10mg");
    record.setField("notes", "Monitor blood pressure monthly");
    
    encryptEntityBySchema(record, "medical_records");
    
    EXPECT_TRUE(record.hasField("diagnosis_encrypted"));
    EXPECT_TRUE(record.hasField("prescription_encrypted"));
    EXPECT_TRUE(record.hasField("notes_encrypted"));
    EXPECT_EQ(record.getFieldAsString("patient_id").value(), "charlie"); // Not encrypted
}

// ============================================================================
// Test 4: Schema Updates and Migration
// ============================================================================

TEST_F(SchemaEncryptionTest, SchemaUpdate_AddField) {
    // Initial schema
    json schema1 = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema1);
    
    // Create entity with old schema
    BaseEntity entity("user:dave");
    entity.setField("id", "dave");
    entity.setField("email", "dave@example.com");
    entity.setField("phone", "+1-555-9999"); // Not in schema yet
    
    encryptEntityBySchema(entity, "users");
    
    EXPECT_TRUE(entity.hasField("email_encrypted"));
    EXPECT_FALSE(entity.hasField("phone_encrypted")); // Not encrypted
    
    // Update schema to include phone
    json schema2 = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "phone"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema2);
    
    // Create new entity with updated schema
    BaseEntity entity2("user:eve");
    entity2.setField("id", "eve");
    entity2.setField("email", "eve@example.com");
    entity2.setField("phone", "+1-555-8888");
    
    encryptEntityBySchema(entity2, "users");
    
    EXPECT_TRUE(entity2.hasField("email_encrypted"));
    EXPECT_TRUE(entity2.hasField("phone_encrypted")); // Now encrypted
}

TEST_F(SchemaEncryptionTest, SchemaUpdate_RemoveField) {
    // Initial schema with two fields
    json schema1 = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "phone"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema1);
    
    BaseEntity entity("user:frank");
    entity.setField("id", "frank");
    entity.setField("email", "frank@example.com");
    entity.setField("phone", "+1-555-7777");
    
    encryptEntityBySchema(entity, "users");
    db_->put("user:frank", entity.serialize());
    
    // Update schema - remove phone
    json schema2 = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema2);
    
    // Read old entity - phone should still be encrypted
    auto retrieved_opt = db_->get("user:frank");
    ASSERT_TRUE(retrieved_opt.has_value());
    auto retrieved = BaseEntity::deserialize("user:frank", *retrieved_opt);
    
    EXPECT_TRUE(retrieved.hasField("phone_encrypted")); // Still encrypted from before
    
    // Can still decrypt with old field list (manual)
    auto enc_json = json::parse(retrieved.getFieldAsString("phone_encrypted").value());
    auto blob = EncryptedBlob::fromJson(enc_json);
    auto decrypted = field_encryption_->decrypt(blob, "users.phone");
    EXPECT_EQ(std::string(decrypted.begin(), decrypted.end()), "+1-555-7777");
}

// ============================================================================
// Test 5: Edge Cases
// ============================================================================

TEST_F(SchemaEncryptionTest, EdgeCase_MissingField) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "ssn"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // Entity missing SSN field
    BaseEntity entity("user:grace");
    entity.setField("id", "grace");
    entity.setField("email", "grace@example.com");
    // No SSN field
    
    // Should not throw, just skip missing field
    EXPECT_NO_THROW(encryptEntityBySchema(entity, "users"));
    
    EXPECT_TRUE(entity.hasField("email_encrypted"));
    EXPECT_FALSE(entity.hasField("ssn_encrypted")); // Missing field not encrypted
}

TEST_F(SchemaEncryptionTest, EdgeCase_EmptyFieldValue) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"notes"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    BaseEntity entity("user:henry");
    entity.setField("id", "henry");
    entity.setField("notes", ""); // Empty string
    
    encryptEntityBySchema(entity, "users");
    
    // Should still encrypt empty strings
    EXPECT_TRUE(entity.hasField("notes_encrypted"));
    
    // Decrypt and verify
    db_->put("user:henry", entity.serialize());
    auto retrieved = BaseEntity::deserialize("user:henry", *db_->get("user:henry"));
    decryptEntityBySchema(retrieved, "users");
    
    EXPECT_EQ(retrieved.getFieldAsString("notes").value(), "");
}

TEST_F(SchemaEncryptionTest, EdgeCase_NonStringField) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"age"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    BaseEntity entity("user:iris");
    entity.setField("id", "iris");
    entity.setField("age", 25); // Integer, not string
    
    // Current implementation only encrypts strings - should skip non-string fields
    encryptEntityBySchema(entity, "users");
    
    // Field not encrypted (simplified implementation limitation)
    EXPECT_FALSE(entity.hasField("age_encrypted"));
    EXPECT_EQ(entity.getFieldAsInt("age").value(), 25);
}

// ============================================================================
// Test 6: Performance - Batch Operations
// ============================================================================

TEST_F(SchemaEncryptionTest, Performance_BatchEncryption) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "ssn"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    const size_t batch_size = 100;
    std::vector<BaseEntity> entities;
    
    // Create batch
    for (size_t i = 0; i < batch_size; ++i) {
        BaseEntity entity("user:" + std::to_string(i));
        entity.setField("id", std::to_string(i));
        entity.setField("email", "user" + std::to_string(i) + "@example.com");
        entity.setField("ssn", "000-00-" + std::to_string(1000 + i));
        entities.push_back(entity);
    }
    
    // Encrypt batch
    auto start = std::chrono::steady_clock::now();
    for (auto& entity : entities) {
        encryptEntityBySchema(entity, "users");
    }
    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    THEMIS_INFO("Batch encryption of {} entities took {} ms", batch_size, duration_ms);
    
    // Verify all encrypted
    for (const auto& entity : entities) {
        EXPECT_TRUE(entity.hasField("email_encrypted"));
        EXPECT_TRUE(entity.hasField("ssn_encrypted"));
    }
    
    // Performance expectation: should complete in reasonable time
    EXPECT_LT(duration_ms, 5000); // Less than 5 seconds for 100 entities
}

// ============================================================================
// Test 7: Security - Key Rotation
// ============================================================================

TEST_F(SchemaEncryptionTest, Security_KeyRotation) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // Encrypt with key version 1
    BaseEntity entity("user:jack");
    entity.setField("id", "jack");
    entity.setField("email", "jack@example.com");
    
    encryptEntityBySchema(entity, "users");
    db_->put("user:jack", entity.serialize());
    
    // Rotate key to version 2
    key_provider_->createKey("dek", 2);
    
    // Read old entity encrypted with v1
    auto retrieved = BaseEntity::deserialize("user:jack", *db_->get("user:jack"));
    
    // Should still decrypt with v1
    decryptEntityBySchema(retrieved, "users");
    EXPECT_EQ(retrieved.getFieldAsString("email").value(), "jack@example.com");
    
    // Re-encrypt with v2 (lazy re-encryption simulation)
    encryptEntityBySchema(retrieved, "users");
    
    auto enc_json = json::parse(retrieved.getFieldAsString("email_encrypted").value());
    auto blob = EncryptedBlob::fromJson(enc_json);
    
    // Should now use key version 2
    EXPECT_EQ(blob.key_version, 2);
}

// ============================================================================
// Test 8: Integration with Secondary Indexes
// ============================================================================

TEST_F(SchemaEncryptionTest, Integration_SecondaryIndex_PlainField) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // Create unique index on username (plain field)
    sec_idx_->createIndex("users", "username", true);
    
    BaseEntity entity("user:kate");
    entity.setField("id", "kate");
    entity.setField("username", "kate_admin");
    entity.setField("email", "kate@secret.com");
    
    encryptEntityBySchema(entity, "users");
    
    // Store and index
    db_->put("user:kate", entity.serialize());
    sec_idx_->insert("users", "username", "kate_admin", "user:kate");
    
    // Lookup by plain index should work
    auto results = sec_idx_->lookup("users", "username", "kate_admin");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], "user:kate");
    
    // Email should still be encrypted
    auto retrieved = BaseEntity::deserialize("user:kate", *db_->get("user:kate"));
    EXPECT_TRUE(retrieved.hasField("email_encrypted"));
}

TEST_F(SchemaEncryptionTest, Integration_RangeIndex_PlainField) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"ssn"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // Create range index on created_at (plain field)
    sec_idx_->createRangeIndex("users", "created_at");
    
    std::vector<std::string> pks;
    for (int i = 0; i < 5; ++i) {
        BaseEntity entity("user:" + std::to_string(i));
        entity.setField("id", std::to_string(i));
        entity.setField("created_at", static_cast<int64_t>(1730000000 + i * 1000));
        entity.setField("ssn", "000-00-000" + std::to_string(i));
        
        encryptEntityBySchema(entity, "users");
        db_->put("user:" + std::to_string(i), entity.serialize());
        sec_idx_->insertRange("users", "created_at", 1730000000 + i * 1000, "user:" + std::to_string(i));
        pks.push_back("user:" + std::to_string(i));
    }
    
    // Range query on plain field
    auto range_results = sec_idx_->range("users", "created_at", 1730000000, 1730003000);
    EXPECT_EQ(range_results.size(), 4); // 0, 1, 2, 3
    
    // All SSNs should be encrypted
    for (const auto& pk : pks) {
        auto entity = BaseEntity::deserialize(pk, *db_->get(pk));
        EXPECT_TRUE(entity.hasField("ssn_encrypted"));
    }
}

// ============================================================================
// Test 9: Complex Schema Scenarios
// ============================================================================

TEST_F(SchemaEncryptionTest, ComplexSchema_ConditionalEncryption) {
    // Schema with multiple collections, some enabled, some disabled
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"email", "ssn"}}
                }}
            }},
            {"products", {
                {"encryption", {
                    {"enabled", false},
                    {"fields", {"price"}}
                }}
            }},
            {"orders", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", {"credit_card", "billing_address"}}
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    // Users - should encrypt
    BaseEntity user("user:lisa");
    user.setField("id", "lisa");
    user.setField("email", "lisa@example.com");
    encryptEntityBySchema(user, "users");
    EXPECT_TRUE(user.hasField("email_encrypted"));
    
    // Products - should NOT encrypt (disabled)
    BaseEntity product("product:001");
    product.setField("id", "001");
    product.setField("price", 99.99);
    encryptEntityBySchema(product, "products");
    EXPECT_FALSE(product.hasField("price_encrypted"));
    
    // Orders - should encrypt
    BaseEntity order("order:001");
    order.setField("id", "001");
    order.setField("credit_card", "4111-1111-1111-1111");
    encryptEntityBySchema(order, "orders");
    EXPECT_TRUE(order.hasField("credit_card_encrypted"));
}

TEST_F(SchemaEncryptionTest, ComplexSchema_EmptyFieldsList) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"fields", json::array()} // Empty fields list
                }}
            }}
        }}
    };
    
    storeEncryptionSchema(schema);
    
    BaseEntity entity("user:mike");
    entity.setField("id", "mike");
    entity.setField("email", "mike@example.com");
    
    encryptEntityBySchema(entity, "users");
    
    // No fields should be encrypted
    EXPECT_FALSE(entity.hasField("email_encrypted"));
}
