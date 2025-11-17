# rocksdb_wrapper.cpp

Path: `src/storage/rocksdb_wrapper.cpp`

Purpose: Wrapper utilities for RocksDB TransactionDB, CF management, checkpointing and snapshot helpers.

Public functions / symbols:
- `if (this != &other) {`
- `if (config_.use_universal_compaction) {`
- `for (const auto& p : config_.db_paths) {`
- `if (ec) {`
- `if (db_) {`
- `for (const auto& key : keys) {`
- `: db_(db) {`
- `if (db_->db_) {`
- `if (active_ && txn_) {`
- `for (int level = 0; level < 7; ++level) {`
- `if (options_->statistics) {`
- `if (total_access > 0) {`
- `switch (ct) {`
- `if (!db_) {`
- `configureOptions();`
- `close();`
- `std::filesystem::path dbp(config_.db_path);`
- `THEMIS_ERROR("{}", msg);`
- `std::filesystem::path wald(config_.wal_dir);`
- `THEMIS_INFO("Closing RocksDB");`
- `THEMIS_DEBUG("MVCC Transaction started with snapshot");`
- `THEMIS_WARN("Transaction not committed or rolled back - auto-rolling back");`
- `rollback();`
- ``
- `THEMIS_DEBUG("MVCC Transaction rolled back");`
- `THEMIS_ERROR("createCheckpoint failed: DB is not open");`
- `fprintf(stderr, "%s
", "createCheckpoint failed: DB is not open");`
- `std::filesystem::path cpp(checkpoint_dir);`
- `std::unique_ptr<rocksdb::Checkpoint> cp(raw);`
- `THEMIS_INFO("Checkpoint created at '{}'", checkpoint_dir);`
- `THEMIS_ERROR("restoreFromCheckpoint: checkpoint dir '{}' does not exist", checkpoint_dir);`
- `THEMIS_ERROR("Failed to reopen DB after restore from '{}'", checkpoint_dir);`
- `THEMIS_INFO("Restored DB from checkpoint '{}' to '{}'", checkpoint_dir, target);`
- `THEMIS_ERROR("getOrCreateColumnFamily: DB not open");`

