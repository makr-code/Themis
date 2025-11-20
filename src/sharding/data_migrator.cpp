#include "sharding/data_migrator.h"
#include <stdexcept>
#include <thread>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

namespace themis {
namespace sharding {

DataMigrator::DataMigrator(const DataMigratorConfig& config)
    : config_(config) {
    
    if (config_.source_endpoint.empty() || config_.target_endpoint.empty()) {
        throw std::invalid_argument("Source and target endpoints must not be empty");
    }
    
    if (config_.batch_size == 0) {
        throw std::invalid_argument("Batch size must be greater than 0");
    }
}

MigrationResult DataMigrator::migrate(
    const std::string& source_shard_id,
    const std::string& target_shard_id,
    uint64_t token_range_start,
    uint64_t token_range_end,
    ProgressCallback progress_callback
) {
    MigrationResult result;
    MigrationProgress progress;
    
    try {
        // Estimate total records (would query source shard in real implementation)
        progress.total_records = 10000; // Placeholder
        
        uint32_t offset = 0;
        uint32_t records_in_batch = config_.batch_size;
        
        while (records_in_batch == config_.batch_size) {
            // Fetch batch from source
            auto batch = fetchBatch(source_shard_id, token_range_start, 
                                  token_range_end, offset, config_.batch_size);
            
            records_in_batch = batch.is_array() ? static_cast<uint32_t>(batch.size()) : 0;
            
            if (records_in_batch == 0) {
                break;
            }
            
            // Calculate hash if verification enabled
            std::string batch_hash;
            if (config_.verify_integrity) {
                batch_hash = calculateHash(batch);
            }
            
            // Write batch to target
            if (!writeBatch(target_shard_id, batch)) {
                result.errors.push_back("Failed to write batch at offset " + 
                                      std::to_string(offset));
                progress.errors++;
                
                if (progress.errors > config_.max_retries) {
                    result.error_message = "Too many errors during migration";
                    return result;
                }
                continue;
            }
            
            // Update progress
            progress.records_migrated += records_in_batch;
            progress.bytes_transferred += batch.dump().size();
            progress.progress_percent = 
                (static_cast<double>(progress.records_migrated) / progress.total_records) * 100.0;
            
            if (progress_callback) {
                progress_callback(progress);
            }
            
            offset += records_in_batch;
            result.records_migrated = progress.records_migrated;
            result.bytes_transferred = progress.bytes_transferred;
        }
        
        // Verify integrity if enabled
        if (config_.verify_integrity) {
            if (!verifyIntegrity(source_shard_id, target_shard_id, 
                               token_range_start, token_range_end)) {
                result.error_message = "Integrity verification failed";
                return result;
            }
        }
        
        result.success = true;
        
    } catch (const std::exception& e) {
        result.error_message = std::string("Migration failed: ") + e.what();
        result.success = false;
    }
    
    return result;
}

bool DataMigrator::verifyIntegrity(
    const std::string& source_shard_id,
    const std::string& target_shard_id,
    uint64_t token_range_start,
    uint64_t token_range_end
) {
    try {
        // Fetch data from both shards
        auto source_data = fetchBatch(source_shard_id, token_range_start, 
                                     token_range_end, 0, 10000);
        auto target_data = fetchBatch(target_shard_id, token_range_start, 
                                     token_range_end, 0, 10000);
        
        // Calculate hashes
        std::string source_hash = calculateHash(source_data);
        std::string target_hash = calculateHash(target_data);
        
        return source_hash == target_hash;
        
    } catch (const std::exception&) {
        return false;
    }
}

nlohmann::json DataMigrator::fetchBatch(
    const std::string& source_shard_id,
    uint64_t token_range_start,
    uint64_t token_range_end,
    uint32_t offset,
    uint32_t limit
) {
    (void)source_shard_id; (void)token_range_end; // Future: implement shard connection
    // In a real implementation, this would:
    // 1. Connect to source shard using mTLS Client
    // 2. Query for records in token range with offset/limit
    // 3. Return JSON array of records
    
    // Placeholder implementation
    nlohmann::json batch = nlohmann::json::array();
    
    for (uint32_t i = 0; i < limit && (offset + i) < 100; ++i) {
        batch.push_back({
            {"id", offset + i},
            {"token", token_range_start + i},
            {"data", "placeholder"}
        });
    }
    
    return batch;
}

bool DataMigrator::writeBatch(
    const std::string& target_shard_id,
    const nlohmann::json& batch
) {
    (void)target_shard_id; // Future: implement shard connection
    // In a real implementation, this would:
    // 1. Connect to target shard using mTLS Client
    // 2. POST batch data to target shard
    // 3. Return success/failure
    
    // Placeholder implementation
    if (!batch.is_array() || batch.empty()) {
        return false;
    }
    
    return retryOperation([&]() {
        // Simulate write operation
        return true;
    });
}

std::string DataMigrator::calculateHash(const nlohmann::json& data) {
    // Calculate SHA-256 hash of JSON data
    std::string data_str = data.dump();
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data_str.c_str()), 
           data_str.size(), hash);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

template<typename Func>
bool DataMigrator::retryOperation(Func func) {
    for (uint32_t attempt = 0; attempt < config_.max_retries; ++attempt) {
        try {
            if (func()) {
                return true;
            }
        } catch (const std::exception&) {
            // Continue to retry
        }
        
        if (attempt < config_.max_retries - 1) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.retry_delay_ms * (attempt + 1))
            );
        }
    }
    
    return false;
}

} // namespace sharding
} // namespace themis
