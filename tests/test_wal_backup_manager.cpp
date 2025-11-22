#include <gtest/gtest.h>
#include <fstream>
#include "storage/backup_manager.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>
#include <memory>

using namespace themis;

TEST(WALBackupManager, CreateAndRestore) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "themis_test_db";
    fs::create_directories(tmp);
    
    // Create RocksDBWrapper for testing
    RocksDBWrapper::Config config;
    config.db_path = tmp.string();
    config.enable_wal = true;
    
    auto db_wrapper = std::make_shared<RocksDBWrapper>(config);
    ASSERT_TRUE(db_wrapper->open());
    
    // Insert some test data
    db_wrapper->put("test_key_1", std::vector<uint8_t>{'t','e','s','t','_','v','a','l','u','e','_','1'});
    db_wrapper->put("test_key_2", std::vector<uint8_t>{'t','e','s','t','_','v','a','l','u','e','_','2'});

    BackupManager mgr(db_wrapper);

    auto dest = fs::temp_directory_path() / "themis_backup";
    std::error_code ec;
    
    // Create full backup
    ASSERT_TRUE(mgr.createFullBackup(dest.string(), ec));
    ASSERT_FALSE(ec);
    
    // Verify backup was created
    auto backups = mgr.listBackups(dest.string());
    ASSERT_FALSE(backups.empty());
    ASSERT_TRUE(backups[0].starts_with("full_"));

    // Verify backup integrity
    auto backup_dir = dest / backups[0];
    ASSERT_TRUE(mgr.verifyBackup(backup_dir.string(), ec));
    ASSERT_FALSE(ec);

    // Archive WAL
    auto arch = dest / "wal_archive";
    ASSERT_TRUE(mgr.archiveWAL(arch.string(), ec));
    
    // Insert more data for incremental backup test
    db_wrapper->put("test_key_3", std::vector<uint8_t>{'t','e','s','t','_','v','a','l','u','e','_','3'});
    
    // Create incremental backup
    ASSERT_TRUE(mgr.createIncrementalBackup(dest.string(), ec));
    ASSERT_FALSE(ec);
    
    // Verify we now have 2 backups
    backups = mgr.listBackups(dest.string());
    ASSERT_EQ(backups.size(), 2);

    // Restore from first (full) backup
    ASSERT_TRUE(mgr.restoreFromBackup(backup_dir.string(), ec));
    ASSERT_FALSE(ec);
    
    // Verify data after restore
    auto val = db_wrapper->get("test_key_1");
    ASSERT_TRUE(val.has_value());
    std::string value(val->begin(), val->end());
    ASSERT_EQ(value, std::string("test_value_1"));

    // cleanup
    db_wrapper->close();
    fs::remove_all(tmp);
    fs::remove_all(dest);
}

