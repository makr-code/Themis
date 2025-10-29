#include "storage/rocksdb_wrapper.h"
#include "utils/logger.h"
#include <rocksdb/db.h>
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/iterator.h>
#include <rocksdb/table.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/advanced_options.h>
#include <rocksdb/statistics.h>
#include <rocksdb/utilities/checkpoint.h>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace themis {

RocksDBWrapper::RocksDBWrapper(const Config& config) : config_(config) {
    options_ = std::make_unique<rocksdb::Options>();
    txn_db_options_ = std::make_unique<rocksdb::TransactionDBOptions>();
    txn_options_ = std::make_unique<rocksdb::TransactionOptions>();
    read_options_ = std::make_unique<rocksdb::ReadOptions>();
    write_options_ = std::make_unique<rocksdb::WriteOptions>();
    configureOptions();
}

RocksDBWrapper::~RocksDBWrapper() {
    close();
}

RocksDBWrapper::RocksDBWrapper(RocksDBWrapper&& other) noexcept
    : config_(std::move(other.config_))
    , db_(std::move(other.db_))
    , options_(std::move(other.options_))
    , txn_db_options_(std::move(other.txn_db_options_))
    , txn_options_(std::move(other.txn_options_))
    , read_options_(std::move(other.read_options_))
    , write_options_(std::move(other.write_options_)) {}

RocksDBWrapper& RocksDBWrapper::operator=(RocksDBWrapper&& other) noexcept {
    if (this != &other) {
        close();
        config_ = std::move(other.config_);
        db_ = std::move(other.db_);
        options_ = std::move(other.options_);
        txn_db_options_ = std::move(other.txn_db_options_);
        txn_options_ = std::move(other.txn_options_);
        read_options_ = std::move(other.read_options_);
        write_options_ = std::move(other.write_options_);
    }
    return *this;
}

void RocksDBWrapper::configureOptions() {
    // Create DB if missing
    options_->create_if_missing = true;
    
    // Enable statistics for monitoring
    options_->statistics = rocksdb::CreateDBStatistics();
    options_->statistics->set_stats_level(rocksdb::kExceptHistogramOrTimers);
    
    // Memtable (write buffer) configuration
    options_->write_buffer_size = config_.memtable_size_mb * 1024 * 1024;
    options_->max_write_buffer_number = config_.max_write_buffer_number;
    options_->min_write_buffer_number_to_merge = config_.min_write_buffer_number_to_merge;
    
    // Block cache (read cache) configuration
    rocksdb::BlockBasedTableOptions table_options;
    table_options.block_cache = rocksdb::NewLRUCache(
        config_.block_cache_size_mb * 1024 * 1024, // capacity
        -1,                                         // num_shard_bits (auto)
        false,                                      // strict_capacity_limit
        config_.high_pri_pool_ratio                 // high_pri_pool_ratio
    );
    table_options.cache_index_and_filter_blocks = config_.cache_index_and_filter_blocks;
    table_options.pin_l0_filter_and_index_blocks_in_cache = config_.pin_l0_filter_and_index_blocks_in_cache;
    table_options.partition_filters = config_.partition_filters;
    
    // Bloom filter for faster point lookups
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(config_.bloom_bits_per_key, false));
    options_->table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));
    
    // Compaction
    options_->max_background_jobs = config_.max_background_jobs;
    if (config_.use_universal_compaction) {
        options_->compaction_style = rocksdb::kCompactionStyleUniversal;
    } else {
        options_->compaction_style = rocksdb::kCompactionStyleLevel;
    }
    options_->level_compaction_dynamic_level_bytes = config_.dynamic_level_bytes;
    options_->target_file_size_base = config_.target_file_size_base_mb * 1024ull * 1024ull;
    options_->max_bytes_for_level_base = config_.max_bytes_for_level_base_mb * 1024ull * 1024ull;

    // Compression preferences (best-effort; depends on build of RocksDB)
    auto toCompression = [](const std::string& s) {
        std::string v = s;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c){ return std::tolower(c); });
        if (v == "none") return rocksdb::kNoCompression;
        if (v == "lz4") return rocksdb::kLZ4Compression;
        if (v == "lz4hc") return rocksdb::kLZ4HCCompression;
        if (v == "zstd") return rocksdb::kZSTD;
        if (v == "snappy") return rocksdb::kSnappyCompression;
        if (v == "zlib") return rocksdb::kZlibCompression;
        if (v == "bzip2" || v == "bz2") return rocksdb::kBZip2Compression;
        return rocksdb::kNoCompression;
    };
    options_->compression = toCompression(config_.compression_default);
    options_->bottommost_compression = toCompression(config_.compression_bottommost);
    
    // WAL
    write_options_->sync = config_.enable_wal;
    if (!config_.wal_dir.empty()) {
        options_->wal_dir = config_.wal_dir;
    }

    // Place SSTables across multiple paths if configured
    if (!config_.db_paths.empty()) {
        std::vector<rocksdb::DbPath> paths;
        paths.reserve(config_.db_paths.size());
        for (const auto& p : config_.db_paths) {
            paths.emplace_back(p.path, static_cast<int64_t>(p.target_size_bytes));
        }
        options_->db_paths = std::move(paths);
    }

    // Direct I/O (can reduce OS cache thrashing when RocksDB cache is large)
    options_->use_direct_reads = config_.use_direct_reads;
    options_->use_direct_io_for_flush_and_compaction = config_.use_direct_io_for_flush_and_compaction;
    
    // MVCC Transaction Configuration
    txn_db_options_->transaction_lock_timeout = 1000; // 1 second timeout
    txn_db_options_->default_lock_timeout = 1000;
    
    // Set transaction options for optimistic concurrency control
    txn_options_->set_snapshot = true; // Automatically create snapshot on begin
    
    // TODO: Configure BlobDB when enable_blobdb is true
}

bool RocksDBWrapper::open() {
    // Ensure target directories exist when using relative paths and tests run from build dir
    try {
        std::error_code ec;
        std::filesystem::path dbp(config_.db_path);
        auto parent = dbp.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                auto msg = std::string("Failed to create DB parent directory '") + parent.string() + "': " + ec.message();
                THEMIS_ERROR("{}", msg);
                fprintf(stderr, "%s\n", msg.c_str());
                return false;
            }
        }
        // Also ensure the DB directory exists to avoid RocksDB creating it in odd environments
        ec.clear();
        std::filesystem::create_directories(dbp, ec);
        if (ec) {
            auto msg = std::string("Failed to create DB directory '") + dbp.string() + "': " + ec.message();
            THEMIS_ERROR("{}", msg);
            fprintf(stderr, "%s\n", msg.c_str());
            return false;
        }
        if (!config_.wal_dir.empty()) {
            std::filesystem::path wald(config_.wal_dir);
            auto wparent = wald;
            if (wald.has_filename()) wparent = wald.parent_path();
            if (!wparent.empty()) {
                ec.clear();
                std::filesystem::create_directories(wparent, ec);
                if (ec) {
                    auto msg = std::string("Failed to create WAL parent directory '") + wparent.string() + "': " + ec.message();
                    THEMIS_ERROR("{}", msg);
                    fprintf(stderr, "%s\n", msg.c_str());
                    return false;
                }
            }
        }
    } catch (const std::exception& e) {
        auto msg = std::string("Exception while ensuring DB directories: ") + e.what();
        THEMIS_ERROR("{}", msg);
        fprintf(stderr, "%s\n", msg.c_str());
        return false;
    }

    rocksdb::TransactionDB* txn_db_ptr = nullptr;
    rocksdb::Status status = rocksdb::TransactionDB::Open(
        *options_, 
        *txn_db_options_,
        config_.db_path, 
        &txn_db_ptr
    );
    
    if (!status.ok()) {
        // Ensure error is visible even if logger wasn't initialized yet
        auto msg = std::string("Failed to open RocksDB TransactionDB: ") + status.ToString();
        THEMIS_ERROR("{}", msg);
        fprintf(stderr, "%s\n", msg.c_str());
        return false;
    }
    
    db_.reset(txn_db_ptr);
    THEMIS_INFO("Opened RocksDB TransactionDB at: {} (MVCC enabled)", config_.db_path);
    return true;
}

void RocksDBWrapper::close() {
    if (db_) {
        THEMIS_INFO("Closing RocksDB");
        db_.reset();
    }
}

bool RocksDBWrapper::isOpen() const {
    return db_ != nullptr;
}

std::optional<std::vector<uint8_t>> RocksDBWrapper::get(std::string_view key) {
    if (!db_) return std::nullopt;
    
    std::string value;
    rocksdb::Status status = db_->Get(*read_options_, rocksdb::Slice(key.data(), key.size()), &value);
    
    if (status.ok()) {
        return std::vector<uint8_t>(value.begin(), value.end());
    }
    
    return std::nullopt;
}

bool RocksDBWrapper::put(std::string_view key, const std::vector<uint8_t>& value) {
    if (!db_) return false;
    
    rocksdb::Status status = db_->Put(
        *write_options_,
        rocksdb::Slice(key.data(), key.size()),
        rocksdb::Slice(reinterpret_cast<const char*>(value.data()), value.size())
    );
    
    return status.ok();
}

bool RocksDBWrapper::del(std::string_view key) {
    if (!db_) return false;
    
    rocksdb::Status status = db_->Delete(*write_options_, rocksdb::Slice(key.data(), key.size()));
    return status.ok();
}

std::vector<std::optional<std::vector<uint8_t>>> RocksDBWrapper::multiGet(
    const std::vector<std::string>& keys
) {
    std::vector<std::optional<std::vector<uint8_t>>> results;
    if (!db_) return results;
    
    // TODO: Use RocksDB MultiGet for batch efficiency
    for (const auto& key : keys) {
        results.push_back(get(key));
    }
    
    return results;
}

// WriteBatchWrapper implementation

RocksDBWrapper::WriteBatchWrapper::WriteBatchWrapper(RocksDBWrapper* db)
    : db_(db), batch_(std::make_unique<rocksdb::WriteBatch>()) {}

RocksDBWrapper::WriteBatchWrapper::~WriteBatchWrapper() = default;

void RocksDBWrapper::WriteBatchWrapper::put(std::string_view key, const std::vector<uint8_t>& value) {
    batch_->Put(
        rocksdb::Slice(key.data(), key.size()),
        rocksdb::Slice(reinterpret_cast<const char*>(value.data()), value.size())
    );
}

void RocksDBWrapper::WriteBatchWrapper::del(std::string_view key) {
    batch_->Delete(rocksdb::Slice(key.data(), key.size()));
}

bool RocksDBWrapper::WriteBatchWrapper::commit() {
    return db_->commitBatch(batch_.get());
}

void RocksDBWrapper::WriteBatchWrapper::rollback() {
    batch_->Clear();
}

std::unique_ptr<RocksDBWrapper::WriteBatchWrapper> RocksDBWrapper::createWriteBatch() {
    return std::make_unique<WriteBatchWrapper>(this);
}

// TransactionWrapper implementation (MVCC)

RocksDBWrapper::TransactionWrapper::TransactionWrapper(RocksDBWrapper* db)
    : db_(db) {
    if (db_->db_) {
        txn_.reset(db_->db_->BeginTransaction(*db_->write_options_, *db_->txn_options_));
        THEMIS_DEBUG("MVCC Transaction started with snapshot");
    }
}

RocksDBWrapper::TransactionWrapper::~TransactionWrapper() {
    if (active_ && txn_) {
        THEMIS_WARN("Transaction not committed or rolled back - auto-rolling back");
        rollback();
    }
}

std::optional<std::vector<uint8_t>> RocksDBWrapper::TransactionWrapper::get(std::string_view key) {
    if (!txn_) return std::nullopt;
    
    std::string value;
    rocksdb::ReadOptions read_opts;
    read_opts.snapshot = txn_->GetSnapshot();
    
    rocksdb::Status status = txn_->Get(read_opts, rocksdb::Slice(key.data(), key.size()), &value);
    
    if (status.ok()) {
        return std::vector<uint8_t>(value.begin(), value.end());
    }
    
    return std::nullopt;
}

bool RocksDBWrapper::TransactionWrapper::put(std::string_view key, const std::vector<uint8_t>& value) {
    if (!txn_ || !active_) return false;
    
    rocksdb::Status status = txn_->Put(
        rocksdb::Slice(key.data(), key.size()),
        rocksdb::Slice(reinterpret_cast<const char*>(value.data()), value.size())
    );
    
    return status.ok();
}

bool RocksDBWrapper::TransactionWrapper::del(std::string_view key) {
    if (!txn_ || !active_) return false;
    
    rocksdb::Status status = txn_->Delete(rocksdb::Slice(key.data(), key.size()));
    return status.ok();
}

bool RocksDBWrapper::TransactionWrapper::commit() {
    if (!txn_ || !active_) return false;
    
    rocksdb::Status status = txn_->Commit();
    active_ = false;
    
    if (!status.ok()) {
        if (status.IsBusy() || status.IsTimedOut() || status.IsTryAgain()) {
            THEMIS_WARN("MVCC Conflict detected: {} - Transaction must be retried", status.ToString());
        } else {
            THEMIS_ERROR("Transaction commit failed: {}", status.ToString());
        }
        return false;
    }
    
    THEMIS_DEBUG("MVCC Transaction committed successfully");
    return true;
}

void RocksDBWrapper::TransactionWrapper::rollback() {
    if (!txn_ || !active_) return;
    
    txn_->Rollback();
    active_ = false;
    THEMIS_DEBUG("MVCC Transaction rolled back");
}

const rocksdb::Snapshot* RocksDBWrapper::TransactionWrapper::getSnapshot() const {
    return txn_ ? txn_->GetSnapshot() : nullptr;
}

std::unique_ptr<RocksDBWrapper::TransactionWrapper> RocksDBWrapper::beginTransaction() {
    return std::make_unique<TransactionWrapper>(this);
}

bool RocksDBWrapper::commitBatch(rocksdb::WriteBatch* batch) {
    if (!db_) return false;
    
    rocksdb::Status status = db_->Write(*write_options_, batch);
    return status.ok();
}

void RocksDBWrapper::scanPrefix(std::string_view prefix, ScanCallback callback) {
    if (!db_) return;
    
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(*read_options_));
    rocksdb::Slice prefix_slice(prefix.data(), prefix.size());
    
    for (it->Seek(prefix_slice); it->Valid() && it->key().starts_with(prefix_slice); it->Next()) {
        std::string_view key(it->key().data(), it->key().size());
        std::string_view value(it->value().data(), it->value().size());
        
        if (!callback(key, value)) {
            break; // Stop iteration if callback returns false
        }
    }
}

void RocksDBWrapper::scanRange(std::string_view start_key, std::string_view end_key, ScanCallback callback) {
    if (!db_) return;
    
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(*read_options_));
    rocksdb::Slice start_slice(start_key.data(), start_key.size());
    rocksdb::Slice end_slice(end_key.data(), end_key.size());
    
    for (it->Seek(start_slice); it->Valid() && it->key().compare(end_slice) < 0; it->Next()) {
        std::string_view key(it->key().data(), it->key().size());
        std::string_view value(it->value().data(), it->value().size());
        
        if (!callback(key, value)) {
            break;
        }
    }
}

void RocksDBWrapper::scanAll(ScanCallback callback) {
    if (!db_) return;
    
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(*read_options_));
    
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        std::string_view key(it->key().data(), it->key().size());
        std::string_view value(it->value().data(), it->value().size());
        
        if (!callback(key, value)) {
            break;
        }
    }
}

std::string RocksDBWrapper::getStats() const {
    if (!db_) return R"({"error": "Database not open"})";
    
    // Get full RocksDB stats text
    std::string raw_stats;
    db_->GetProperty("rocksdb.stats", &raw_stats);
    
    // Get specific numeric properties for structured output
    uint64_t block_cache_usage = 0;
    uint64_t block_cache_capacity = 0;
    uint64_t estimate_keys = 0;
    uint64_t estimate_live_data_size = 0;
    uint64_t estimate_pending_compaction_bytes = 0;
    uint64_t num_running_compactions = 0;
    uint64_t num_running_flushes = 0;
    uint64_t memtable_size = 0;
    uint64_t cur_size_all_mem_tables = 0;
    
    db_->GetIntProperty("rocksdb.block-cache-usage", &block_cache_usage);
    db_->GetIntProperty("rocksdb.block-cache-capacity", &block_cache_capacity);
    db_->GetIntProperty("rocksdb.estimate-num-keys", &estimate_keys);
    db_->GetIntProperty("rocksdb.estimate-live-data-size", &estimate_live_data_size);
    db_->GetIntProperty("rocksdb.estimate-pending-compaction-bytes", &estimate_pending_compaction_bytes);
    db_->GetIntProperty("rocksdb.num-running-compactions", &num_running_compactions);
    db_->GetIntProperty("rocksdb.num-running-flushes", &num_running_flushes);
    db_->GetIntProperty("rocksdb.size-all-mem-tables", &memtable_size);
    db_->GetIntProperty("rocksdb.cur-size-all-mem-tables", &cur_size_all_mem_tables);
    
    // Get per-level file counts
    std::string num_files_at_levels;
    for (int level = 0; level < 7; ++level) {
        uint64_t num_files = 0;
        std::string prop = "rocksdb.num-files-at-level" + std::to_string(level);
        db_->GetIntProperty(prop, &num_files);
        if (level > 0) num_files_at_levels += ", ";
        num_files_at_levels += "\"L" + std::to_string(level) + "\": " + std::to_string(num_files);
    }
    
    // Get statistics counters if available
    std::string stats_counters;
    if (options_->statistics) {
        auto stats = options_->statistics;
        uint64_t block_cache_hit = stats->getTickerCount(rocksdb::BLOCK_CACHE_HIT);
        uint64_t block_cache_miss = stats->getTickerCount(rocksdb::BLOCK_CACHE_MISS);
        uint64_t bytes_written = stats->getTickerCount(rocksdb::BYTES_WRITTEN);
        uint64_t bytes_read = stats->getTickerCount(rocksdb::BYTES_READ);
        uint64_t compaction_key_drop_obsolete = stats->getTickerCount(rocksdb::COMPACTION_KEY_DROP_OBSOLETE);
        
        double hit_rate = 0.0;
        uint64_t total_access = block_cache_hit + block_cache_miss;
        if (total_access > 0) {
            hit_rate = static_cast<double>(block_cache_hit) / total_access * 100.0;
        }
        
        stats_counters = "\"block_cache_hit\": " + std::to_string(block_cache_hit) + ", "
                        "\"block_cache_miss\": " + std::to_string(block_cache_miss) + ", "
                        "\"cache_hit_rate_percent\": " + std::to_string(hit_rate) + ", "
                        "\"bytes_written\": " + std::to_string(bytes_written) + ", "
                        "\"bytes_read\": " + std::to_string(bytes_read) + ", "
                        "\"compaction_keys_dropped\": " + std::to_string(compaction_key_drop_obsolete);
    }
    
    // Build JSON response
    std::string json = "{\n"
        "  \"rocksdb\": {\n"
        "    \"block_cache_usage_bytes\": " + std::to_string(block_cache_usage) + ",\n"
        "    \"block_cache_capacity_bytes\": " + std::to_string(block_cache_capacity) + ",\n"
        "    \"estimate_num_keys\": " + std::to_string(estimate_keys) + ",\n"
        "    \"estimate_live_data_size_bytes\": " + std::to_string(estimate_live_data_size) + ",\n"
        "    \"estimate_pending_compaction_bytes\": " + std::to_string(estimate_pending_compaction_bytes) + ",\n"
        "    \"num_running_compactions\": " + std::to_string(num_running_compactions) + ",\n"
        "    \"num_running_flushes\": " + std::to_string(num_running_flushes) + ",\n"
        "    \"memtable_size_bytes\": " + std::to_string(memtable_size) + ",\n"
        "    \"cur_size_all_mem_tables_bytes\": " + std::to_string(cur_size_all_mem_tables) + ",\n"
        "    \"files_per_level\": { " + num_files_at_levels + " }";
    
    if (!stats_counters.empty()) {
        json += ",\n    " + stats_counters;
    }
    
    json += "\n  },\n"
        "  \"raw_stats\": " + nlohmann::json(raw_stats).dump() + "\n"
        "}";
    
    return json;
}

std::string RocksDBWrapper::getCompressionType() const {
    if (!db_) return "unknown (db closed)";
    
    // Query the active compression type via column family options
    auto cf_options = db_->GetOptions();
    
    auto fromCompression = [](rocksdb::CompressionType ct) -> std::string {
        switch (ct) {
            case rocksdb::kNoCompression: return "none";
            case rocksdb::kSnappyCompression: return "snappy";
            case rocksdb::kZlibCompression: return "zlib";
            case rocksdb::kBZip2Compression: return "bzip2";
            case rocksdb::kLZ4Compression: return "lz4";
            case rocksdb::kLZ4HCCompression: return "lz4hc";
            case rocksdb::kXpressCompression: return "xpress";
            case rocksdb::kZSTD: return "zstd";
            case rocksdb::kDisableCompressionOption: return "disabled";
            default: return "unknown";
        }
    };
    
    std::string default_compression = fromCompression(cf_options.compression);
    std::string bottommost = fromCompression(cf_options.bottommost_compression);
    
    return "default=" + default_compression + ", bottommost=" + bottommost;
}

void RocksDBWrapper::compactRange(std::string_view start_key, std::string_view end_key) {
    if (!db_) return;
    
    rocksdb::Slice start(start_key.data(), start_key.size());
    rocksdb::Slice end(end_key.data(), end_key.size());
    
    rocksdb::CompactRangeOptions options;
    db_->CompactRange(options, &start, &end);
}

void RocksDBWrapper::flush() {
    if (!db_) return;
    
    rocksdb::FlushOptions options;
    db_->Flush(options);
}

uint64_t RocksDBWrapper::getApproximateSize() const {
    if (!db_) return 0;
    
    // TODO: Implement proper size calculation
    return 0;
}

bool RocksDBWrapper::createCheckpoint(const std::string& checkpoint_dir) {
    if (!db_) {
        THEMIS_ERROR("createCheckpoint failed: DB is not open");
        fprintf(stderr, "%s\n", "createCheckpoint failed: DB is not open");
        return false;
    }
    try {
        // Ensure parent directory exists, but do not pre-create the checkpoint directory itself
        std::error_code ec;
        std::filesystem::path cpp(checkpoint_dir);
        auto parent = cpp.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                THEMIS_ERROR("Failed to create checkpoint parent directory '{}': {}", parent.string(), ec.message());
                fprintf(stderr, "Failed to create checkpoint parent directory '%s': %s\\n", parent.string().c_str(), ec.message().c_str());
                return false;
            }
        }
        rocksdb::Checkpoint* raw = nullptr;
        auto st = rocksdb::Checkpoint::Create(db_.get(), &raw);
        if (!st.ok()) {
            THEMIS_ERROR("RocksDB Checkpoint::Create failed: {}", st.ToString());
            fprintf(stderr, "RocksDB Checkpoint::Create failed: %s\n", st.ToString().c_str());
            return false;
        }
        std::unique_ptr<rocksdb::Checkpoint> cp(raw);
        st = cp->CreateCheckpoint(checkpoint_dir);
        if (!st.ok()) {
            THEMIS_ERROR("CreateCheckpoint to '{}' failed: {}", checkpoint_dir, st.ToString());
            fprintf(stderr, "CreateCheckpoint to '%s' failed: %s\\n", checkpoint_dir.c_str(), st.ToString().c_str());
            return false;
        }
        THEMIS_INFO("Checkpoint created at '{}'", checkpoint_dir);
        fprintf(stderr, "Checkpoint created at '%s'\n", checkpoint_dir.c_str());
        return true;
    } catch (const std::exception& e) {
        THEMIS_ERROR("createCheckpoint exception: {}", e.what());
        fprintf(stderr, "createCheckpoint exception: %s\n", e.what());
        return false;
    }
}

bool RocksDBWrapper::restoreFromCheckpoint(const std::string& checkpoint_dir) {
    try {
        if (!std::filesystem::exists(checkpoint_dir)) {
            THEMIS_ERROR("restoreFromCheckpoint: checkpoint dir '{}' does not exist", checkpoint_dir);
            fprintf(stderr, "restoreFromCheckpoint: checkpoint dir '%s' does not exist\n", checkpoint_dir.c_str());
            return false;
        }
        // Close DB if open
        if (db_) {
            close();
        }
        const auto& target = config_.db_path;
        std::error_code ec;
        if (std::filesystem::exists(target)) {
            std::filesystem::remove_all(target, ec);
            if (ec) {
                THEMIS_ERROR("Failed to remove existing DB path '{}': {}", target, ec.message());
                fprintf(stderr, "Failed to remove existing DB path '%s': %s\n", target.c_str(), ec.message().c_str());
                return false;
            }
        }
        std::filesystem::create_directories(target, ec);
        if (ec) {
            THEMIS_ERROR("Failed to create DB path '{}': {}", target, ec.message());
            fprintf(stderr, "Failed to create DB path '%s': %s\n", target.c_str(), ec.message().c_str());
            return false;
        }
        // Copy checkpoint contents into DB path
        std::filesystem::copy(
            checkpoint_dir,
            target,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
            ec
        );
        if (ec) {
            THEMIS_ERROR("Failed to copy checkpoint '{}' to '{}': {}", checkpoint_dir, target, ec.message());
            fprintf(stderr, "Failed to copy checkpoint '%s' to '%s': %s\n", checkpoint_dir.c_str(), target.c_str(), ec.message().c_str());
            return false;
        }
        // Reopen DB
        if (!open()) {
            THEMIS_ERROR("Failed to reopen DB after restore from '{}'", checkpoint_dir);
            fprintf(stderr, "Failed to reopen DB after restore from '%s'\n", checkpoint_dir.c_str());
            return false;
        }
        THEMIS_INFO("Restored DB from checkpoint '{}' to '{}'", checkpoint_dir, target);
        fprintf(stderr, "Restored DB from checkpoint '%s' to '%s'\n", checkpoint_dir.c_str(), target.c_str());
        return true;
    } catch (const std::exception& e) {
        THEMIS_ERROR("restoreFromCheckpoint exception: {}", e.what());
        fprintf(stderr, "restoreFromCheckpoint exception: %s\n", e.what());
        return false;
    }
}

} // namespace themis
