#include "storage/backup_manager.h"
#include "storage/rocksdb_wrapper.h"
#include "core/logging.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace themis {

BackupManager::BackupManager(std::shared_ptr<RocksDBWrapper> db_wrapper) 
    : db_wrapper_(std::move(db_wrapper)) {
    if (!db_wrapper_) {
        THEMIS_ERROR("BackupManager: db_wrapper is null");
        throw std::invalid_argument("db_wrapper cannot be null");
    }
}

BackupManager::~BackupManager() = default;

std::string BackupManager::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
    return ss.str();
}

bool BackupManager::createManifest(const std::string& backup_dir, const std::string& type,
                                   uint64_t sequence_number, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        nlohmann::json manifest;
        manifest["type"] = type;
        manifest["timestamp"] = getTimestamp();
        manifest["sequence_number"] = sequence_number;
        manifest["db_path"] = db_wrapper_->getConfig().db_path;
        
        auto manifest_path = fs::path(backup_dir) / "MANIFEST.json";
        std::ofstream out(manifest_path);
        if (!out) {
            ec = std::make_error_code(std::errc::io_error);
            THEMIS_ERROR("Failed to create manifest file: {}", manifest_path.string());
            return false;
        }
        out << manifest.dump(2);
        out.close();
        
        THEMIS_INFO("Created backup manifest: type={}, seq={}, path={}", 
                    type, sequence_number, manifest_path.string());
        return true;
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception creating manifest: {}", e.what());
        return false;
    }
}

bool BackupManager::readManifest(const std::string& backup_dir, std::string& type,
                                 uint64_t& sequence_number, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        auto manifest_path = fs::path(backup_dir) / "MANIFEST.json";
        if (!fs::exists(manifest_path)) {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            THEMIS_ERROR("Manifest not found: {}", manifest_path.string());
            return false;
        }
        
        std::ifstream in(manifest_path);
        if (!in) {
            ec = std::make_error_code(std::errc::io_error);
            THEMIS_ERROR("Failed to read manifest: {}", manifest_path.string());
            return false;
        }
        
        nlohmann::json manifest;
        in >> manifest;
        
        type = manifest.value("type", "unknown");
        sequence_number = manifest.value("sequence_number", 0ULL);
        
        THEMIS_INFO("Read backup manifest: type={}, seq={}", type, sequence_number);
        return true;
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception reading manifest: {}", e.what());
        return false;
    }
}

uint64_t BackupManager::getCurrentSequenceNumber() const {
    // RocksDB exposes sequence number via GetLatestSequenceNumber()
    // For now, we use a simplified approach with timestamp
    // In production, integrate with RocksDB's sequence number API
    return static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
}

bool BackupManager::copyWALFiles(const std::string& src_dir, const std::string& dest_dir,
                                 uint64_t min_sequence, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        fs::create_directories(dest_dir, ec);
        if (ec) {
            THEMIS_ERROR("Failed to create WAL dest directory: {}", ec.message());
            return false;
        }
        
        int count = 0;
        for (const auto& entry : fs::directory_iterator(src_dir)) {
            auto path = entry.path();
            auto ext = path.extension().string();
            
            // Copy .log files (RocksDB WAL files) and .sst files (SST files)
            if (ext == ".log" || ext == ".sst") {
                auto dest_path = fs::path(dest_dir) / path.filename();
                fs::copy_file(path, dest_path, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    THEMIS_ERROR("Failed to copy WAL file {}: {}", path.string(), ec.message());
                    return false;
                }
                count++;
            }
        }
        
        THEMIS_INFO("Copied {} WAL files from {} to {}", count, src_dir, dest_dir);
        return true;
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception copying WAL files: {}", e.what());
        return false;
    }
}

bool BackupManager::createFullBackup(const std::string& dest_dir, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        // Create timestamped backup directory
        auto timestamp = getTimestamp();
        auto backup_dir = fs::path(dest_dir) / ("full_" + timestamp);
        
        THEMIS_INFO("Creating full backup to {}", backup_dir.string());
        
        fs::create_directories(backup_dir, ec);
        if (ec) {
            THEMIS_ERROR("Failed to create backup directory: {}", ec.message());
            return false;
        }
        
        // Create RocksDB checkpoint
        auto checkpoint_dir = backup_dir / "checkpoint";
        if (!db_wrapper_->createCheckpoint(checkpoint_dir.string())) {
            ec = std::make_error_code(std::errc::io_error);
            THEMIS_ERROR("Failed to create RocksDB checkpoint");
            return false;
        }
        
        // Archive current WAL files
        auto wal_dir = backup_dir / "wal";
        auto db_path = db_wrapper_->getConfig().db_path;
        if (!copyWALFiles(db_path, wal_dir.string(), 0, ec)) {
            THEMIS_ERROR("Failed to copy WAL files");
            return false;
        }
        
        // Create manifest
        uint64_t seq = getCurrentSequenceNumber();
        if (!createManifest(backup_dir.string(), "full", seq, ec)) {
            THEMIS_ERROR("Failed to create backup manifest");
            return false;
        }
        
        // Update 'latest' symlink
        auto latest_link = fs::path(dest_dir) / "latest";
        if (fs::exists(latest_link)) {
            fs::remove(latest_link, ec);
        }
        fs::create_symlink(backup_dir.filename(), latest_link, ec);
        if (ec) {
            THEMIS_WARN("Failed to create 'latest' symlink: {}", ec.message());
            ec.clear(); // Non-critical
        }
        
        THEMIS_INFO("Full backup created successfully: {}", backup_dir.string());
        return true;
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception creating full backup: {}", e.what());
        return false;
    }
}

bool BackupManager::createIncrementalBackup(const std::string& dest_dir, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        // Find last backup to determine min sequence number
        auto backups = listBackups(dest_dir);
        uint64_t min_sequence = 0;
        
        if (!backups.empty()) {
            auto last_backup_dir = fs::path(dest_dir) / backups.back();
            std::string type;
            if (!readManifest(last_backup_dir.string(), type, min_sequence, ec)) {
                THEMIS_WARN("Could not read last backup manifest, creating full backup instead");
                return createFullBackup(dest_dir, ec);
            }
        } else {
            THEMIS_INFO("No previous backups found, creating full backup");
            return createFullBackup(dest_dir, ec);
        }
        
        // Create timestamped incremental backup directory
        auto timestamp = getTimestamp();
        auto backup_dir = fs::path(dest_dir) / ("incr_" + timestamp);
        
        THEMIS_INFO("Creating incremental backup to {} (seq >= {})", 
                    backup_dir.string(), min_sequence);
        
        fs::create_directories(backup_dir, ec);
        if (ec) {
            THEMIS_ERROR("Failed to create incremental backup directory: {}", ec.message());
            return false;
        }
        
        // Copy WAL files since last backup
        auto wal_dir = backup_dir / "wal";
        auto db_path = db_wrapper_->getConfig().db_path;
        if (!copyWALFiles(db_path, wal_dir.string(), min_sequence, ec)) {
            THEMIS_ERROR("Failed to copy incremental WAL files");
            return false;
        }
        
        // Create manifest
        uint64_t seq = getCurrentSequenceNumber();
        if (!createManifest(backup_dir.string(), "incremental", seq, ec)) {
            THEMIS_ERROR("Failed to create incremental backup manifest");
            return false;
        }
        
        THEMIS_INFO("Incremental backup created successfully: {}", backup_dir.string());
        return true;
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception creating incremental backup: {}", e.what());
        return false;
    }
}

bool BackupManager::archiveWAL(const std::string& dest_dir, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        fs::create_directories(dest_dir, ec);
        if (ec) {
            THEMIS_ERROR("Failed to create WAL archive directory: {}", ec.message());
            return false;
        }
        
        auto db_path = db_wrapper_->getConfig().db_path;
        return copyWALFiles(db_path, dest_dir, 0, ec);
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception archiving WAL: {}", e.what());
        return false;
    }
}

bool BackupManager::restoreFromBackup(const std::string& src_dir, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        THEMIS_INFO("Restoring database from backup: {}", src_dir);
        
        // Read backup manifest
        std::string type;
        uint64_t sequence_number;
        if (!readManifest(src_dir, type, sequence_number, ec)) {
            THEMIS_ERROR("Failed to read backup manifest");
            return false;
        }
        
        if (type != "full") {
            THEMIS_ERROR("Can only restore from full backups (got type={})", type);
            ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        
        // Verify backup integrity
        if (!verifyBackup(src_dir, ec)) {
            THEMIS_ERROR("Backup integrity verification failed");
            return false;
        }
        
        // Restore from checkpoint
        auto checkpoint_dir = fs::path(src_dir) / "checkpoint";
        if (!fs::exists(checkpoint_dir)) {
            THEMIS_ERROR("Checkpoint directory not found: {}", checkpoint_dir.string());
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return false;
        }
        
        if (!db_wrapper_->restoreFromCheckpoint(checkpoint_dir.string())) {
            THEMIS_ERROR("Failed to restore from checkpoint");
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        
        THEMIS_INFO("Database restored successfully from {}", src_dir);
        return true;
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception restoring from backup: {}", e.what());
        return false;
    }
}

std::vector<std::string> BackupManager::listBackups(const std::string& backup_dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> backups;
    
    try {
        if (!fs::exists(backup_dir)) {
            return backups;
        }
        
        for (const auto& entry : fs::directory_iterator(backup_dir)) {
            if (entry.is_directory()) {
                auto name = entry.path().filename().string();
                if (name.starts_with("full_") || name.starts_with("incr_")) {
                    backups.push_back(name);
                }
            }
        }
        
        // Sort by timestamp (filename format ensures correct sort order)
        std::sort(backups.begin(), backups.end());
        
        THEMIS_INFO("Found {} backups in {}", backups.size(), backup_dir);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Exception listing backups: {}", e.what());
    }
    
    return backups;
}

bool BackupManager::verifyBackup(const std::string& backup_dir, std::error_code& ec) {
    namespace fs = std::filesystem;
    try {
        // Verify manifest exists
        auto manifest_path = fs::path(backup_dir) / "MANIFEST.json";
        if (!fs::exists(manifest_path)) {
            THEMIS_ERROR("Backup manifest missing: {}", manifest_path.string());
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return false;
        }
        
        // Read manifest
        std::string type;
        uint64_t seq;
        if (!readManifest(backup_dir, type, seq, ec)) {
            return false;
        }
        
        // Verify checkpoint directory exists for full backups
        if (type == "full") {
            auto checkpoint_dir = fs::path(backup_dir) / "checkpoint";
            if (!fs::exists(checkpoint_dir)) {
                THEMIS_ERROR("Checkpoint directory missing: {}", checkpoint_dir.string());
                ec = std::make_error_code(std::errc::no_such_file_or_directory);
                return false;
            }
            
            // Verify checkpoint has RocksDB files
            bool has_files = false;
            for (const auto& entry : fs::directory_iterator(checkpoint_dir)) {
                if (entry.is_regular_file()) {
                    has_files = true;
                    break;
                }
            }
            if (!has_files) {
                THEMIS_ERROR("Checkpoint directory is empty: {}", checkpoint_dir.string());
                ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }
        }
        
        // Verify WAL directory exists
        auto wal_dir = fs::path(backup_dir) / "wal";
        if (!fs::exists(wal_dir)) {
            THEMIS_WARN("WAL directory missing (non-critical): {}", wal_dir.string());
        }
        
        THEMIS_INFO("Backup verification passed: {}", backup_dir);
        return true;
    } catch (const std::exception& e) {
        ec = std::make_error_code(std::errc::io_error);
        THEMIS_ERROR("Exception verifying backup: {}", e.what());
        return false;
    }
}

} // namespace themis
