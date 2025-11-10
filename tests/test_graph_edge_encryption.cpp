#include <gtest/gtest.h>
#include "storage/base_entity.h"
#include "storage/rocksdb_wrapper.h"
#include "index/graph_index.h"
#include "security/encryption.h"
#include "security/mock_key_provider.h"
#include "auth/jwt_validator.h"
#include "utils/hkdf_helper.h"
#include <nlohmann/json.hpp>
#include <filesystem>

using namespace themis;
using namespace themis::auth;
using json = nlohmann::json;

class GraphEdgeEncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary database
        test_dir_ = std::filesystem::temp_directory_path() / "themis_test_graph_enc";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        
        RocksDBWrapper::Config config;
        config.db_path = test_dir_.string();
        db_ = std::make_unique<RocksDBWrapper>(config);
        db_->open();
        
        graph_mgr_ = std::make_unique<GraphIndexManager>(*db_);
        
        // Setup encryption infrastructure
        key_provider_ = std::make_shared<MockKeyProvider>();
        key_provider_->createKey("dek", 1);
        field_encryption_ = std::make_shared<FieldEncryption>(key_provider_);
        
        // Setup JWT claims for user context
        claims_.sub = "user123";
        claims_.email = "test@example.com";
        claims_.groups = {"engineering"};
        
        // Get DEK for key derivation
        dek_ = key_provider_->getKey("dek", 1);
    }
    
    void TearDown() override {
        graph_mgr_.reset();
        db_->close();
        db_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    BaseEntity createEdge(const std::string& id, 
                         const std::string& from,
                         const std::string& to,
                         double weight = 1.0,
                         const std::string& metadata = "") {
        BaseEntity::FieldMap fields;
        fields["id"] = id;
        fields["_from"] = from;
        fields["_to"] = to;
        fields["label"] = std::string("KNOWS");
        fields["created_at"] = static_cast<int64_t>(1730000000);
        fields["weight"] = weight;
        
        if (!metadata.empty()) {
            fields["metadata"] = metadata;
        }
        
        return BaseEntity::fromFields(id, fields);
    }
    
    BaseEntity encryptEdgeFields(const BaseEntity& edge,
                                const std::vector<std::string>& encrypted_fields) {
        BaseEntity::FieldMap fields;
        
        // Copy all standard fields
        if (auto id = edge.getFieldAsString("id")) fields["id"] = *id;
        if (auto from = edge.getFieldAsString("_from")) fields["_from"] = *from;
        if (auto to = edge.getFieldAsString("_to")) fields["_to"] = *to;
        if (auto label = edge.getFieldAsString("label")) fields["label"] = *label;
        if (auto ts = edge.getFieldAsInt("created_at")) fields["created_at"] = *ts;
        
        // Handle weight field
        if (auto weight = edge.getFieldAsDouble("weight")) {
            if (std::find(encrypted_fields.begin(), encrypted_fields.end(), "weight") != encrypted_fields.end()) {
                // Encrypt weight
                std::string plain_str = std::to_string(*weight);
                auto user_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.weight");
                auto encrypted_blob = field_encryption_->encryptWithKey(
                    plain_str, "edges.weight", 1, user_key);
                fields["weight_encrypted"] = encrypted_blob.toBase64();
            } else {
                // Keep plaintext
                fields["weight"] = *weight;
            }
        }
        
        // Handle metadata field
        if (auto metadata = edge.getFieldAsString("metadata")) {
            if (std::find(encrypted_fields.begin(), encrypted_fields.end(), "metadata") != encrypted_fields.end()) {
                // Encrypt metadata
                auto user_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.metadata");
                auto encrypted_blob = field_encryption_->encryptWithKey(
                    *metadata, "edges.metadata", 1, user_key);
                fields["metadata_encrypted"] = encrypted_blob.toBase64();
            } else {
                // Keep plaintext
                fields["metadata"] = *metadata;
            }
        }
        
        // Handle custom_prop field if exists
        if (auto custom = edge.getFieldAsString("custom_prop")) {
            if (std::find(encrypted_fields.begin(), encrypted_fields.end(), "custom_prop") != encrypted_fields.end()) {
                auto user_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.custom_prop");
                auto encrypted_blob = field_encryption_->encryptWithKey(
                    *custom, "edges.custom_prop", 1, user_key);
                fields["custom_prop_encrypted"] = encrypted_blob.toBase64();
            } else {
                fields["custom_prop"] = *custom;
            }
        }
        
        return BaseEntity::fromFields(edge.getPrimaryKey(), fields);
    }
    
    std::filesystem::path test_dir_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<GraphIndexManager> graph_mgr_;
    std::shared_ptr<MockKeyProvider> key_provider_;
    std::shared_ptr<FieldEncryption> field_encryption_;
    JWTClaims claims_;
    std::vector<uint8_t> dek_;
};

TEST_F(GraphEdgeEncryptionTest, AddEdge_WithoutEncryption_StoresPlaintext) {
    auto edge = createEdge("e1", "alice", "bob", 0.95, "university context");
    auto status = graph_mgr_->addEdge(edge);
    ASSERT_TRUE(status.ok);
    
    // Verify edge is stored in plaintext
    std::string edge_key = "edge:e1";
    auto blob = db_->get(edge_key);
    ASSERT_TRUE(blob.has_value());
    
    auto loaded = BaseEntity::deserialize("e1", *blob);
    auto weight = loaded.getFieldAsDouble("weight");
    ASSERT_TRUE(weight.has_value());
    EXPECT_DOUBLE_EQ(0.95, *weight);
    
    auto metadata = loaded.getFieldAsString("metadata");
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ("university context", *metadata);
}

TEST_F(GraphEdgeEncryptionTest, AddEdge_WithEncryption_StoresEncryptedFields) {
    auto edge = createEdge("e2", "alice", "charlie", 0.85, "confidential");
    auto encrypted = encryptEdgeFields(edge, {"weight", "metadata"});
    
    auto status = graph_mgr_->addEdge(encrypted);
    ASSERT_TRUE(status.ok);
    
    // Verify encrypted fields exist
    std::string edge_key = "edge:e2";
    auto blob = db_->get(edge_key);
    ASSERT_TRUE(blob.has_value());
    
    auto loaded = BaseEntity::deserialize("e2", *blob);
    
    // Plaintext fields should be absent
    EXPECT_FALSE(loaded.hasField("weight"));
    EXPECT_FALSE(loaded.hasField("metadata"));
    
    // Encrypted fields should exist
    EXPECT_TRUE(loaded.hasField("weight_encrypted"));
    EXPECT_TRUE(loaded.hasField("metadata_encrypted"));
}

TEST_F(GraphEdgeEncryptionTest, EncryptedEdge_Decrypt_RoundTrip) {
    auto edge = createEdge("e3", "bob", "dave", 0.75, "project team");
    auto encrypted = encryptEdgeFields(edge, {"weight", "metadata"});
    
    graph_mgr_->addEdge(encrypted);
    
    // Load and decrypt
    std::string edge_key = "edge:e3";
    auto blob = db_->get(edge_key);
    ASSERT_TRUE(blob.has_value());
    
    auto loaded = BaseEntity::deserialize("e3", *blob);
    
    // Decrypt weight
    auto weight_enc_b64 = loaded.getFieldAsString("weight_encrypted");
    ASSERT_TRUE(weight_enc_b64.has_value());
    
    auto weight_blob = EncryptedBlob::fromBase64(*weight_enc_b64);
    auto user_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.weight");
    auto weight_plain = field_encryption_->decryptWithKey(weight_blob, user_key);
    
    EXPECT_EQ("0.750000", weight_plain);
    
    // Decrypt metadata
    auto metadata_enc_b64 = loaded.getFieldAsString("metadata_encrypted");
    ASSERT_TRUE(metadata_enc_b64.has_value());
    
    auto metadata_blob = EncryptedBlob::fromBase64(*metadata_enc_b64);
    auto metadata_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.metadata");
    auto metadata_plain = field_encryption_->decryptWithKey(metadata_blob, metadata_key);
    
    EXPECT_EQ("project team", metadata_plain);
}

TEST_F(GraphEdgeEncryptionTest, DifferentUser_CannotDecrypt) {
    auto edge = createEdge("e4", "alice", "eve", 0.65, "secret");
    auto encrypted = encryptEdgeFields(edge, {"metadata"});
    
    graph_mgr_->addEdge(encrypted);
    
    // User2 tries to decrypt with their key
    JWTClaims claims2 = claims_;
    claims2.sub = "user456";
    
    std::string edge_key = "edge:e4";
    auto blob = db_->get(edge_key);
    auto loaded = BaseEntity::deserialize("e4", *blob);
    
    auto metadata_enc_b64 = loaded.getFieldAsString("metadata_encrypted");
    ASSERT_TRUE(metadata_enc_b64.has_value());
    
    auto metadata_blob = EncryptedBlob::fromBase64(*metadata_enc_b64);
    auto user2_key = JWTValidator::deriveUserKey(dek_, claims2, "edges.metadata");
    
    // Decryption should fail (auth tag mismatch)
    EXPECT_THROW(
        field_encryption_->decryptWithKey(metadata_blob, user2_key),
        std::runtime_error
    );
}

TEST_F(GraphEdgeEncryptionTest, GroupEncryption_MultipleUsersAccess) {
    auto edge = createEdge("e5", "alice", "bob", 0.88, "team project");
    
    // Use group-based encryption
    std::string group_context = "engineering";
    std::vector<uint8_t> salt(group_context.begin(), group_context.end());
    auto group_key = themis::utils::HKDFHelper::derive(dek_, salt, "group-field:edges.metadata", 32);
    
    // Encrypt with group key
    auto metadata_blob = field_encryption_->encryptWithKey(
        "team project", "edges.metadata", 1, group_key);
    
    BaseEntity::FieldMap fields;
    fields["id"] = std::string("e5");
    fields["_from"] = std::string("alice");
    fields["_to"] = std::string("bob");
    fields["label"] = std::string("COLLABORATES");
    fields["metadata_encrypted"] = metadata_blob.toBase64();
    
    auto encrypted_edge = BaseEntity::fromFields("e5", fields);
    graph_mgr_->addEdge(encrypted_edge);
    
    // User1 (engineering member) can decrypt
    EXPECT_TRUE(JWTValidator::hasAccess(claims_, "engineering"));
    auto decrypted1 = field_encryption_->decryptWithKey(metadata_blob, group_key);
    EXPECT_EQ("team project", decrypted1);
    
    // User2 (also engineering) can decrypt
    JWTClaims claims2 = claims_;
    claims2.sub = "user789";
    claims2.groups = {"engineering"};
    EXPECT_TRUE(JWTValidator::hasAccess(claims2, "engineering"));
    auto decrypted2 = field_encryption_->decryptWithKey(metadata_blob, group_key);
    EXPECT_EQ("team project", decrypted2);
    
    // User3 (not engineering) cannot access
    JWTClaims claims3;
    claims3.sub = "user999";
    claims3.groups = {"finance"};
    EXPECT_FALSE(JWTValidator::hasAccess(claims3, "engineering"));
}

TEST_F(GraphEdgeEncryptionTest, PartialEncryption_WeightPlain_MetadataEncrypted) {
    auto edge = createEdge("e6", "alice", "frank", 0.92, "sensitive info");
    auto encrypted = encryptEdgeFields(edge, {"metadata"}); // Only encrypt metadata
    
    graph_mgr_->addEdge(encrypted);
    
    std::string edge_key = "edge:e6";
    auto blob = db_->get(edge_key);
    auto loaded = BaseEntity::deserialize("e6", *blob);
    
    // Weight should remain in plaintext (for graph algorithms)
    auto weight = loaded.getFieldAsDouble("weight");
    ASSERT_TRUE(weight.has_value());
    EXPECT_DOUBLE_EQ(0.92, *weight);
    
    // Metadata should be encrypted
    EXPECT_FALSE(loaded.hasField("metadata"));
    EXPECT_TRUE(loaded.hasField("metadata_encrypted"));
}

TEST_F(GraphEdgeEncryptionTest, TopologyFields_AlwaysPlaintext) {
    auto edge = createEdge("e7", "alice", "george", 0.78);
    auto encrypted = encryptEdgeFields(edge, {"weight"});
    
    graph_mgr_->addEdge(encrypted);
    
    // Verify adjacency indices are created (plaintext)
    auto out_result = graph_mgr_->outNeighbors("alice");
    ASSERT_TRUE(out_result.first.ok);
    
    bool found_george = false;
    for (const auto& neighbor : out_result.second) {
        if (neighbor == "george") {
            found_george = true;
            break;
        }
    }
    EXPECT_TRUE(found_george);
    
    // Verify in-adjacency
    auto in_result = graph_mgr_->inNeighbors("george");
    ASSERT_TRUE(in_result.first.ok);
    
    bool found_alice = false;
    for (const auto& neighbor : in_result.second) {
        if (neighbor == "alice") {
            found_alice = true;
            break;
        }
    }
    EXPECT_TRUE(found_alice);
}

TEST_F(GraphEdgeEncryptionTest, BFS_WithEncryptedEdges_StillTraverses) {
    // Create encrypted edge: alice -> bob
    auto e1 = createEdge("e8", "alice", "bob", 1.0);
    auto e1_enc = encryptEdgeFields(e1, {"weight"});
    graph_mgr_->addEdge(e1_enc);
    
    // Verify adjacency works (topology is still plaintext)
    auto out_result = graph_mgr_->outNeighbors("alice");
    ASSERT_TRUE(out_result.first.ok);
    
    bool found_bob = false;
    for (const auto& neighbor : out_result.second) {
        if (neighbor == "bob") {
            found_bob = true;
            break;
        }
    }
    EXPECT_TRUE(found_bob) << "BFS should work with encrypted edges (topology is plaintext)";
}

TEST_F(GraphEdgeEncryptionTest, MultipleFieldsEncryption_AllDecryptCorrectly) {
    BaseEntity::FieldMap fields;
    fields["id"] = std::string("e10");
    fields["_from"] = std::string("alice");
    fields["_to"] = std::string("helen");
    fields["weight"] = 0.88;
    fields["metadata"] = std::string("confidential project");
    fields["custom_prop"] = std::string("extra data");
    
    auto edge = BaseEntity::fromFields("e10", fields);
    auto encrypted = encryptEdgeFields(edge, {"weight", "metadata", "custom_prop"});
    
    graph_mgr_->addEdge(encrypted);
    
    // Load and verify all encrypted
    std::string edge_key = "edge:e10";
    auto blob = db_->get(edge_key);
    auto loaded = BaseEntity::deserialize("e10", *blob);
    
    EXPECT_TRUE(loaded.hasField("weight_encrypted"));
    EXPECT_TRUE(loaded.hasField("metadata_encrypted"));
    EXPECT_TRUE(loaded.hasField("custom_prop_encrypted"));
    
    // Decrypt all
    auto weight_blob = EncryptedBlob::fromBase64(*loaded.getFieldAsString("weight_encrypted"));
    auto weight_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.weight");
    auto weight_plain = field_encryption_->decryptWithKey(weight_blob, weight_key);
    EXPECT_EQ("0.880000", weight_plain);
    
    auto metadata_blob = EncryptedBlob::fromBase64(*loaded.getFieldAsString("metadata_encrypted"));
    auto metadata_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.metadata");
    auto metadata_plain = field_encryption_->decryptWithKey(metadata_blob, metadata_key);
    EXPECT_EQ("confidential project", metadata_plain);
    
    auto custom_blob = EncryptedBlob::fromBase64(*loaded.getFieldAsString("custom_prop_encrypted"));
    auto custom_key = JWTValidator::deriveUserKey(dek_, claims_, "edges.custom_prop");
    auto custom_plain = field_encryption_->decryptWithKey(custom_blob, custom_key);
    EXPECT_EQ("extra data", custom_plain);
}

TEST_F(GraphEdgeEncryptionTest, EncryptedEdge_DeletedCorrectly) {
    auto edge = createEdge("e11", "alice", "ivan", 0.7, "temporary");
    auto encrypted = encryptEdgeFields(edge, {"metadata"});
    
    graph_mgr_->addEdge(encrypted);
    
    // Verify edge exists
    std::string edge_key = "edge:e11";
    EXPECT_TRUE(db_->get(edge_key).has_value());
    
    // Delete edge
    auto status = graph_mgr_->deleteEdge("e11");
    ASSERT_TRUE(status.ok);
    
    // Verify edge is deleted
    EXPECT_FALSE(db_->get(edge_key).has_value());
    
    // Verify adjacency indices are removed
    auto out_result = graph_mgr_->outNeighbors("alice");
    ASSERT_TRUE(out_result.first.ok);
    
    for (const auto& neighbor : out_result.second) {
        EXPECT_NE("ivan", neighbor);
    }
}
