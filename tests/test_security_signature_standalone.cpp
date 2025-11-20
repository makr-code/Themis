#include <gtest/gtest.h>
#include "storage/security_signature_manager.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>
#include <fstream>

using namespace themis::storage;
namespace fs = std::filesystem;

class SecuritySignatureManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_security_sig_db";
        
        // Clean up any existing test database
        if (fs::exists(test_db_path_)) {
            fs::remove_all(test_db_path_);
        }
        
        // Create RocksDB wrapper
        db_ = std::make_shared<RocksDBWrapper>();
        ASSERT_TRUE(db_->open(test_db_path_));
        
        // Create manager
        manager_ = std::make_shared<SecuritySignatureManager>(db_);
        
        // Create test file
        test_file_path_ = "test_resource.txt";
        std::ofstream f(test_file_path_);
        f << "Test content for signature verification\n";
        f.close();
    }
    
    void TearDown() override {
        manager_.reset();
        db_.reset();
        
        if (fs::exists(test_db_path_)) {
            fs::remove_all(test_db_path_);
        }
        if (fs::exists(test_file_path_)) {
            fs::remove(test_file_path_);
        }
    }
    
    std::string test_db_path_;
    std::string test_file_path_;
    std::shared_ptr<RocksDBWrapper> db_;
    std::shared_ptr<SecuritySignatureManager> manager_;
};

TEST_F(SecuritySignatureManagerTest, StoreAndRetrieveSignature) {
    SecuritySignature sig;
    sig.resource_id = "test/resource";
    sig.hash = "abcdef1234567890";
    sig.algorithm = "sha256";
    sig.created_at = 1732000000;
    sig.created_by = "test_user";
    sig.comment = "Test signature";
    
    // Store
    ASSERT_TRUE(manager_->storeSignature(sig));
    
    // Retrieve
    auto retrieved = manager_->getSignature("test/resource");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->resource_id, "test/resource");
    EXPECT_EQ(retrieved->hash, "abcdef1234567890");
    EXPECT_EQ(retrieved->algorithm, "sha256");
    EXPECT_EQ(retrieved->created_at, 1732000000);
    EXPECT_EQ(retrieved->created_by, "test_user");
    EXPECT_EQ(retrieved->comment, "Test signature");
}

TEST_F(SecuritySignatureManagerTest, DeleteSignature) {
    SecuritySignature sig;
    sig.resource_id = "test/deleteme";
    sig.hash = "deadbeef";
    sig.algorithm = "sha256";
    sig.created_at = 1732000000;
    
    // Store
    ASSERT_TRUE(manager_->storeSignature(sig));
    
    // Verify exists
    auto retrieved = manager_->getSignature("test/deleteme");
    ASSERT_TRUE(retrieved.has_value());
    
    // Delete
    ASSERT_TRUE(manager_->deleteSignature("test/deleteme"));
    
    // Verify deleted
    retrieved = manager_->getSignature("test/deleteme");
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(SecuritySignatureManagerTest, ListAllSignatures) {
    // Store multiple signatures
    for (int i = 0; i < 5; ++i) {
        SecuritySignature sig;
        sig.resource_id = "test/resource" + std::to_string(i);
        sig.hash = "hash" + std::to_string(i);
        sig.algorithm = "sha256";
        sig.created_at = 1732000000 + i;
        ASSERT_TRUE(manager_->storeSignature(sig));
    }
    
    // List all
    auto signatures = manager_->listAllSignatures();
    EXPECT_EQ(signatures.size(), 5);
    
    // Verify all are present
    std::set<std::string> resource_ids;
    for (const auto& sig : signatures) {
        resource_ids.insert(sig.resource_id);
    }
    
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(resource_ids.count("test/resource" + std::to_string(i)) > 0);
    }
}

TEST_F(SecuritySignatureManagerTest, ComputeFileHash) {
    std::string hash = SecuritySignatureManager::computeFileHash(test_file_path_);
    
    // Hash should be 64 hex characters (SHA256)
    EXPECT_EQ(hash.length(), 64);
    
    // Verify all characters are hex
    for (char c : hash) {
        EXPECT_TRUE(std::isxdigit(c));
    }
    
    // Hash should be deterministic
    std::string hash2 = SecuritySignatureManager::computeFileHash(test_file_path_);
    EXPECT_EQ(hash, hash2);
}

TEST_F(SecuritySignatureManagerTest, VerifyFile_Success) {
    // Compute hash and store signature
    std::string hash = SecuritySignatureManager::computeFileHash(test_file_path_);
    
    SecuritySignature sig;
    sig.resource_id = test_file_path_;
    sig.hash = hash;
    sig.algorithm = "sha256";
    sig.created_at = 1732000000;
    
    ASSERT_TRUE(manager_->storeSignature(sig));
    
    // Verify should succeed
    EXPECT_TRUE(manager_->verifyFile(test_file_path_, test_file_path_));
}

TEST_F(SecuritySignatureManagerTest, VerifyFile_Mismatch) {
    // Store signature with wrong hash
    SecuritySignature sig;
    sig.resource_id = test_file_path_;
    sig.hash = "wronghash1234567890abcdef1234567890abcdef1234567890abcdef12345678";
    sig.algorithm = "sha256";
    sig.created_at = 1732000000;
    
    ASSERT_TRUE(manager_->storeSignature(sig));
    
    // Verify should fail
    EXPECT_FALSE(manager_->verifyFile(test_file_path_, test_file_path_));
}

TEST_F(SecuritySignatureManagerTest, VerifyFile_NoSignature) {
    // Verify without stored signature should fail
    EXPECT_FALSE(manager_->verifyFile(test_file_path_, "nonexistent_resource"));
}

TEST_F(SecuritySignatureManagerTest, NormalizeResourceId) {
    // Test basic normalization
    std::string normalized = SecuritySignatureManager::normalizeResourceId("./config/test.yaml");
    EXPECT_TRUE(normalized.find("./") == std::string::npos || normalized.find("./") != 0);
    
    // Generic format (forward slashes)
    EXPECT_TRUE(normalized.find('\\') == std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
