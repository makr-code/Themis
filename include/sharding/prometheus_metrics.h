#ifndef THEMIS_SHARDING_PROMETHEUS_METRICS_H
#define THEMIS_SHARDING_PROMETHEUS_METRICS_H

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

namespace themis {
namespace sharding {

/**
 * Prometheus metrics exporter for horizontal sharding system.
 * 
 * Exposes metrics in Prometheus text format via HTTP endpoint /metrics.
 * Tracks shard health, routing statistics, PKI events, migration progress,
 * query performance, and topology changes.
 */
class PrometheusMetrics {
public:
    struct Config {
        int http_port = 8080;
        std::string http_path = "/metrics";
        bool enable_histograms = true;
        int histogram_buckets = 10;
    };

    explicit PrometheusMetrics(const Config& config);
    ~PrometheusMetrics() = default;

    // Shard health metrics
    void recordShardHealth(const std::string& shard_id, const std::string& status);
    void recordCertificateExpiry(const std::string& shard_id, int64_t seconds_until_expiry);

    // Routing statistics
    void recordRoutingRequest(const std::string& type); // local/remote/scatter_gather
    void recordRoutingError(const std::string& shard_id, const std::string& error_type);
    void recordRoutingLatency(const std::string& operation, double latency_ms);

    // PKI events
    void recordPKIConnection(const std::string& shard_id, const std::string& result); // success/failure
    void recordCertificateValidation(const std::string& result);
    void recordCRLCheck(const std::string& result);

    // Migration progress
    void recordMigrationProgress(const std::string& operation_id, int64_t records, int64_t bytes, double percent);
    void recordMigrationDuration(const std::string& operation_id, double duration_seconds);

    // Query performance
    void recordQueryExecution(const std::string& query_type, double latency_ms);
    void recordScatterGatherFanout(int num_shards);
    void recordResultMergeTime(double time_ms);

    // Topology changes
    void recordTopologyChange(const std::string& change_type); // add/remove
    void recordClusterSize(int num_shards);
    void recordVirtualNodes(int total_vnodes);

    // Get metrics in Prometheus text format
    std::string getMetrics() const;

private:
    Config config_;
    mutable std::mutex mutex_;

    // Counters
    std::map<std::string, std::atomic<int64_t>> counters_;
    
    // Gauges
    std::map<std::string, std::atomic<double>> gauges_;
    
    // Histograms (simplified - stores recent values)
    std::map<std::string, std::vector<double>> histograms_;

    void incrementCounter(const std::string& name, const std::map<std::string, std::string>& labels = {});
    void setGauge(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    void observeHistogram(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    
    std::string formatLabels(const std::map<std::string, std::string>& labels) const;
    std::string getCounterKey(const std::string& name, const std::map<std::string, std::string>& labels) const;
};

} // namespace sharding
} // namespace themis

#endif // THEMIS_SHARDING_PROMETHEUS_METRICS_H
