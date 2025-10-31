#include "timeseries/continuous_agg.h"
#include "timeseries/tsstore.h"
#include <sstream>

namespace themis {

std::string ContinuousAggregateManager::derivedMetricName(const std::string& base, std::chrono::milliseconds win) {
    std::ostringstream oss;
    oss << base << "__agg_" << win.count() << "ms";
    return oss.str();
}

void ContinuousAggregateManager::refresh(const AggConfig& cfg, int64_t from_ms, int64_t to_ms) {
    if (!store_) return;
    const auto win_ms = cfg.window.size.count();
    const std::string out_metric = derivedMetricName(cfg.metric, cfg.window.size);

    // For MVP: if entity is provided, aggregate for that entity; otherwise, do nothing
    if (!cfg.entity.has_value()) return;
    const std::string entity = *cfg.entity;

    // Iterate windows
    for (int64_t wstart = from_ms; wstart <= to_ms; wstart += win_ms) {
        int64_t wend = std::min(wstart + win_ms - 1, to_ms);

        TSStore::QueryOptions qopt;
        qopt.metric = cfg.metric;
        qopt.entity = entity;
        qopt.from_timestamp_ms = wstart;
        qopt.to_timestamp_ms = wend;
        qopt.limit = 1000000; // big window cap

        auto [st, points] = store_->query(qopt);
        if (!st.ok || points.empty()) continue;

        // Compute aggregates
        double minv = points[0].value;
        double maxv = points[0].value;
        double sum = 0.0;
        for (const auto& p : points) {
            if (p.value < minv) minv = p.value;
            if (p.value > maxv) maxv = p.value;
            sum += p.value;
        }
        size_t count = points.size();
        double avg = sum / static_cast<double>(count);

        // Store one data point per window at window end with avg as value and metadata
        TSStore::DataPoint out;
        out.metric = out_metric;
        out.entity = entity;
        out.timestamp_ms = wend;
        out.value = avg;
        out.metadata = {
            {"min", minv}, {"max", maxv}, {"sum", sum}, {"count", count},
            {"from_ms", wstart}, {"to_ms", wend}
        };
        store_->putDataPoint(out);
    }
}

} // namespace themis
