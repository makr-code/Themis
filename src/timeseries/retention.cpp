#include "timeseries/retention.h"
#include "timeseries/tsstore.h"
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/iterator.h>

namespace themis {

// Helper: list metric names by scanning prefix keys
static std::vector<std::string> list_metrics(rocksdb::TransactionDB* db, rocksdb::ColumnFamilyHandle* cf) {
    std::vector<std::string> out;
    rocksdb::ReadOptions ro;
    std::unique_ptr<rocksdb::Iterator> it;
    if (cf) it.reset(db->NewIterator(ro, cf)); else it.reset(db->NewIterator(ro));
    std::string prefix = "ts:";
    it->Seek(prefix);
    std::unordered_set<std::string> seen;
    while (it->Valid()) {
        auto k = it->key().ToString();
        if (k.compare(0, prefix.size(), prefix) != 0) break;
        auto p2 = k.find(':', prefix.size());
        if (p2 == std::string::npos) { it->Next(); continue; }
        std::string metric = k.substr(prefix.size(), p2 - prefix.size());
        if (!seen.count(metric)) { seen.insert(metric); out.push_back(metric); }
        it->Next();
    }
    return out;
}

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
