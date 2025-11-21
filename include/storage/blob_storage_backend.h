#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstdint>

namespace themis {
namespace storage {

/**
 * @brief Blob Storage Type
 */
enum class BlobStorageType {
    INLINE,       // RocksDB inline (< 1 MB)
    ROCKSDB_BLOB, // RocksDB BlobDB (1-10 MB)
    FILESYSTEM,   // Local filesystem
    S3,           // AWS S3
    AZURE_BLOB,   // Azure Blob Storage
    GCS,          // Google Cloud Storage
    WEBDAV,       // WebDAV (for ActiveDirectory/SharePoint integration)
    CUSTOM        // User-defined backend
};

/**
 * @brief Blob Reference
 * 
 * Contains metadata about a blob stored in an external backend.
 */
struct BlobRef {
    std::string id;           // Blob ID (UUID)
    BlobStorageType type;     // Storage backend type
    std::string uri;          // Backend-specific URI
    int64_t size_bytes;       // Original size
    std::string hash_sha256;  // Content hash (for integrity)
    
    // Optional metadata
    int64_t created_at = 0;   // Unix timestamp
    bool compressed = false;   // Is blob compressed?
    std::string compression_type; // e.g., "zstd"
};

/**
 * @brief Blob Storage Backend Interface
 * 
 * Abstract interface for external blob storage backends.
 * Implementations include: Filesystem, S3, Azure Blob, WebDAV (ActiveDirectory).
 * 
 * Thread-Safety: Implementations must be thread-safe.
 */
class IBlobStorageBackend {
public:
    virtual ~IBlobStorageBackend() = default;
    
    /**
     * @brief Store a blob
     * @param blob_id Unique blob identifier
     * @param data Blob data
     * @return BlobRef Reference to stored blob
     * @throws std::runtime_error on failure
     */
    virtual BlobRef put(
        const std::string& blob_id,
        const std::vector<uint8_t>& data
    ) = 0;
    
    /**
     * @brief Retrieve a blob
     * @param ref Blob reference
     * @return Blob data or nullopt if not found
     */
    virtual std::optional<std::vector<uint8_t>> get(
        const BlobRef& ref
    ) = 0;
    
    /**
     * @brief Delete a blob
     * @param ref Blob reference
     * @return true if deleted, false if not found
     */
    virtual bool remove(const BlobRef& ref) = 0;
    
    /**
     * @brief Check if blob exists
     * @param ref Blob reference
     * @return true if exists
     */
    virtual bool exists(const BlobRef& ref) = 0;
    
    /**
     * @brief Get backend name
     * @return Backend name (e.g., "filesystem", "s3", "webdav")
     */
    virtual std::string name() const = 0;
    
    /**
     * @brief Check if backend is available
     * @return true if backend can be used
     */
    virtual bool isAvailable() const = 0;
};

/**
 * @brief Blob Storage Configuration
 */
struct BlobStorageConfig {
    // Thresholds
    int64_t inline_threshold_bytes = 1024 * 1024;      // 1 MB - inline in RocksDB
    int64_t rocksdb_blob_threshold_bytes = 10 * 1024 * 1024; // 10 MB - RocksDB BlobDB
    
    // Filesystem backend
    bool enable_filesystem = true;
    std::string filesystem_base_path = "./data/blobs";
    
    // S3 backend
    bool enable_s3 = false;
    std::string s3_bucket;
    std::string s3_region = "us-east-1";
    std::string s3_prefix;
    
    // Azure backend
    bool enable_azure = false;
    std::string azure_connection_string;
    std::string azure_container;
    
    // WebDAV backend (for ActiveDirectory/SharePoint)
    bool enable_webdav = false;
    std::string webdav_base_url;
    std::string webdav_username;
    std::string webdav_password;
    bool webdav_verify_ssl = true;
};

} // namespace storage
} // namespace themis
