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

/**
 * @brief Time-Range Filter for querying edges valid during a time window
 * 
 * Checks if edge has ANY overlap with the query time range [range_start, range_end].
 * Edge is included if: edge.valid_from <= range_end AND edge.valid_to >= range_start
 */
struct TimeRangeFilter {
    std::optional<int64_t> range_start_ms;  // Query window start (null = unbounded past)
    std::optional<int64_t> range_end_ms;    // Query window end (null = unbounded future)
    
    /**
     * @brief Check if edge overlaps with query time range
     * @param valid_from Edge validity start
     * @param valid_to Edge validity end
     * @return true if edge has any temporal overlap with query range
     */
    bool hasOverlap(std::optional<int64_t> valid_from, std::optional<int64_t> valid_to) const {
        // No range filter = include all edges
        if (!range_start_ms.has_value() && !range_end_ms.has_value()) {
            return true;
        }
        
        // Edge starts after range ends: no overlap
        if (range_end_ms.has_value() && valid_from.has_value() && *valid_from > *range_end_ms) {
            return false;
        }
        
        // Edge ends before range starts: no overlap
        if (range_start_ms.has_value() && valid_to.has_value() && *valid_to < *range_start_ms) {
            return false;
        }
        
        // Otherwise: overlap exists
        return true;
    }
    
    /**
     * @brief Check if edge is fully contained within query time range
     * @param valid_from Edge validity start
     * @param valid_to Edge validity end
     * @return true if edge is completely within query range
     */
    bool fullyContains(std::optional<int64_t> valid_from, std::optional<int64_t> valid_to) const {
        // Check lower bound: edge starts after or at range start
        if (range_start_ms.has_value()) {
            if (!valid_from.has_value() || *valid_from < *range_start_ms) {
                return false;
            }
        }
        
        // Check upper bound: edge ends before or at range end
        if (range_end_ms.has_value()) {
            if (!valid_to.has_value() || *valid_to > *range_end_ms) {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * @brief Create filter for time range
     */
    static TimeRangeFilter between(int64_t start_ms, int64_t end_ms) {
        return TimeRangeFilter{start_ms, end_ms};
    }
    
    /**
     * @brief Create filter for time range from start to unbounded future
     */
    static TimeRangeFilter since(int64_t start_ms) {
        return TimeRangeFilter{start_ms, std::nullopt};
    }
    
    /**
     * @brief Create filter for time range from unbounded past to end
     */
    static TimeRangeFilter until(int64_t end_ms) {
        return TimeRangeFilter{std::nullopt, end_ms};
    }
    
    /**
     * @brief Create filter that includes all edges (no filtering)
     */
    static TimeRangeFilter all() {
        return TimeRangeFilter{std::nullopt, std::nullopt};
    }
};

/**
 * @brief Temporal Statistics for edges in a time range
 * 
 * Provides aggregated metrics over edges valid during a specific time window.
 */
struct TemporalStats {
    size_t edge_count = 0;              // Total edges with any overlap
    size_t fully_contained_count = 0;   // Edges fully within time range
    
    // Duration statistics (only for bounded edges)
    size_t bounded_edge_count = 0;      // Edges with both valid_from and valid_to
    double avg_duration_ms = 0.0;       // Average duration of bounded edges
    double total_duration_ms = 0.0;     // Sum of all durations
    std::optional<int64_t> min_duration_ms;  // Shortest edge duration
    std::optional<int64_t> max_duration_ms;  // Longest edge duration
    
    // Temporal range coverage
    std::optional<int64_t> earliest_start;   // Earliest valid_from among all edges
    std::optional<int64_t> latest_end;       // Latest valid_to among all edges
    
    /**
     * @brief Pretty-print statistics
     */
    std::string toString() const;
};

} // namespace themis

#endif // THEMIS_TEMPORAL_GRAPH_H
