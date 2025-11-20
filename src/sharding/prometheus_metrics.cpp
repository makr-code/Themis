#include "sharding/prometheus_metrics.h"
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace themis {
namespace sharding {

PrometheusMetrics::PrometheusMetrics(const Config& config)
    : config_(config) {
}

void PrometheusMetrics::recordShardHealth(const std::string& shard_id, const std::string& status) {
    setGauge("themis_shard_health_status", 1.0, {{"shard_id", shard_id}, {"status", status}});
}

void PrometheusMetrics::recordCertificateExpiry(const std::string& shard_id, int64_t seconds_until_expiry) {
    setGauge("themis_shard_certificate_expiry_seconds", static_cast<double>(seconds_until_expiry), 
             {{"shard_id", shard_id}});
}

void PrometheusMetrics::recordRoutingRequest(const std::string& type) {
    incrementCounter("themis_routing_requests_total", {{"type", type}});
}

void PrometheusMetrics::recordRoutingError(const std::string& shard_id, const std::string& error_type) {
    incrementCounter("themis_routing_errors_total", {{"shard_id", shard_id}, {"error_type", error_type}});
}

void PrometheusMetrics::recordRoutingLatency(const std::string& operation, double latency_ms) {
    observeHistogram("themis_routing_latency_seconds", latency_ms / 1000.0, {{"operation", operation}});
}

void PrometheusMetrics::recordPKIConnection(const std::string& shard_id, const std::string& result) {
    incrementCounter("themis_pki_connections_total", {{"shard_id", shard_id}, {"result", result}});
}

void PrometheusMetrics::recordCertificateValidation(const std::string& result) {
    incrementCounter("themis_pki_certificate_validations_total", {{"result", result}});
}

void PrometheusMetrics::recordCRLCheck(const std::string& result) {
    incrementCounter("themis_pki_crl_checks_total", {{"result", result}});
}

void PrometheusMetrics::recordMigrationProgress(const std::string& operation_id, int64_t records, 
                                                 int64_t bytes, double percent) {
    setGauge("themis_migration_records_total", static_cast<double>(records), {{"operation_id", operation_id}});
    setGauge("themis_migration_bytes_total", static_cast<double>(bytes), {{"operation_id", operation_id}});
    setGauge("themis_migration_progress_percent", percent, {{"operation_id", operation_id}});
}

void PrometheusMetrics::recordMigrationDuration(const std::string& operation_id, double duration_seconds) {
    setGauge("themis_migration_duration_seconds", duration_seconds, {{"operation_id", operation_id}});
}

void PrometheusMetrics::recordQueryExecution(const std::string& query_type, double latency_ms) {
    observeHistogram("themis_query_execution_seconds", latency_ms / 1000.0, {{"query_type", query_type}});
}

void PrometheusMetrics::recordScatterGatherFanout(int num_shards) {
    observeHistogram("themis_scatter_gather_fanout", static_cast<double>(num_shards), {});
}

void PrometheusMetrics::recordResultMergeTime(double time_ms) {
    observeHistogram("themis_result_merge_time_seconds", time_ms / 1000.0, {});
}

void PrometheusMetrics::recordTopologyChange(const std::string& change_type) {
    incrementCounter("themis_topology_changes_total", {{"change_type", change_type}});
}

void PrometheusMetrics::recordClusterSize(int num_shards) {
    setGauge("themis_cluster_size", static_cast<double>(num_shards), {});
}

void PrometheusMetrics::recordVirtualNodes(int total_vnodes) {
    setGauge("themis_virtual_nodes_total", static_cast<double>(total_vnodes), {});
}

std::string PrometheusMetrics::getMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;

    // Export counters
    for (const auto& [key, value] : counters_) {
        oss << key << " " << value.load() << "\n";
    }

    // Export gauges
    for (const auto& [key, value] : gauges_) {
        oss << key << " " << value.load() << "\n";
    }

    // Export histograms (simplified - just quantiles)
    for (const auto& [key, values] : histograms_) {
        if (values.empty()) continue;
        
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        
        // Calculate quantiles
        auto p50 = sorted[sorted.size() * 50 / 100];
        auto p95 = sorted[sorted.size() * 95 / 100];
        auto p99 = sorted[sorted.size() * 99 / 100];
        
        oss << key << "{quantile=\"0.5\"} " << p50 << "\n";
        oss << key << "{quantile=\"0.95\"} " << p95 << "\n";
        oss << key << "{quantile=\"0.99\"} " << p99 << "\n";
    }

    return oss.str();
}

void PrometheusMetrics::incrementCounter(const std::string& name, 
                                          const std::map<std::string, std::string>& labels) {
    std::string key = getCounterKey(name, labels);
    counters_[key]++;
}

void PrometheusMetrics::setGauge(const std::string& name, double value, 
                                  const std::map<std::string, std::string>& labels) {
    std::string key = getCounterKey(name, labels);
    gauges_[key].store(value);
}

void PrometheusMetrics::observeHistogram(const std::string& name, double value, 
                                          const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = getCounterKey(name, labels);
    histograms_[key].push_back(value);
    
    // Keep only recent values (max 1000)
    if (histograms_[key].size() > 1000) {
        histograms_[key].erase(histograms_[key].begin());
    }
}

std::string PrometheusMetrics::formatLabels(const std::map<std::string, std::string>& labels) const {
    if (labels.empty()) return "";
    
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [key, value] : labels) {
        if (!first) oss << ",";
        oss << key << "=\"" << value << "\"";
        first = false;
    }
    oss << "}";
    return oss.str();
}

std::string PrometheusMetrics::getCounterKey(const std::string& name, 
                                               const std::map<std::string, std::string>& labels) const {
    return name + formatLabels(labels);
}

} // namespace sharding
} // namespace themis
