#ifndef THEMIS_RETENTION_H
#define THEMIS_RETENTION_H

#include <string>
#include <unordered_map>
#include <chrono>

namespace rocksdb { class TransactionDB; class ColumnFamilyHandle; }

namespace themis {

class TSStore;

struct RetentionPolicy {
    // Retention per metric in seconds (0 or missing means ignore)
    std::unordered_map<std::string, std::chrono::seconds> per_metric;
};

class RetentionManager {
public:
    RetentionManager(TSStore* store, RetentionPolicy policy)
        : store_(store), policy_(std::move(policy)) {}

    // Apply retention for now()
    size_t apply();

private:
    TSStore* store_;
    RetentionPolicy policy_;
};

} // namespace themis

#endif // THEMIS_RETENTION_H
