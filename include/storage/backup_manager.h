#pragma once

#include <string>
#include <system_error>

namespace themis {

/**
 * Simple BackupManager interface
 *
 * Responsibilities (scaffold):
 * - Trigger incremental/full backups
 * - Manage WAL archiving (rotate/upload)
 * - Restore from backup archive
 *
 * The current implementation is a lightweight scaffold with a local-FS
 * checkpoint + WAL copy behavior suitable for unit testing. A production
 * implementation should integrate with RocksDB checkpoints, S3/backends
 * and safe restore semantics.
 */
class BackupManager {
public:
    explicit BackupManager(const std::string& db_path);
    ~BackupManager();

    // Create an incremental backup snapshot into `dest_dir`.
    // Returns true on success, false otherwise and sets ec.
    bool createIncrementalBackup(const std::string& dest_dir, std::error_code& ec);

    // Archive WAL files to dest (placeholder)
    bool archiveWAL(const std::string& dest_dir, std::error_code& ec);

    // Restore DB from backup directory (placeholder)
    bool restoreFromBackup(const std::string& src_dir, std::error_code& ec);

private:
    std::string db_path_;
};

} // namespace themis
