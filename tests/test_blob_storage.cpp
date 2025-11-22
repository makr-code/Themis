#include <gtest/gtest.h>
#include "storage/blob_storage_backend.h"
#include "storage/blob_storage_manager.h"
#include <filesystem>
#include <fstream>

// Forward declarations - implementations in blob_backend_filesystem.cpp
namespace themis {
namespace storage {
class FilesystemBlobBackend;
}
}

// Include implementation
#include "../src/storage/blob_backend_filesystem.cpp"

namespace fs = std::filesystem;

class FilesystemBlobBackendTest : public ::testing::Test {
protected:
    std::string test_path_ = "./test_blobs";
    
    void SetUp() override {
        // Clean up test directory
        if (fs::exists(test_path_)) {
            fs::remove_all(test_path_);
        }
    }
    
    void TearDown() override {
        // Clean up test directory
        if (fs::exists(test_path_)) {
            fs::remove_all(test_path_);
        }
    }
};

TEST_F(FilesystemBlobBackendTest, PutAndGet) {
    themis::storage::FilesystemBlobBackend backend(test_path_);
    
    std::string blob_id = "test123abc";
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    
    // Put blob
    auto ref = backend.put(blob_id, data);
    
    EXPECT_EQ(ref.id, blob_id);
    EXPECT_EQ(ref.type, themis::storage::BlobStorageType::FILESYSTEM);
    EXPECT_EQ(ref.size_bytes, 5);
    EXPECT_FALSE(ref.hash_sha256.empty());
    EXPECT_TRUE(fs::exists(ref.uri));
    
    // Get blob
    auto retrieved = backend.get(ref);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, data);
}

TEST_F(FilesystemBlobBackendTest, HierarchicalStructure) {
    themis::storage::FilesystemBlobBackend backend(test_path_);
    
    std::string blob_id = "abcd1234efgh5678";
    std::vector<uint8_t> data = {10, 20, 30};
    
    auto ref = backend.put(blob_id, data);
    
    // Check hierarchical path: test_blobs/ab/cd/abcd1234efgh5678.blob
    EXPECT_TRUE(ref.uri.find("/ab/cd/") != std::string::npos);
    EXPECT_TRUE(ref.uri.ends_with(".blob"));
}

TEST_F(FilesystemBlobBackendTest, RemoveBlob) {
    themis::storage::FilesystemBlobBackend backend(test_path_);
    
    std::string blob_id = "test456def";
    std::vector<uint8_t> data = {7, 8, 9};
    
    auto ref = backend.put(blob_id, data);
    EXPECT_TRUE(backend.exists(ref));
    
    bool removed = backend.remove(ref);
    EXPECT_TRUE(removed);
    EXPECT_FALSE(backend.exists(ref));
}

TEST_F(FilesystemBlobBackendTest, GetNonExistent) {
    themis::storage::FilesystemBlobBackend backend(test_path_);
    
    themis::storage::BlobRef ref;
    ref.id = "nonexistent";
    ref.type = themis::storage::BlobStorageType::FILESYSTEM;
    ref.uri = test_path_ + "/xx/yy/nonexistent.blob";
    
    auto result = backend.get(ref);
    EXPECT_FALSE(result.has_value());
}

TEST_F(FilesystemBlobBackendTest, IsAvailable) {
    themis::storage::FilesystemBlobBackend backend(test_path_);
    EXPECT_TRUE(backend.isAvailable());
    EXPECT_EQ(backend.name(), "filesystem");
}

TEST_F(FilesystemBlobBackendTest, LargeBlobRoundTrip) {
    themis::storage::FilesystemBlobBackend backend(test_path_);
    
    // Create 1 MB blob
    std::vector<uint8_t> data(1024 * 1024);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i % 256);
    }
    
    std::string blob_id = "large_blob_001";
    auto ref = backend.put(blob_id, data);
    
    EXPECT_EQ(ref.size_bytes, 1024 * 1024);
    
    auto retrieved = backend.get(ref);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, data);
}

// BlobStorageManager tests
class BlobStorageManagerTest : public ::testing::Test {
protected:
    std::string test_path_ = "./test_blob_manager";
    
    void SetUp() override {
        if (fs::exists(test_path_)) {
            fs::remove_all(test_path_);
        }
    }
    
    void TearDown() override {
        if (fs::exists(test_path_)) {
            fs::remove_all(test_path_);
        }
    }
};

TEST_F(BlobStorageManagerTest, AutomaticBackendSelection) {
    themis::storage::BlobStorageConfig config;
    config.inline_threshold_bytes = 1024;  // 1 KB
    config.rocksdb_blob_threshold_bytes = 10240;  // 10 KB
    config.enable_filesystem = true;
    config.filesystem_base_path = test_path_;
    
    themis::storage::BlobStorageManager manager(config);
    
    // Register filesystem backend
    auto fs_backend = std::make_shared<themis::storage::FilesystemBlobBackend>(test_path_);
    manager.registerBackend(themis::storage::BlobStorageType::FILESYSTEM, fs_backend);
    
    // Small blob (should not use filesystem, but we only have filesystem registered)
    std::vector<uint8_t> small_data(512);
    auto small_ref = manager.put("small_blob", small_data);
    EXPECT_EQ(small_ref.type, themis::storage::BlobStorageType::FILESYSTEM);
    
    // Large blob (should use filesystem)
    std::vector<uint8_t> large_data(20 * 1024);
    auto large_ref = manager.put("large_blob", large_data);
    EXPECT_EQ(large_ref.type, themis::storage::BlobStorageType::FILESYSTEM);
}

TEST_F(BlobStorageManagerTest, GetBlob) {
    themis::storage::BlobStorageConfig config;
    config.filesystem_base_path = test_path_;
    
    themis::storage::BlobStorageManager manager(config);
    
    auto fs_backend = std::make_shared<themis::storage::FilesystemBlobBackend>(test_path_);
    manager.registerBackend(themis::storage::BlobStorageType::FILESYSTEM, fs_backend);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto ref = manager.put("test_blob", data);
    
    auto retrieved = manager.get(ref);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, data);
}

TEST_F(BlobStorageManagerTest, RemoveBlob) {
    themis::storage::BlobStorageConfig config;
    config.filesystem_base_path = test_path_;
    
    themis::storage::BlobStorageManager manager(config);
    
    auto fs_backend = std::make_shared<themis::storage::FilesystemBlobBackend>(test_path_);
    manager.registerBackend(themis::storage::BlobStorageType::FILESYSTEM, fs_backend);
    
    std::vector<uint8_t> data = {1, 2, 3};
    auto ref = manager.put("test_blob", data);
    
    EXPECT_TRUE(manager.exists(ref));
    EXPECT_TRUE(manager.remove(ref));
    EXPECT_FALSE(manager.exists(ref));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
