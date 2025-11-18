#include "timeseries/retention.h"
#include "timeseries/tsstore.h"
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/iterator.h>

namespace themis {

// (removed) list_metrics helper was unused; removed to avoid C4505 warning

size_t RetentionManager::apply() {
    if (!store_) return 0;
    // Extract DB internals from store
    // We rely on TSStore API: we will scan metrics and call deleteOldData
    size_t total_deleted = 0;
    // TSStore does not expose db handle; we scan via deleteOldData per metric by iterating entities
    // Simpler: iterate all keys and delete if older than threshold per metric using deleteOldData
    // But TSStore::deleteOldData deletes globally across all metrics before timestamp.
    // We'll call deleteOldData for each metric threshold.

    // Build now timestamp
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& [metric, retention] : policy_.per_metric) {
        int64_t cutoff = now_ms - std::chrono::duration_cast<std::chrono::milliseconds>(retention).count();
        total_deleted += store_->deleteOldDataForMetric(metric, cutoff);
    }
    return total_deleted;
}

} // namespace themis
