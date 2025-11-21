#pragma once

#include "storage/blob_storage_backend.h"
#include <unordered_map>
#include <mutex>

namespace themis {
namespace storage {

/**
 * @brief Blob Storage Manager
 * 
 * Orchestrates multiple blob storage backends and automatically selects
 * the appropriate backend based on blob size and configuration.
 * 
 * Selection Strategy:
 * - < inline_threshold_bytes: INLINE (stored in RocksDB)
 * - < rocksdb_blob_threshold_bytes: ROCKSDB_BLOB (RocksDB BlobDB)
 * - >= rocksdb_blob_threshold_bytes: External backend (Filesystem/S3/Azure/WebDAV)
 * 
 * Thread-Safety: All methods are thread-safe.
 */
class BlobStorageManager {
private:
    BlobStorageConfig config_;
    std::unordered_map<BlobStorageType, std::shared_ptr<IBlobStorageBackend>> backends_;
    mutable std::mutex mutex_;
    
    BlobStorageType selectBackendType(size_t blob_size) const {
        if (blob_size < static_cast<size_t>(config_.inline_threshold_bytes)) {
            return BlobStorageType::INLINE;
        }
        
        if (blob_size < static_cast<size_t>(config_.rocksdb_blob_threshold_bytes)) {
            return BlobStorageType::ROCKSDB_BLOB;
        }
        
        // Select external backend (prefer enabled backends in order)
        if (config_.enable_webdav) {
            return BlobStorageType::WEBDAV;
        }
        if (config_.enable_s3) {
            return BlobStorageType::S3;
        }
        if (config_.enable_azure) {
            return BlobStorageType::AZURE_BLOB;
        }
        if (config_.enable_filesystem) {
            return BlobStorageType::FILESYSTEM;
        }
        
        // Default to filesystem
        return BlobStorageType::FILESYSTEM;
    }
    
public:
    explicit BlobStorageManager(const BlobStorageConfig& config)
        : config_(config) {}
    
    /**
     * @brief Register a blob storage backend
     * @param type Backend type
     * @param backend Backend implementation
     */
    void registerBackend(BlobStorageType type, std::shared_ptr<IBlobStorageBackend> backend) {
        std::lock_guard<std::mutex> lock(mutex_);
        backends_[type] = backend;
    }
    
    /**
     * @brief Store a blob with automatic backend selection
     * @param blob_id Unique blob identifier
     * @param data Blob data
     * @return BlobRef Reference to stored blob
     * @throws std::runtime_error if no suitable backend available
     */
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) {
        BlobStorageType target_type = selectBackendType(data.size());
        
        std::shared_ptr<IBlobStorageBackend> backend;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = backends_.find(target_type);
            if (it != backends_.end() && it->second && it->second->isAvailable()) {
                backend = it->second;
            } else {
                // Fallback to filesystem
                auto fs_it = backends_.find(BlobStorageType::FILESYSTEM);
                if (fs_it != backends_.end() && fs_it->second && fs_it->second->isAvailable()) {
                    backend = fs_it->second;
                    target_type = BlobStorageType::FILESYSTEM;
                }
            }
        }
        
        if (!backend) {
            throw std::runtime_error("No suitable blob storage backend available");
        }
        
        return backend->put(blob_id, data);
    }
    
    /**
     * @brief Retrieve a blob
     * @param ref Blob reference
     * @return Blob data or nullopt if not found
     */
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) {
        std::shared_ptr<IBlobStorageBackend> backend;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = backends_.find(ref.type);
            if (it != backends_.end()) {
                backend = it->second;
            }
        }
        
        if (!backend) {
            throw std::runtime_error(
                "Backend not registered for type: " + std::to_string(static_cast<int>(ref.type))
            );
        }
        
        return backend->get(ref);
    }
    
    /**
     * @brief Delete a blob
     * @param ref Blob reference
     * @return true if deleted
     */
    bool remove(const BlobRef& ref) {
        std::shared_ptr<IBlobStorageBackend> backend;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = backends_.find(ref.type);
            if (it != backends_.end()) {
                backend = it->second;
            }
        }
        
        if (!backend) {
            return false;
        }
        
        return backend->remove(ref);
    }
    
    /**
     * @brief Check if blob exists
     * @param ref Blob reference
     * @return true if exists
     */
    bool exists(const BlobRef& ref) {
        std::shared_ptr<IBlobStorageBackend> backend;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = backends_.find(ref.type);
            if (it != backends_.end()) {
                backend = it->second;
            }
        }
        
        if (!backend) {
            return false;
        }
        
        return backend->exists(ref);
    }
    
    /**
     * @brief Get configuration
     */
    const BlobStorageConfig& getConfig() const {
        return config_;
    }
    
    /**
     * @brief Get registered backend types
     */
    std::vector<BlobStorageType> getRegisteredBackends() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BlobStorageType> types;
        for (const auto& pair : backends_) {
            types.push_back(pair.first);
        }
        return types;
    }
};

} // namespace storage
} // namespace themis
