#pragma once

#include "updates/release_manifest.h"
#include "storage/rocksdb_wrapper.h"
#include "acceleration/plugin_security.h"
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace themis {
namespace updates {

/**
 * @brief RocksDB-backed database for release manifests
 * 
 * Column Families:
 * - release_manifests: version -> ReleaseManifest (JSON)
 * - file_registry: path:version -> ReleaseFile (JSON)
 * - signature_cache: hash -> Signature verification result
 * - download_cache: version:file -> local path
 */
class ManifestDatabase {
public:
    /**
     * @brief Construct manifest database with storage backend
     * @param storage RocksDB wrapper
     * @param verifier Plugin security verifier for signature verification
     */
    ManifestDatabase(
        std::shared_ptr<RocksDBWrapper> storage,
        std::shared_ptr<acceleration::PluginSecurityVerifier> verifier
    );
    
    ~ManifestDatabase();
    
    /**
     * @brief Store manifest in database
     * @param manifest Release manifest to store
     * @return true if successful
     */
    bool storeManifest(const ReleaseManifest& manifest);
    
    /**
     * @brief Retrieve manifest by version
     * @param version Version string (e.g., "1.2.3")
     * @return Manifest if found
     */
    std::optional<ReleaseManifest> getManifest(const std::string& version);
    
    /**
     * @brief Get latest manifest (by version number)
     * @return Latest manifest if any exist
     */
    std::optional<ReleaseManifest> getLatestManifest();
    
    /**
     * @brief List all available versions
     * @return Vector of version strings, sorted ascending
     */
    std::vector<std::string> listVersions() const;
    
    /**
     * @brief Verify manifest integrity
     * @param manifest Manifest to verify
     * @return true if manifest is valid and signature checks out
     */
    bool verifyManifest(const ReleaseManifest& manifest);
    
    /**
     * @brief Check if file exists and is valid for a version
     * @param path File path
     * @param version Version string
     * @return true if file exists and hash matches
     */
    bool verifyFile(const std::string& path, const std::string& version);
    
    /**
     * @brief Get file from registry
     * @param path File path
     * @param version Version string
     * @return ReleaseFile if found
     */
    std::optional<ReleaseFile> getFile(
        const std::string& path,
        const std::string& version
    );
    
    /**
     * @brief Store file in registry
     * @param file ReleaseFile to store
     * @param version Version string
     * @return true if successful
     */
    bool storeFile(const ReleaseFile& file, const std::string& version);
    
    /**
     * @brief Cache signature verification result
     * @param hash SHA-256 hash
     * @param verified Verification result
     * @param certificate Certificate used for verification
     */
    void cacheSignatureVerification(
        const std::string& hash,
        bool verified,
        const std::string& certificate
    );
    
    /**
     * @brief Check if signature is cached
     * @param hash SHA-256 hash
     * @return Verification result if cached
     */
    std::optional<bool> getCachedSignatureVerification(const std::string& hash);
    
    /**
     * @brief Store download cache entry
     * @param version Version string
     * @param filename Filename
     * @param local_path Local file path
     */
    void cacheDownload(
        const std::string& version,
        const std::string& filename,
        const std::string& local_path
    );
    
    /**
     * @brief Get cached download path
     * @param version Version string
     * @param filename Filename
     * @return Local path if cached
     */
    std::optional<std::string> getCachedDownload(
        const std::string& version,
        const std::string& filename
    );
    
    /**
     * @brief Delete manifest and associated files
     * @param version Version to delete
     * @return true if successful
     */
    bool deleteManifest(const std::string& version);
    
private:
    std::shared_ptr<RocksDBWrapper> storage_;
    std::shared_ptr<acceleration::PluginSecurityVerifier> verifier_;
    
    // Column family handles
    rocksdb::ColumnFamilyHandle* cf_manifests_ = nullptr;
    rocksdb::ColumnFamilyHandle* cf_files_ = nullptr;
    rocksdb::ColumnFamilyHandle* cf_signatures_ = nullptr;
    rocksdb::ColumnFamilyHandle* cf_cache_ = nullptr;
    
    /**
     * @brief Initialize column families
     */
    void initializeColumnFamilies();
};

} // namespace updates
} // namespace themis
