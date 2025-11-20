#include "sharding/rebalance_operation.h"
#include <stdexcept>
#include <sstream>

namespace themis {
namespace sharding {

RebalanceOperation::RebalanceOperation(const RebalanceOperationConfig& config)
    : config_(config), state_(RebalanceState::PLANNED) {
    
    if (config_.source_shard_id.empty() || config_.target_shard_id.empty()) {
        throw std::invalid_argument("Source and target shard IDs must not be empty");
    }
    
    if (config_.token_range_start >= config_.token_range_end) {
        throw std::invalid_argument("Invalid token range");
    }
    
    progress_.start_time = std::chrono::system_clock::now();
    progress_.total_records = 0; // Will be updated during execution
}

bool RebalanceOperation::start(const std::string& operator_signature) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate state transition
    if (state_ != RebalanceState::PLANNED) {
        return false;
    }
    
    // Validate operator authorization
    if (!validateOperator(operator_signature)) {
        return false;
    }
    
    // Transition to IN_PROGRESS
    state_ = RebalanceState::IN_PROGRESS;
    progress_.start_time = std::chrono::system_clock::now();
    
    return true;
}

bool RebalanceOperation::complete() {
    return transitionState(RebalanceState::IN_PROGRESS, RebalanceState::COMPLETED);
}

bool RebalanceOperation::fail(const std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != RebalanceState::IN_PROGRESS) {
        return false;
    }
    
    error_message_ = error_message;
    state_ = RebalanceState::FAILED;
    
    // Trigger automatic rollback if enabled
    if (config_.enable_rollback) {
        // Rollback will be handled externally
    }
    
    return true;
}

bool RebalanceOperation::rollback() {
    return transitionState(RebalanceState::FAILED, RebalanceState::ROLLED_BACK);
}

RebalanceState RebalanceOperation::getState() const {
    return state_.load();
}

RebalanceProgress RebalanceOperation::getProgress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

void RebalanceOperation::setProgressCallback(ProgressCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_callback_ = std::move(callback);
}

void RebalanceOperation::updateProgress(uint64_t records_migrated, uint64_t bytes_transferred) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    progress_.records_migrated = records_migrated;
    progress_.bytes_transferred = bytes_transferred;
    
    if (progress_.total_records > 0) {
        progress_.progress_percent = 
            (static_cast<double>(records_migrated) / progress_.total_records) * 100.0;
        
        // Estimate completion time
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - progress_.start_time).count();
        
        if (progress_.progress_percent > 0) {
            double total_seconds = (elapsed * 100.0) / progress_.progress_percent;
            progress_.estimated_completion = progress_.start_time + 
                std::chrono::seconds(static_cast<int64_t>(total_seconds));
        }
    }
    
    // Invoke callback if set
    if (progress_callback_) {
        progress_callback_(progress_);
    }
}

bool RebalanceOperation::validateOperator(const std::string& operator_signature) {
    // In a real implementation, this would:
    // 1. Load operator certificate from config_.operator_cert_path
    // 2. Verify certificate against CA
    // 3. Check certificate has "rebalance" capability
    // 4. Verify signature matches certificate
    
    // For now, simple validation
    if (operator_signature.empty()) {
        return false;
    }
    
    if (config_.operator_cert_path.empty()) {
        return false;
    }
    
    operator_validated_ = true;
    return true;
}

bool RebalanceOperation::transitionState(RebalanceState from, RebalanceState to) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    RebalanceState expected = from;
    return state_.compare_exchange_strong(expected, to);
}

} // namespace sharding
} // namespace themis
