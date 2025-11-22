#include "updates/hot_reload_engine.h"
#include "utils/logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <openssl/sha.h>

#ifdef THEMIS_ENABLE_CURL
#include <curl/curl.h>
#endif

namespace themis {
namespace updates {

namespace fs = std::filesystem;

HotReloadEngine::HotReloadEngine(
    std::shared_ptr<ManifestDatabase> manifest_db,
    std::shared_ptr<utils::UpdateChecker> update_checker,
    const Config& config
)
    : manifest_db_(std::move(manifest_db))
    , update_checker_(std::move(update_checker))
    , config_(config) {
    
    // Create directories
    fs::create_directories(config_.download_directory);
    fs::create_directories(config_.backup_directory);
    
    LOG_INFO("HotReloadEngine initialized");
}

HotReloadEngine::~HotReloadEngine() = default;

DownloadResult HotReloadEngine::downloadRelease(const std::string& version) {
    DownloadResult result;
    result.success = false;
    
    reportProgress(0, "Fetching manifest for version " + version);
    
    // Get manifest from database first
    auto manifest = manifest_db_->getManifest(version);
    if (!manifest) {
        result.error_message = "Manifest not found for version: " + version;
        LOG_ERROR("{}", result.error_message);
        return result;
    }
    
    result.manifest = *manifest;
    
    // Verify manifest
    reportProgress(10, "Verifying manifest");
    if (config_.verify_signatures && !manifest_db_->verifyManifest(*manifest)) {
        result.error_message = "Manifest verification failed";
        LOG_ERROR("{}", result.error_message);
        return result;
    }
    
    // Create version-specific download directory
    std::string version_dir = config_.download_directory + "/" + version;
    fs::create_directories(version_dir);
    result.download_path = version_dir;
    
    // Download files
    int file_count = manifest->files.size();
    int current_file = 0;
    
    for (const auto& file : manifest->files) {
        current_file++;
        int progress = 10 + (current_file * 80 / file_count);
        reportProgress(progress, "Downloading " + file.path);
        
        std::string dest_path = version_dir + "/" + file.path;
        
        // Check cache first
        auto cached_path = manifest_db_->getCachedDownload(version, file.path);
        if (cached_path && fs::exists(*cached_path)) {
            // Verify cached file
            if (verifyDownloadedFile(file, *cached_path)) {
                LOG_DEBUG("Using cached file: {}", *cached_path);
                continue;
            }
        }
        
        // Create parent directories
        fs::create_directories(fs::path(dest_path).parent_path());
        
        // Download file
        if (!downloadFile(file, dest_path)) {
            result.error_message = "Failed to download file: " + file.path;
            LOG_ERROR("{}", result.error_message);
            return result;
        }
        
        // Verify downloaded file
        if (!verifyDownloadedFile(file, dest_path)) {
            result.error_message = "Verification failed for file: " + file.path;
            LOG_ERROR("{}", result.error_message);
            return result;
        }
        
        // Cache download
        manifest_db_->cacheDownload(version, file.path, dest_path);
    }
    
    reportProgress(100, "Download complete");
    result.success = true;
    return result;
}

ReloadResult HotReloadEngine::applyHotReload(
    const std::string& version,
    bool verify_only
) {
    ReloadResult result;
    result.success = false;
    
    reportProgress(0, "Starting hot-reload for version " + version);
    
    // Get manifest
    auto manifest = manifest_db_->getManifest(version);
    if (!manifest) {
        result.error_message = "Manifest not found";
        return result;
    }
    
    // Verify release
    reportProgress(10, "Verifying release");
    auto verify_result = verifyRelease(*manifest);
    if (!verify_result.verified) {
        result.error_message = verify_result.error_message;
        return result;
    }
    
    if (verify_only) {
        result.success = true;
        result.error_message = "Verification passed (dry-run mode)";
        return result;
    }
    
    // Check compatibility
    auto current_version = update_checker_->getConfig().current_version;
    reportProgress(20, "Checking compatibility");
    if (!isCompatibleUpgrade(current_version, version)) {
        result.error_message = "Incompatible upgrade from " + current_version + " to " + version;
        return result;
    }
    
    // Create backup
    std::string rollback_id;
    if (config_.create_backup) {
        reportProgress(30, "Creating backup");
        rollback_id = createBackup(manifest->files);
        result.rollback_id = rollback_id;
    }
    
    // Apply updates
    reportProgress(50, "Applying updates");
    int file_count = manifest->files.size();
    int current_file = 0;
    
    std::string version_dir = config_.download_directory + "/" + version;
    
    for (const auto& file : manifest->files) {
        current_file++;
        int progress = 50 + (current_file * 40 / file_count);
        reportProgress(progress, "Updating " + file.path);
        
        std::string src_path = version_dir + "/" + file.path;
        std::string dst_path = config_.install_directory + "/" + file.path;
        
        if (!fs::exists(src_path)) {
            result.error_message = "Source file not found: " + src_path;
            LOG_ERROR("{}", result.error_message);
            
            // Rollback if backup was created
            if (!rollback_id.empty()) {
                rollback(rollback_id);
            }
            return result;
        }
        
        // Atomic replace
        if (!atomicReplace(src_path, dst_path)) {
            result.error_message = "Failed to replace file: " + file.path;
            LOG_ERROR("{}", result.error_message);
            
            // Rollback
            if (!rollback_id.empty()) {
                rollback(rollback_id);
            }
            return result;
        }
        
        result.files_updated.push_back(file.path);
    }
    
    reportProgress(100, "Hot-reload complete");
    result.success = true;
    
    LOG_INFO("Hot-reload applied successfully: {} -> {}", current_version, version);
    return result;
}

bool HotReloadEngine::rollback(const std::string& rollback_id) {
    try {
        std::string backup_dir = config_.backup_directory + "/" + rollback_id;
        
        if (!fs::exists(backup_dir)) {
            LOG_ERROR("Rollback directory not found: {}", backup_dir);
            return false;
        }
        
        // Read rollback metadata
        std::string metadata_path = backup_dir + "/rollback.json";
        if (!fs::exists(metadata_path)) {
            LOG_ERROR("Rollback metadata not found");
            return false;
        }
        
        std::ifstream metadata_file(metadata_path);
        json metadata_json;
        metadata_file >> metadata_json;
        
        // Restore files
        for (const auto& file_json : metadata_json["files"]) {
            std::string file_path = file_json["path"];
            std::string backup_file = backup_dir + "/" + file_path;
            std::string dest_file = config_.install_directory + "/" + file_path;
            
            if (fs::exists(backup_file)) {
                fs::copy_file(backup_file, dest_file, fs::copy_options::overwrite_existing);
                LOG_DEBUG("Restored: {}", file_path);
            }
        }
        
        LOG_INFO("Rollback completed: {}", rollback_id);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Rollback failed: {}", e.what());
        return false;
    }
}

VerificationResult HotReloadEngine::verifyRelease(const ReleaseManifest& manifest) {
    VerificationResult result;
    result.verified = false;
    
    // Verify manifest
    if (!manifest_db_->verifyManifest(manifest)) {
        result.error_message = "Manifest verification failed";
        return result;
    }
    
    // Check if files are downloaded
    std::string version_dir = config_.download_directory + "/" + manifest.version;
    for (const auto& file : manifest.files) {
        std::string file_path = version_dir + "/" + file.path;
        if (!fs::exists(file_path)) {
            result.warnings.push_back("File not downloaded: " + file.path);
        } else {
            // Verify hash
            if (!verifyDownloadedFile(file, file_path)) {
                result.error_message = "Hash mismatch for file: " + file.path;
                return result;
            }
        }
    }
    
    result.verified = true;
    return result;
}

bool HotReloadEngine::isCompatibleUpgrade(
    const std::string& current_version,
    const std::string& target_version
) {
    // Parse versions
    auto current = utils::Version::parse(current_version);
    auto target = utils::Version::parse(target_version);
    
    if (!current || !target) {
        return false;
    }
    
    // Can't downgrade
    if (*target < *current) {
        LOG_WARN("Cannot downgrade from {} to {}", current_version, target_version);
        return false;
    }
    
    // Check manifest's minimum upgrade version
    auto manifest = manifest_db_->getManifest(target_version);
    if (manifest && !manifest->min_upgrade_from.empty()) {
        auto min_version = utils::Version::parse(manifest->min_upgrade_from);
        if (min_version && *current < *min_version) {
            LOG_WARN("Current version {} is below minimum required {}", 
                current_version, manifest->min_upgrade_from);
            return false;
        }
    }
    
    return true;
}

std::vector<std::pair<std::string, std::string>> HotReloadEngine::listRollbackPoints() const {
    std::vector<std::pair<std::string, std::string>> rollback_points;
    
    try {
        for (const auto& entry : fs::directory_iterator(config_.backup_directory)) {
            if (entry.is_directory()) {
                std::string rollback_id = entry.path().filename().string();
                std::string metadata_path = entry.path().string() + "/rollback.json";
                
                if (fs::exists(metadata_path)) {
                    std::ifstream metadata_file(metadata_path);
                    json metadata_json;
                    metadata_file >> metadata_json;
                    
                    std::string timestamp = metadata_json.value("timestamp", "unknown");
                    rollback_points.emplace_back(rollback_id, timestamp);
                }
            }
        }
        
        // Sort by timestamp (newest first)
        std::sort(rollback_points.begin(), rollback_points.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to list rollback points: {}", e.what());
    }
    
    return rollback_points;
}

void HotReloadEngine::cleanRollbackPoints(size_t keep_count) {
    auto rollback_points = listRollbackPoints();
    
    if (rollback_points.size() <= keep_count) {
        return;
    }
    
    // Delete old rollback points
    for (size_t i = keep_count; i < rollback_points.size(); i++) {
        std::string rollback_dir = config_.backup_directory + "/" + rollback_points[i].first;
        try {
            fs::remove_all(rollback_dir);
            LOG_INFO("Cleaned old rollback point: {}", rollback_points[i].first);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to clean rollback point {}: {}", rollback_points[i].first, e.what());
        }
    }
}

void HotReloadEngine::setProgressCallback(
    std::function<void(int, const std::string&)> callback
) {
    progress_callback_ = std::move(callback);
}

#ifdef THEMIS_ENABLE_CURL
static size_t writeFileCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}
#endif

bool HotReloadEngine::downloadFile(const ReleaseFile& file, const std::string& dest) {
#ifdef THEMIS_ENABLE_CURL
    if (file.download_url.empty()) {
        LOG_ERROR("No download URL for file: {}", file.path);
        return false;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    FILE* fp = fopen(dest.c_str(), "wb");
    if (!fp) {
        LOG_ERROR("Failed to open file for writing: {}", dest);
        curl_easy_cleanup(curl);
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, file.download_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);  // 5 minutes
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ThemisDB-HotReload/1.0");
    
    CURLcode res = curl_easy_perform(curl);
    
    fclose(fp);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("CURL error downloading {}: {}", file.path, curl_easy_strerror(res));
        fs::remove(dest);  // Clean up partial download
        return false;
    }
    
    return true;
#else
    LOG_ERROR("CURL support not enabled - cannot download files");
    return false;
#endif
}

bool HotReloadEngine::verifyDownloadedFile(const ReleaseFile& file, const std::string& path) {
    std::string actual_hash = calculateFileHash(path);
    
    if (actual_hash != file.sha256_hash) {
        LOG_ERROR("Hash mismatch for {}: expected {}, got {}",
            file.path, file.sha256_hash, actual_hash);
        return false;
    }
    
    // Verify size
    auto file_size = fs::file_size(path);
    if (file_size != file.size_bytes) {
        LOG_ERROR("Size mismatch for {}: expected {}, got {}",
            file.path, file.size_bytes, file_size);
        return false;
    }
    
    return true;
}

std::string HotReloadEngine::createBackup(const std::vector<ReleaseFile>& files) {
    std::string rollback_id = generateRollbackId();
    std::string backup_dir = config_.backup_directory + "/" + rollback_id;
    
    try {
        fs::create_directories(backup_dir);
        
        // Backup files
        for (const auto& file : files) {
            std::string src = config_.install_directory + "/" + file.path;
            std::string dst = backup_dir + "/" + file.path;
            
            if (fs::exists(src)) {
                fs::create_directories(fs::path(dst).parent_path());
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
            }
        }
        
        // Save metadata
        json metadata;
        metadata["rollback_id"] = rollback_id;
        metadata["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        metadata["files"] = json::array();
        for (const auto& file : files) {
            metadata["files"].push_back({{"path", file.path}});
        }
        
        std::ofstream metadata_file(backup_dir + "/rollback.json");
        metadata_file << metadata.dump(2);
        
        LOG_INFO("Backup created: {}", rollback_id);
        return rollback_id;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create backup: {}", e.what());
        return "";
    }
}

bool HotReloadEngine::atomicReplace(const std::string& src, const std::string& dst) {
    try {
        // Create temp file
        std::string temp = dst + ".tmp";
        
        // Copy to temp
        fs::copy_file(src, temp, fs::copy_options::overwrite_existing);
        
        // Atomic rename
        fs::rename(temp, dst);
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Atomic replace failed for {}: {}", dst, e.what());
        return false;
    }
}

std::string HotReloadEngine::calculateFileHash(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    const size_t bufferSize = 32768;
    std::vector<char> buffer(bufferSize);
    
    while (file.read(buffer.data(), bufferSize) || file.gcount() > 0) {
        SHA256_Update(&sha256, buffer.data(), file.gcount());
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);
    
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    
    return ss.str();
}

std::string HotReloadEngine::generateRollbackId() {
    // Simple timestamp-based ID
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    return "rollback_" + std::to_string(timestamp);
}

void HotReloadEngine::reportProgress(int percentage, const std::string& message) {
    LOG_DEBUG("Progress: {}% - {}", percentage, message);
    
    if (progress_callback_) {
        progress_callback_(percentage, message);
    }
}

} // namespace updates
} // namespace themis
