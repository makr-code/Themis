#pragma once

#include "updates/manifest_database.h"
#include "utils/update_checker.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace themis {
namespace updates {

/**
 * @brief Result of a download operation
 */
struct DownloadResult {
    bool success = false;
    std::string error_message;
    std::string download_path;
    ReleaseManifest manifest;
};

/**
 * @brief Result of a hot-reload operation
 */
struct ReloadResult {
    bool success = false;
    std::string error_message;
    std::vector<std::string> files_updated;
    std::string rollback_id;  // For rollback capability
};

/**
 * @brief Result of manifest verification
 */
struct VerificationResult {
    bool verified = false;
    std::string error_message;
    std::vector<std::string> warnings;
};

/**
 * @brief Engine for hot-reloading ThemisDB from release manifests
 * 
 * Features:
 * - Download and verify releases from GitHub
 * - Atomic file replacement
 * - Automatic backup before update
 * - Rollback capability on failure
 */
class HotReloadEngine {
public:
    /**
     * @brief Configuration for hot-reload engine
     */
    struct Config {
        std::string download_directory = "/tmp/themis_updates";
        std::string backup_directory = "/var/lib/themisdb/rollback";
        std::string install_directory = ".";  // Current directory by default
        bool verify_signatures = true;
        bool create_backup = true;
        bool dry_run = false;  // Don't actually apply changes
    };
    
    /**
     * @brief Construct hot-reload engine
     * @param manifest_db Manifest database
     * @param update_checker Update checker for fetching releases
     * @param config Configuration
     */
    HotReloadEngine(
        std::shared_ptr<ManifestDatabase> manifest_db,
        std::shared_ptr<utils::UpdateChecker> update_checker,
        const Config& config = Config()
    );
    
    ~HotReloadEngine();
    
    /**
     * @brief Download and verify a release
     * @param version Version to download (e.g., "1.2.3")
     * @return Download result
     */
    DownloadResult downloadRelease(const std::string& version);
    
    /**
     * @brief Apply hot-reload (atomic operation)
     * @param version Version to apply
     * @param verify_only Dry-run mode (don't actually apply)
     * @return Reload result
     */
    ReloadResult applyHotReload(
        const std::string& version,
        bool verify_only = false
    );
    
    /**
     * @brief Rollback to previous version
     * @param rollback_id Rollback ID from previous reload
     * @return true if successful
     */
    bool rollback(const std::string& rollback_id);
    
    /**
     * @brief Verify release before applying
     * @param manifest Manifest to verify
     * @return Verification result
     */
    VerificationResult verifyRelease(const ReleaseManifest& manifest);
    
    /**
     * @brief Check if upgrade is compatible
     * @param current_version Current version
     * @param target_version Target version
     * @return true if upgrade is compatible
     */
    bool isCompatibleUpgrade(
        const std::string& current_version,
        const std::string& target_version
    );
    
    /**
     * @brief List available rollback points
     * @return Vector of rollback IDs with timestamps
     */
    std::vector<std::pair<std::string, std::string>> listRollbackPoints() const;
    
    /**
     * @brief Clean old rollback points
     * @param keep_count Number of rollback points to keep
     */
    void cleanRollbackPoints(size_t keep_count = 3);
    
    /**
     * @brief Set progress callback for long operations
     * @param callback Callback function (progress percentage, message)
     */
    void setProgressCallback(
        std::function<void(int, const std::string&)> callback
    );
    
private:
    std::shared_ptr<ManifestDatabase> manifest_db_;
    std::shared_ptr<utils::UpdateChecker> update_checker_;
    Config config_;
    std::function<void(int, const std::string&)> progress_callback_;
    
    /**
     * @brief Download single file with resume support
     * @param file ReleaseFile to download
     * @param dest Destination path
     * @return true if successful
     */
    bool downloadFile(const ReleaseFile& file, const std::string& dest);
    
    /**
     * @brief Verify downloaded file
     * @param file ReleaseFile with expected hash
     * @param path Local file path
     * @return true if hash matches
     */
    bool verifyDownloadedFile(const ReleaseFile& file, const std::string& path);
    
    /**
     * @brief Create backup before update
     * @return Rollback ID for the backup
     */
    std::string createBackup(const std::vector<ReleaseFile>& files);
    
    /**
     * @brief Atomic file replacement
     * @param src Source file path
     * @param dst Destination file path
     * @return true if successful
     */
    bool atomicReplace(const std::string& src, const std::string& dst);
    
    /**
     * @brief Calculate SHA-256 hash of file
     * @param path File path
     * @return Hex-encoded hash
     */
    std::string calculateFileHash(const std::string& path);
    
    /**
     * @brief Generate unique rollback ID
     * @return UUID-based rollback ID
     */
    std::string generateRollbackId();
    
    /**
     * @brief Report progress
     * @param percentage Progress percentage (0-100)
     * @param message Progress message
     */
    void reportProgress(int percentage, const std::string& message);
};

} // namespace updates
} // namespace themis
