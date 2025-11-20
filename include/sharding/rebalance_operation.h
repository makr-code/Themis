#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>
#include <memory>

namespace themis {
namespace sharding {

// State of a rebalance operation
enum class RebalanceState {
    PLANNED,        // Initial state, not started
    IN_PROGRESS,    // Currently executing
    COMPLETED,      // Successfully completed
    FAILED,         // Failed during execution
    ROLLED_BACK     // Rolled back after failure
};

// Progress information for a rebalance operation
struct RebalanceProgress {
    uint64_t records_migrated = 0;
    uint64_t total_records = 0;
    uint64_t bytes_transferred = 0;
    double progress_percent = 0.0;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point estimated_completion;
};

// Configuration for rebalance operations
struct RebalanceOperationConfig {
    std::string source_shard_id;
    std::string target_shard_id;
    uint64_t token_range_start;
    uint64_t token_range_end;
    std::string operator_cert_path;     // Certificate of operator authorizing the rebalance
    std::string ca_cert_path;           // CA certificate for verification
    uint32_t batch_size = 1000;         // Records per batch
    bool verify_data = true;            // Verify data integrity
    bool enable_rollback = true;        // Enable automatic rollback on failure
};

/**
 * Manages rebalance operations for shard data migration
 * 
 * Handles:
 * - Operator authorization via PKI certificates
 * - State machine for rebalance lifecycle
 * - Progress tracking
 * - Rollback support on failure
 * - Token range migration planning
 */
class RebalanceOperation {
public:
    using ProgressCallback = std::function<void(const RebalanceProgress&)>;

    explicit RebalanceOperation(const RebalanceOperationConfig& config);
    ~RebalanceOperation() = default;

    // Start the rebalance operation with operator signature
    bool start(const std::string& operator_signature);

    // Complete the operation successfully
    bool complete();

    // Mark operation as failed
    bool fail(const std::string& error_message);

    // Rollback the operation
    bool rollback();

    // Get current state
    RebalanceState getState() const;

    // Get progress information
    RebalanceProgress getProgress() const;

    // Set progress callback
    void setProgressCallback(ProgressCallback callback);

    // Update progress (called by data migrator)
    void updateProgress(uint64_t records_migrated, uint64_t bytes_transferred);

    // Validate operator certificate and signature
    bool validateOperator(const std::string& operator_signature);

private:
    RebalanceOperationConfig config_;
    std::atomic<RebalanceState> state_;
    mutable std::mutex mutex_;
    
    RebalanceProgress progress_;
    ProgressCallback progress_callback_;
    
    std::string error_message_;
    bool operator_validated_ = false;

    // Internal state transitions
    bool transitionState(RebalanceState from, RebalanceState to);
};

} // namespace sharding
} // namespace themis
