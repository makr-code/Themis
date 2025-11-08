#ifndef THEMIS_TSSTORE_H
#define THEMIS_TSSTORE_H

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

// Forward declarations for RocksDB types
namespace rocksdb {
    class TransactionDB;
    class ColumnFamilyHandle;
}

namespace themis {

/**
 * @brief Time-Series Storage MVP (Sprint B)
 * 
 * Simple time-series storage for metrics, logs, and events with range queries
 * and aggregations. Designed for observability and monitoring use cases.
 * 
 * Key Schema: "{metric_name}:{entity_id}:{timestamp_ms}"
 * Value: JSON with fields: {"value": double, "tags": {...}, "metadata": {...}}
 * 
 * Features:
 * - Range queries by time interval
 * - Aggregations: min, max, avg, count, sum
 * - Tag-based filtering
 * - Efficient RocksDB range scans
 * 
 * MVP Scope (No automatic downsampling/retention yet):
 * - Raw data storage
 * - Basic aggregations computed on-the-fly
 * - Manual retention via deleteOldData()
 * 
 * Future Enhancements:
 * - Continuous aggregates (materialized views)
 * - Automatic retention policies
 * - Downsampling (1m → 1h → 1d)
 * 
 * Compression:
 * - Gorilla compression for float64 time-series (10-20x ratio, +15% CPU)
 * - Configurable per-metric compression strategy
 */
class TSStore {
public:
    enum class CompressionType {
        None,       // No compression (raw JSON)
        Gorilla     // Gorilla codec for time-series (10-20x ratio)
    };
    
    struct Config {
        // Default to Gorilla compression for better storage efficiency
        CompressionType compression = CompressionType::Gorilla;
        int chunk_size_hours = 24;  // Gorilla chunk size (hours)
    };
    
    struct DataPoint {
        std::string metric;           // Metric name (e.g., "cpu_usage")
        std::string entity;           // Entity ID (e.g., "server01")
        int64_t timestamp_ms;         // Timestamp in milliseconds since epoch
        double value;                 // Numeric value
        nlohmann::json tags;          // Tags for filtering (e.g., {"region": "us-east", "env": "prod"})
        nlohmann::json metadata;      // Additional metadata
        
        // Serialization
        nlohmann::json toJson() const;
        static DataPoint fromJson(const nlohmann::json& j);
    };
    
    struct QueryOptions {
        std::string metric;           // Required: metric name
        std::optional<std::string> entity; // Optional: filter by entity
        int64_t from_timestamp_ms = 0; // Start of time range (inclusive)
        int64_t to_timestamp_ms = INT64_MAX; // End of time range (inclusive)
        size_t limit = 1000;          // Max data points to return
        nlohmann::json tag_filter;    // Optional: filter by tags (exact match)
    };
    
    struct AggregationResult {
        double min = 0.0;
        double max = 0.0;
        double avg = 0.0;
        double sum = 0.0;
        size_t count = 0;
        int64_t first_timestamp_ms = 0;
        int64_t last_timestamp_ms = 0;
    };
    
    struct Stats {
        size_t total_data_points = 0;
        size_t total_metrics = 0;      // Number of unique metrics
        size_t total_size_bytes = 0;
        int64_t oldest_timestamp_ms = 0;
        int64_t newest_timestamp_ms = 0;
    };
    
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };
    
    /**
     * @brief Construct TSStore
     * @param db RocksDB TransactionDB instance (not owned)
     * @param cf Optional column family handle (nullptr = default CF)
     * @param config Compression and storage configuration
     */
    explicit TSStore(rocksdb::TransactionDB* db, 
                     rocksdb::ColumnFamilyHandle* cf = nullptr,
                     Config config = Config{});
    
    ~TSStore() = default;
    
    /**
     * @brief Get current compression configuration
     */
    const Config& getConfig() const { return config_; }
    
    /**
     * @brief Update compression configuration
     * @note Changes only affect new data points; existing data remains unchanged
     */
    void setConfig(const Config& config) { config_ = config; }
    
    /**
     * @brief Write a data point
     * @param point Data point to store
     * @return Status
     */
    Status putDataPoint(const DataPoint& point);
    
    /**
     * @brief Write multiple data points (batch operation)
     * @param points Vector of data points
     * @return Status
     */
    Status putDataPoints(const std::vector<DataPoint>& points);
    
    /**
     * @brief Query data points with filters
     * @param options Query options (time range, entity, tags)
     * @return Pair of Status and vector of data points
     */
    std::pair<Status, std::vector<DataPoint>> query(const QueryOptions& options) const;
    
    /**
     * @brief Compute aggregations over time range
     * @param options Query options
     * @return Pair of Status and aggregation result
     */
    std::pair<Status, AggregationResult> aggregate(const QueryOptions& options) const;
    
    /**
     * @brief Get time-series statistics
     * @return Stats struct
     */
    Stats getStats() const;
    
    /**
     * @brief Delete data older than specified timestamp (retention policy)
     * @param before_timestamp_ms Delete data points with timestamp < this value
     * @return Number of data points deleted
     */
    size_t deleteOldData(int64_t before_timestamp_ms);

    /**
     * @brief Delete old data for a specific metric (retention policy)
     * @param metric Metric name
     * @param before_timestamp_ms Delete data points with timestamp < this value
     * @return Number of data points deleted for that metric
     */
    size_t deleteOldDataForMetric(const std::string& metric, int64_t before_timestamp_ms);
    
    /**
     * @brief Delete all data for a specific metric
     * @param metric Metric name
     * @return Status
     */
    Status deleteMetric(const std::string& metric);
    
    /**
     * @brief Clear all time-series data (admin operation)
     */
    void clear();

private:
    rocksdb::TransactionDB* db_;
    rocksdb::ColumnFamilyHandle* cf_;
    Config config_;
    
    static constexpr const char* KEY_PREFIX = "ts:";
    static constexpr const char* GORILLA_CHUNK_PREFIX = "tsc:";
    
    // Key format: "ts:{metric}:{entity}:{timestamp_ms}"
    std::string makeKey(const std::string& metric, 
                       const std::string& entity, 
                       int64_t timestamp_ms) const;
    
    // Parse key to extract components
    struct KeyComponents {
        std::string metric;
        std::string entity;
        int64_t timestamp_ms;
    };
    std::optional<KeyComponents> parseKey(const std::string& key) const;
    
    // Check if data point matches tag filter
    bool matchesTagFilter(const DataPoint& point, const nlohmann::json& tag_filter) const;
};

} // namespace themis

#endif // THEMIS_TSSTORE_H
