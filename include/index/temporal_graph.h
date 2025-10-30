#ifndef THEMIS_TEMPORAL_GRAPH_H
#define THEMIS_TEMPORAL_GRAPH_H

#include <cstdint>
#include <optional>
#include <string>
#include <chrono>

namespace themis {

/**
 * @brief Temporal Graph Extension (Sprint B)
 * 
 * Adds time-awareness to graph edges with valid_from/valid_to timestamps.
 * 
 * Design:
 * - Edges can have optional valid_from and valid_to fields (int64_t milliseconds since epoch)
 * - Traversal queries can filter by time point: t ∈ [valid_from, valid_to]
 * - Null values mean unbounded (valid_from=null → valid since beginning, valid_to=null → valid forever)
 * 
 * Schema Addition (BaseEntity fields for edges):
 * - valid_from: int64_t (optional) - Start of validity period
 * - valid_to: int64_t (optional) - End of validity period
 * 
 * Query Examples:
 * - Find all edges valid at specific timestamp
 * - Find path through graph at specific point in time
 * - Track relationship evolution over time
 * 
 * MVP Scope:
 * - Filter edges by timestamp in traversal
 * - AQL extension: FILTER e.valid_from <= @t AND e.valid_to >= @t
 * - No automatic expiration (handled by queries)
 */

struct TemporalFilter {
    std::optional<int64_t> timestamp_ms; // Query time point (null = no filter)
    
    /**
     * @brief Check if edge is valid at query timestamp
     * @param valid_from Edge validity start (null = always valid from past)
     * @param valid_to Edge validity end (null = always valid into future)
     * @return true if edge should be included in results
     */
    bool isValid(std::optional<int64_t> valid_from, std::optional<int64_t> valid_to) const {
        // No temporal filter = include all edges
        if (!timestamp_ms.has_value()) {
            return true;
        }
        
        int64_t t = *timestamp_ms;
        
        // Check lower bound: t >= valid_from (or no lower bound)
        if (valid_from.has_value() && t < *valid_from) {
            return false;
        }
        
        // Check upper bound: t <= valid_to (or no upper bound)
        if (valid_to.has_value() && t > *valid_to) {
            return false;
        }
        
        return true;
    }
    
    /**
     * @brief Create filter for current time
     */
    static TemporalFilter now() {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        return TemporalFilter{now_ms};
    }
    
    /**
     * @brief Create filter for specific timestamp
     */
    static TemporalFilter at(int64_t timestamp_ms) {
        return TemporalFilter{timestamp_ms};
    }
    
    /**
     * @brief Create filter that includes all edges (no temporal filtering)
     */
    static TemporalFilter all() {
        return TemporalFilter{std::nullopt};
    }
};

} // namespace themis

#endif // THEMIS_TEMPORAL_GRAPH_H
