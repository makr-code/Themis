#include "storage/blob_storage_backend.h"
#include "utils/logger.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <openssl/sha.h>

namespace themis {
namespace storage {

namespace fs = std::filesystem;

/**
 * @brief Filesystem Blob Storage Backend
 * 
 * Stores blobs in a hierarchical directory structure:
 * base_path/prefix/subdir/blob_id.blob
 * 
 * Example: ./data/blobs/a1/b2/a1b2c3d4e5f6....blob
 */
class FilesystemBlobBackend : public IBlobStorageBackend {
private:
    std::string base_path_;
    
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
    
    // Get hierarchical path for blob_id
    std::string getPath(const std::string& blob_id) const {
        if (blob_id.length() < 4) {
            throw std::runtime_error("Invalid blob_id: too short");
        }
        
        // Create hierarchical structure: blob_id[:2]/blob_id[2:4]/blob_id.blob
        std::string prefix = blob_id.substr(0, 2);
        std::string subdir = blob_id.substr(2, 2);
        
        fs::path dir_path = fs::path(base_path_) / prefix / subdir;
        return (dir_path / (blob_id + ".blob")).string();
    }
    
public:
    explicit FilesystemBlobBackend(const std::string& base_path)
        : base_path_(base_path) {
        // Create base directory if not exists
        try {
            fs::create_directories(base_path_);
            THEMIS_INFO("FilesystemBlobBackend initialized: path={}", base_path_);
        } catch (const std::exception& e) {
            THEMIS_ERROR("Failed to create blob storage directory: {}", e.what());
            throw;
        }
    }
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        std::string file_path = getPath(blob_id);
        
        try {
            // Create parent directories
            fs::create_directories(fs::path(file_path).parent_path());
            
            // Write blob to file
            std::ofstream ofs(file_path, std::ios::binary);
            if (!ofs) {
                throw std::runtime_error("Failed to open file for writing: " + file_path);
            }
            
            ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
            ofs.close();
            
            if (!ofs) {
                throw std::runtime_error("Failed to write blob to file: " + file_path);
            }
            
            // Create BlobRef
            BlobRef ref;
            ref.id = blob_id;
            ref.type = BlobStorageType::FILESYSTEM;
            ref.uri = file_path;
            ref.size_bytes = static_cast<int64_t>(data.size());
            ref.hash_sha256 = computeSHA256(data);
            ref.created_at = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            
            THEMIS_DEBUG("FilesystemBlobBackend: Stored blob {} ({} bytes) at {}", 
                blob_id, data.size(), file_path);
            
            return ref;
            
        } catch (const std::exception& e) {
            THEMIS_ERROR("FilesystemBlobBackend::put failed for {}: {}", blob_id, e.what());
            throw;
        }
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        try {
            std::ifstream ifs(ref.uri, std::ios::binary);
            if (!ifs) {
                THEMIS_WARN("FilesystemBlobBackend: Blob not found: {}", ref.uri);
                return std::nullopt;
            }
            
            // Read entire file
            std::vector<uint8_t> data(
                (std::istreambuf_iterator<char>(ifs)),
                std::istreambuf_iterator<char>()
            );
            
            THEMIS_DEBUG("FilesystemBlobBackend: Retrieved blob {} ({} bytes)", 
                ref.id, data.size());
            
            return data;
            
        } catch (const std::exception& e) {
            THEMIS_ERROR("FilesystemBlobBackend::get failed for {}: {}", ref.id, e.what());
            return std::nullopt;
        }
    }
    
    bool remove(const BlobRef& ref) override {
        try {
            if (fs::remove(ref.uri)) {
                THEMIS_DEBUG("FilesystemBlobBackend: Removed blob {}", ref.id);
                return true;
            }
            return false;
        } catch (const std::exception& e) {
            THEMIS_ERROR("FilesystemBlobBackend::remove failed for {}: {}", ref.id, e.what());
            return false;
        }
    }
    
    bool exists(const BlobRef& ref) override {
        return fs::exists(ref.uri);
    }
    
    std::string name() const override {
        return "filesystem";
    }
    
    bool isAvailable() const override {
        try {
            return fs::exists(base_path_) && fs::is_directory(base_path_);
        } catch (...) {
            return false;
        }
    }
};

} // namespace storage
} // namespace themis
