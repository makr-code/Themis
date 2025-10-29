#include "transaction/saga.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "utils/logger.h"
#include <algorithm>

namespace themis {

Saga::~Saga() {
    if (!compensated_ && !steps_.empty()) {
        THEMIS_WARN("Saga destructed without compensation - auto-compensating {} steps", steps_.size());
        compensate();
    }
}

void Saga::addStep(std::string operation_name, CompensatingAction compensate) {
    steps_.emplace_back(std::move(operation_name), std::move(compensate));
    THEMIS_DEBUG("SAGA: Added step '{}' (total steps: {})", steps_.back().operation_name, steps_.size());
}

void Saga::compensate() {
    if (compensated_) {
        THEMIS_WARN("SAGA: Already compensated, skipping");
        return;
    }
    
    THEMIS_INFO("SAGA: Compensating {} steps in reverse order", steps_.size());
    
    // Execute compensating actions in reverse order
    for (auto it = steps_.rbegin(); it != steps_.rend(); ++it) {
        if (it->compensated) {
            THEMIS_DEBUG("SAGA: Step '{}' already compensated, skipping", it->operation_name);
            continue;
        }
        
        try {
            THEMIS_DEBUG("SAGA: Compensating step '{}'", it->operation_name);
            it->compensate();
            it->compensated = true;
        } catch (const std::exception& e) {
            THEMIS_ERROR("SAGA: Compensation failed for '{}': {}", it->operation_name, e.what());
            // Continue with other compensations
        } catch (...) {
            THEMIS_ERROR("SAGA: Unknown error during compensation of '{}'", it->operation_name);
        }
    }
    
    compensated_ = true;
    THEMIS_INFO("SAGA: Compensation complete ({}/{} steps)", compensatedCount(), steps_.size());
}

void Saga::clear() {
    THEMIS_DEBUG("SAGA: Clearing {} steps", steps_.size());
    steps_.clear();
    compensated_ = false;
}

size_t Saga::compensatedCount() const {
    return std::count_if(steps_.begin(), steps_.end(), 
                        [](const Step& s) { return s.compensated; });
}

bool Saga::isFullyCompensated() const {
    return compensated_ && compensatedCount() == steps_.size();
}

std::vector<std::string> Saga::getStepHistory() const {
    std::vector<std::string> history;
    history.reserve(steps_.size());
    for (const auto& step : steps_) {
        std::string status = step.compensated ? "[COMPENSATED]" : "[ACTIVE]";
        history.push_back(status + " " + step.operation_name);
    }
    return history;
}

int64_t Saga::getDurationMs() const {
    if (steps_.empty()) return 0;
    auto now = std::chrono::system_clock::now();
    auto first_step_time = steps_[0].executed_at;
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - first_step_time).count();
}

// ========== SAGA Operations ==========

void SagaOperation::putEntityWithCompensation(
    RocksDBWrapper& db,
    const std::string& key,
    const std::vector<uint8_t>& value,
    Saga& saga
) {
    // Check if key exists (for idempotency)
    auto existing = db.get(key);
    
    // Compensating action: restore old value or delete
    if (existing.has_value()) {
        // Update case: restore old value
        std::vector<uint8_t> old_value = std::move(*existing);
        saga.addStep("putEntity:" + key, [&db, key, old_value]() {
            db.put(key, old_value);
            THEMIS_DEBUG("SAGA: Restored old value for key '{}'", key);
        });
    } else {
        // Insert case: delete on rollback
        saga.addStep("putEntity:" + key, [&db, key]() {
            db.del(key);
            THEMIS_DEBUG("SAGA: Deleted key '{}' (compensating insert)", key);
        });
    }
}

void SagaOperation::deleteEntityWithCompensation(
    RocksDBWrapper& db,
    const std::string& key,
    Saga& saga
) {
    // Save current value for restore
    auto existing = db.get(key);
    
    if (!existing.has_value()) {
        THEMIS_WARN("SAGA: Delete of non-existent key '{}' - no compensation needed", key);
        return;
    }
    
    std::vector<uint8_t> old_value = std::move(*existing);
    
    // Compensating action: restore deleted value
    saga.addStep("deleteEntity:" + key, [&db, key, old_value]() {
        db.put(key, old_value);
        THEMIS_DEBUG("SAGA: Restored deleted key '{}'", key);
    });
}

void SagaOperation::indexPutWithCompensation(
    SecondaryIndexManager& idx,
    const std::string& table,
    const BaseEntity& entity,
    RocksDBWrapper::WriteBatchWrapper& batch,
    Saga& saga
) {
    const std::string& pk = entity.getPrimaryKey();
    
    // Compensating action: remove from secondary index
    // Note: We don't need old values since we're just removing index entries
    saga.addStep("indexPut:" + table + ":" + pk, [&idx, table, pk]() {
        // Create a temporary batch for compensation
        // This is executed outside the main transaction batch
        THEMIS_WARN("SAGA: Index compensation requires direct DB access - not fully implemented yet");
        // TODO: Need access to RocksDB for creating compensation batch
    });
}
void SagaOperation::graphAddWithCompensation(
    GraphIndexManager& graph,
    const BaseEntity& edge,
    RocksDBWrapper::WriteBatchWrapper& batch,
    Saga& saga
) {
    std::string edge_id = edge.getPrimaryKey();
    
    // Compensating action: delete graph edge
    // This is tricky because we're in a batch context
    saga.addStep("graphAdd:" + edge_id, [&graph, edge_id]() {
        THEMIS_WARN("SAGA: Graph compensation requires batch context - simplified implementation");
        // TODO: Store edge details for proper compensation
    });
}

void SagaOperation::vectorAddWithCompensation(
    VectorIndexManager& vec,
    const BaseEntity& entity,
    RocksDBWrapper::WriteBatchWrapper& batch,
    const std::string& vectorField,
    Saga& saga
) {
    const std::string& pk = entity.getPrimaryKey();
    
    // Compensating action: remove from vector cache and HNSW
    saga.addStep("vectorAdd:" + pk, [&vec, pk]() {
        auto status = vec.removeByPk(pk);
        if (!status.ok) {
            THEMIS_WARN("SAGA: Vector compensation failed for '{}': {}", pk, status.message);
        } else {
            THEMIS_DEBUG("SAGA: Removed vector '{}' from cache", pk);
        }
    });
}

} // namespace themis
