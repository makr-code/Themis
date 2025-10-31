#ifndef THEMIS_CONTINUOUS_AGG_H
#define THEMIS_CONTINUOUS_AGG_H

#include <string>
#include <chrono>
#include <optional>

namespace themis {

class TSStore;

struct AggWindow {
    std::chrono::milliseconds size{std::chrono::minutes(1)};
};

enum class AggFunc { Min, Max, Avg, Sum, Count };

struct AggConfig {
    std::string metric;
    std::optional<std::string> entity; // nullopt = for all entities (not supported in MVP)
    AggWindow window;
    // For MVP we always compute min/max/avg/sum/count and store avg as value with metadata for others
};

class ContinuousAggregateManager {
public:
    explicit ContinuousAggregateManager(TSStore* store) : store_(store) {}
    // Compute aggregates for [from,to] and store as derived metric
    // Derived metric name: metric + "__agg_" + window_ms
    void refresh(const AggConfig& cfg, int64_t from_ms, int64_t to_ms);

    static std::string derivedMetricName(const std::string& base, std::chrono::milliseconds win);

private:
    TSStore* store_;
};

} // namespace themis

#endif // THEMIS_CONTINUOUS_AGG_H
