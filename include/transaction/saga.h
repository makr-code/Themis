#pragma once

#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <algorithm>
#include "storage/rocksdb_wrapper.h"

namespace themis {

// Forward declarations
class BaseEntity;
class SecondaryIndexManager;
class GraphIndexManager;
class VectorIndexManager;

/// SAGA Pattern: Distributed Transaction with Compensating Actions
/// 
/// Each operation in a transaction records a compensating action that can undo it.
/// On rollback, compensating actions are executed in reverse order.
/// Guarantees eventual consistency even if individual operations fail.

class Saga {
public:
    using CompensatingAction = std::function<void()>;
    
    struct Step {
        std::string operation_name;
        CompensatingAction compensate;
        std::chrono::system_clock::time_point executed_at;
        bool compensated = false;
        
        Step(std::string name, CompensatingAction action)
            : operation_name(std::move(name))
            , compensate(std::move(action))
            , executed_at(std::chrono::system_clock::now()) {}
    };
    
    Saga() = default;
    ~Saga();
    
    // Disable copy, enable move
    Saga(const Saga&) = delete;
    Saga& operator=(const Saga&) = delete;
    Saga(Saga&&) noexcept = default;
    Saga& operator=(Saga&&) noexcept = default;
    
    /// Add a step with its compensating action
    void addStep(std::string operation_name, CompensatingAction compensate);
    
    /// Execute all compensating actions in reverse order
    void compensate();
    
    /// Clear all steps (called after successful commit)
    void clear();
    
    /// Get number of recorded steps
    size_t stepCount() const { return steps_.size(); }
    
    /// Get number of compensated steps
    size_t compensatedCount() const;
    
    /// Check if all steps have been compensated
    bool isFullyCompensated() const;
    
    /// Get step history for debugging
    std::vector<std::string> getStepHistory() const;
    
    /// Get duration since first step
    int64_t getDurationMs() const;
    
private:
    std::vector<Step> steps_;
    bool compensated_ = false;
};

/// SAGA-aware Transaction Operations
/// These track compensating actions for each operation

struct SagaOperation {
    /// Put entity with compensating delete
    static void putEntityWithCompensation(
        RocksDBWrapper& db,
        const std::string& key,
        const std::vector<uint8_t>& value,
        Saga& saga
    );
    
    /// Delete entity with compensating restore
    static void deleteEntityWithCompensation(
        RocksDBWrapper& db,
        const std::string& key,
        Saga& saga
    );
    
    /// Secondary index put with compensating delete
    static void indexPutWithCompensation(
        SecondaryIndexManager& idx,
        const std::string& table,
        const BaseEntity& entity,
        RocksDBWrapper::WriteBatchWrapper& batch,
        Saga& saga
    );
    
    /// Graph edge add with compensating delete
    static void graphAddWithCompensation(
        GraphIndexManager& graph,
        const BaseEntity& edge,
        RocksDBWrapper::WriteBatchWrapper& batch,
        Saga& saga
    );
    
    /// Vector add with compensating cache cleanup
    static void vectorAddWithCompensation(
        VectorIndexManager& vec,
        const BaseEntity& entity,
        RocksDBWrapper::WriteBatchWrapper& batch,
        const std::string& vectorField,
        Saga& saga
    );
};

} // namespace themis
