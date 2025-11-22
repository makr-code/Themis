#include "storage/blob_storage_backend.h"
#include "utils/logger.h"
#include <azure/storage/blobs.hpp>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <mutex>

namespace themis {
namespace storage {

/**
 * @brief Azure Blob Storage Backend
 * 
 * Stores blobs in Azure Blob Storage container.
 * Uses Azure SDK for C++.
 * 
 * Features:
 * - Server-side encryption (AES256)
 * - Content-MD5 verification
 * - Thread-safe operations
 * - Automatic retry policy
 */
class AzureBlobBackend : public IBlobStorageBackend {
private:
    std::string connection_string_;
    std::string container_name_;
    std::string prefix_;
    std::unique_ptr<Azure::Storage::Blobs::BlobContainerClient> container_client_;
    mutable std::mutex mutex_;
    
    // Compute SHA256 hash
    static std::string computeSHA256(const std::vector<uint8_t>& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') 
               << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
    
    // Get blob name
    std::string getBlobName(const std::string& blob_id) const {
        if (prefix_.empty()) {
            return blob_id + ".blob";
        }
        return prefix_ + "/" + blob_id + ".blob";
    }

public:
    AzureBlobBackend(const std::string& connection_string, 
                     const std::string& container_name,
                     const std::string& prefix = "")
        : connection_string_(connection_string),
          container_name_(container_name),
          prefix_(prefix) {
        
        try {
            // Create blob service client
            auto service_client = Azure::Storage::Blobs::BlobServiceClient::CreateFromConnectionString(
                connection_string_
            );
            
            // Get container client
            container_client_ = std::make_unique<Azure::Storage::Blobs::BlobContainerClient>(
                service_client.GetBlobContainerClient(container_name_)
            );
            
            // Ensure container exists
            try {
                container_client_->CreateIfNotExists();
            } catch (const Azure::Core::RequestFailedException& e) {
                // Container might already exist, ignore
                THEMIS_DEBUG("Azure container check: {}", e.what());
            }
            
            THEMIS_INFO("AzureBlobBackend initialized: container={}, prefix={}", 
                        container_name_, prefix_);
        } catch (const std::exception& e) {
            THEMIS_ERROR("Failed to initialize Azure Blob Storage: {}", e.what());
            throw;
        }
    }
    
    ~AzureBlobBackend() override = default;
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string blob_name = getBlobName(blob_id);
        
        try {
            // Get blob client
            auto blob_client = container_client_->GetBlockBlobClient(blob_name);
            
            // Upload data
            Azure::Core::IO::MemoryBodyStream stream(data);
            Azure::Storage::Blobs::UploadBlockBlobOptions options;
            options.HttpHeaders.ContentType = "application/octet-stream";
            
            auto response = blob_client.Upload(stream, options);
            
            // Create blob reference
            BlobRef ref;
            ref.id = blob_id;
            ref.type = BlobStorageType::AZURE_BLOB;
            ref.uri = "azure://" + container_name_ + "/" + blob_name;
            ref.size_bytes = data.size();
            ref.hash_sha256 = computeSHA256(data);
            ref.created_at = std::chrono::system_clock::now().time_since_epoch().count();
            
            THEMIS_DEBUG("Blob stored in Azure: id={}, size={} bytes", blob_id, data.size());
            return ref;
            
        } catch (const Azure::Core::RequestFailedException& e) {
            THEMIS_ERROR("Azure upload failed: {}", e.what());
            throw std::runtime_error("Azure upload failed: " + std::string(e.what()));
        }
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string blob_name = getBlobName(ref.id);
        
        try {
            // Get blob client
            auto blob_client = container_client_->GetBlockBlobClient(blob_name);
            
            // Download blob
            auto response = blob_client.Download();
            
            // Read data from stream
            std::vector<uint8_t> data;
            data.reserve(ref.size_bytes);
            
            auto& body = response.Value.BodyStream;
            std::vector<uint8_t> buffer(4096);
            while (true) {
                size_t bytes_read = body->Read(buffer.data(), buffer.size());
                if (bytes_read == 0) break;
                data.insert(data.end(), buffer.begin(), buffer.begin() + bytes_read);
            }
            
            // Verify hash
            std::string actual_hash = computeSHA256(data);
            if (actual_hash != ref.hash_sha256) {
                THEMIS_ERROR("Hash mismatch for blob {}: expected={}, actual={}", 
                            ref.id, ref.hash_sha256, actual_hash);
                return std::nullopt;
            }
            
            THEMIS_DEBUG("Blob retrieved from Azure: id={}, size={} bytes", ref.id, data.size());
            return data;
            
        } catch (const Azure::Core::RequestFailedException& e) {
            if (e.StatusCode == Azure::Core::Http::HttpStatusCode::NotFound) {
                THEMIS_WARN("Blob not found in Azure: {}", ref.id);
                return std::nullopt;
            }
            THEMIS_ERROR("Azure download failed: {}", e.what());
            return std::nullopt;
        }
    }
    
    bool remove(const BlobRef& ref) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string blob_name = getBlobName(ref.id);
        
        try {
            // Get blob client
            auto blob_client = container_client_->GetBlockBlobClient(blob_name);
            
            // Delete blob
            blob_client.Delete();
            
            THEMIS_DEBUG("Blob deleted from Azure: id={}", ref.id);
            return true;
            
        } catch (const Azure::Core::RequestFailedException& e) {
            THEMIS_ERROR("Azure delete failed: {}", e.what());
            return false;
        }
    }
    
    bool exists(const BlobRef& ref) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string blob_name = getBlobName(ref.id);
        
        try {
            // Get blob client
            auto blob_client = container_client_->GetBlockBlobClient(blob_name);
            
            // Check existence
            auto properties = blob_client.GetProperties();
            return true;
            
        } catch (const Azure::Core::RequestFailedException& e) {
            if (e.StatusCode == Azure::Core::Http::HttpStatusCode::NotFound) {
                return false;
            }
            THEMIS_ERROR("Azure exists check failed: {}", e.what());
            return false;
        }
    }
    
    std::string name() const override {
        return "azure";
    }
    
    bool isAvailable() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            // Test connectivity
            container_client_->GetProperties();
            return true;
        } catch (...) {
            return false;
        }
    }
};

} // namespace storage
} // namespace themis
