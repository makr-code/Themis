#include <gtest/gtest.h>
#include "storage/backup_manager.h"
#include <filesystem>

using namespace themis;

TEST(WALBackupManager, CreateAndRestore) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "themis_test_db";
    fs::create_directories(tmp);
    // create a dummy WAL file
    std::ofstream(tmp / "0001.wal") << "wal-data";

    BackupManager mgr(tmp.string());

    auto dest = fs::temp_directory_path() / "themis_backup";
    std::error_code ec;
    ASSERT_TRUE(mgr.createIncrementalBackup(dest.string(), ec));
    ASSERT_FALSE(ec);

    // Archive WAL
    auto arch = dest / "wal_archive";
    ASSERT_TRUE(mgr.archiveWAL(arch.string(), ec));
    ASSERT_TRUE(fs::exists(arch / "0001.wal"));

    // Restore -> copy back to new dir
    auto restore_dir = fs::temp_directory_path() / "themis_restore";
    fs::create_directories(restore_dir);
    ASSERT_TRUE(mgr.restoreFromBackup(arch.string(), ec));

    // cleanup
    fs::remove_all(tmp);
    fs::remove_all(dest);
    fs::remove_all(restore_dir);
}
