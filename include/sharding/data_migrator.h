#pragma once

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace themis {
namespace sharding {

// Progress information for data migration
struct MigrationProgress {
    uint64_t records_migrated = 0;
    uint64_t total_records = 0;
    uint64_t bytes_transferred = 0;
    uint64_t errors = 0;
    double progress_percent = 0.0;
};

// Result of a migration operation
struct MigrationResult {
    bool success = false;
    uint64_t records_migrated = 0;
    uint64_t bytes_transferred = 0;
    std::vector<std::string> errors;
    std::string error_message;
};

// Configuration for data migrator
struct DataMigratorConfig {
    std::string source_endpoint;
    std::string target_endpoint;
    std::string cert_path;
    std::string key_path;
    std::string ca_cert_path;
    uint32_t batch_size = 1000;
    bool verify_integrity = true;
    uint32_t max_retries = 3;
    uint32_t retry_delay_ms = 1000;
};

/**
 * Handles data migration between shards
 * 
 * Features:
 * - Stream-based batch processing
 * - Data integrity verification (hash-based)
 * - Atomic cutover
 * - Progress tracking
 * - Error handling with retry
 */
class DataMigrator {
public:
    using ProgressCallback = std::function<void(const MigrationProgress&)>;

    explicit DataMigrator(const DataMigratorConfig& config);
    ~DataMigrator() = default;

    /**
     * Migrate data for a token range from source to target shard
     * 
     * @param source_shard_id Source shard identifier
     * @param target_shard_id Target shard identifier
     * @param token_range_start Start of token range to migrate
     * @param token_range_end End of token range to migrate
     * @param progress_callback Optional callback for progress updates
     * @return MigrationResult with success status and statistics
     */
    MigrationResult migrate(
        const std::string& source_shard_id,
        const std::string& target_shard_id,
        uint64_t token_range_start,
        uint64_t token_range_end,
        ProgressCallback progress_callback = nullptr
    );

    /**
     * Verify data integrity between source and target
     * 
     * @param source_shard_id Source shard identifier
     * @param target_shard_id Target shard identifier
     * @param token_range_start Start of token range
     * @param token_range_end End of token range
     * @return true if data matches, false otherwise
     */
    bool verifyIntegrity(
        const std::string& source_shard_id,
        const std::string& target_shard_id,
        uint64_t token_range_start,
        uint64_t token_range_end
    );

private:
    DataMigratorConfig config_;

    // Fetch batch of records from source
    nlohmann::json fetchBatch(
        const std::string& source_shard_id,
        uint64_t token_range_start,
        uint64_t token_range_end,
        uint32_t offset,
        uint32_t limit
    );

    // Write batch to target
    bool writeBatch(
        const std::string& target_shard_id,
        const nlohmann::json& batch
    );

    // Calculate hash for data integrity
    std::string calculateHash(const nlohmann::json& data);

    // Retry logic for failed operations
    template<typename Func>
    bool retryOperation(Func func);
};

} // namespace sharding
} // namespace themis
