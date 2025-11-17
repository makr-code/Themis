#pragma once

#include <string>
#include <system_error>
#include <memory>
#include <vector>

namespace themis {

// Forward declarations
class RocksDBWrapper;

/**
 * Production-ready BackupManager for incremental backups and WAL archiving
 *
 * Features:
 * - RocksDB Checkpoint API integration for consistent snapshots
 * - Incremental backups with sequence number tracking
 * - WAL (Write-Ahead Log) archiving for point-in-time recovery
 * - Backup manifest files with metadata
 * - Restore with integrity verification
 *
 * Backup Strategy:
 * 1. Full backup: RocksDB checkpoint + WAL files
 * 2. Incremental backup: WAL files since last backup
 * 3. Manifest files track backup chain and metadata
 *
 * Directory Structure:
 * backup_dir/
 *   ├── full_YYYYMMDD_HHMMSS/
 *   │   ├── checkpoint/       (RocksDB checkpoint data)
 *   │   ├── wal/              (WAL files at checkpoint time)
 *   │   └── MANIFEST.json     (backup metadata)
 *   ├── incr_YYYYMMDD_HHMMSS/
 *   │   ├── wal/              (incremental WAL files)
 *   │   └── MANIFEST.json     (incremental metadata)
 *   └── latest -> full_YYYYMMDD_HHMMSS/  (symlink to latest backup)
 */
class BackupManager {
public:
    /**
     * @param db_wrapper: Shared pointer to RocksDBWrapper for checkpoint operations
     */
    explicit BackupManager(std::shared_ptr<RocksDBWrapper> db_wrapper);
    ~BackupManager();

    /**
     * Create a full backup using RocksDB checkpoint
     * @param dest_dir: Base backup directory (will create timestamped subdirectory)
     * @param ec: Error code on failure
     * @return true on success, false otherwise
     */
    bool createFullBackup(const std::string& dest_dir, std::error_code& ec);

    /**
     * Create an incremental backup (WAL files since last backup)
     * @param dest_dir: Base backup directory
     * @param ec: Error code on failure
     * @return true on success, false otherwise
     */
    bool createIncrementalBackup(const std::string& dest_dir, std::error_code& ec);

    /**
     * Archive WAL files to destination directory
     * @param dest_dir: Destination for WAL files
     * @param ec: Error code on failure
     * @return true on success, false otherwise
     */
    bool archiveWAL(const std::string& dest_dir, std::error_code& ec);

    /**
     * Restore database from backup directory
     * @param src_dir: Source backup directory (full or incremental chain)
     * @param ec: Error code on failure
     * @return true on success, false otherwise
     */
    bool restoreFromBackup(const std::string& src_dir, std::error_code& ec);

    /**
     * List available backups in directory
     * @param backup_dir: Base backup directory
     * @return Vector of backup directory names sorted by timestamp
     */
    std::vector<std::string> listBackups(const std::string& backup_dir);

    /**
     * Verify backup integrity
     * @param backup_dir: Backup directory to verify
     * @param ec: Error code on failure
     * @return true if backup is valid, false otherwise
     */
    bool verifyBackup(const std::string& backup_dir, std::error_code& ec);

private:
    std::shared_ptr<RocksDBWrapper> db_wrapper_;
    
    // Helper: Get current timestamp string (YYYYMMDD_HHMMSS)
    std::string getTimestamp() const;
    
    // Helper: Create backup manifest file
    bool createManifest(const std::string& backup_dir, const std::string& type,
                        uint64_t sequence_number, std::error_code& ec);
    
    // Helper: Read backup manifest
    bool readManifest(const std::string& backup_dir, std::string& type,
                      uint64_t& sequence_number, std::error_code& ec);
    
    // Helper: Copy WAL files with sequence number filtering
    bool copyWALFiles(const std::string& src_dir, const std::string& dest_dir,
                      uint64_t min_sequence, std::error_code& ec);
    
    // Helper: Get current WAL sequence number from RocksDB
    uint64_t getCurrentSequenceNumber() const;
};

} // namespace themis
