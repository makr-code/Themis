#include "sharding/health_check.h"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <thread>
#include <chrono>

namespace themis {
namespace sharding {

HealthCheckSystem::HealthCheckSystem(const Config& config)
    : config_(config) {
}

ShardHealthInfo HealthCheckSystem::checkShardHealth(const std::string& shard_id, 
                                                     const std::string& endpoint,
                                                     const std::string& cert_path) {
    ShardHealthInfo info;
    info.shard_id = shard_id;
    info.status = HealthStatus::HEALTHY;

    // Check certificate validity
    info.cert_valid = checkCertificateValidity(cert_path, info.cert_expiry_seconds);
    if (!info.cert_valid) {
        info.errors.push_back("Certificate invalid");
        info.status = HealthStatus::CRITICAL;
    } else if (info.cert_expiry_seconds < config_.cert_expiry_warning_days * 86400) {
        info.warnings.push_back("Certificate expires in " + std::to_string(info.cert_expiry_seconds / 86400) + " days");
        if (info.status == HealthStatus::HEALTHY) {
            info.status = HealthStatus::DEGRADED;
        }
    }

    // Check storage capacity
    info.storage_ok = checkStorageCapacity(endpoint, info.storage_usage_percent);
    if (info.storage_usage_percent >= config_.storage_critical_percent) {
        info.errors.push_back("Storage critical: " + std::to_string(info.storage_usage_percent) + "%");
        info.status = HealthStatus::CRITICAL;
    } else if (info.storage_usage_percent >= config_.storage_warning_percent) {
        info.warnings.push_back("Storage high: " + std::to_string(info.storage_usage_percent) + "%");
        if (info.status == HealthStatus::HEALTHY) {
            info.status = HealthStatus::DEGRADED;
        }
    }

    // Check network connectivity
    info.network_ok = checkNetworkConnectivity(endpoint, info.response_time_ms);
    if (!info.network_ok) {
        info.errors.push_back("Network unreachable");
        info.status = HealthStatus::CRITICAL;
    } else if (info.response_time_ms >= config_.response_time_unhealthy_ms) {
        info.errors.push_back("Response time too high: " + std::to_string(info.response_time_ms) + "ms");
        info.status = HealthStatus::UNHEALTHY;
    } else if (info.response_time_ms >= config_.response_time_degraded_ms) {
        info.warnings.push_back("Response time degraded: " + std::to_string(info.response_time_ms) + "ms");
        if (info.status == HealthStatus::HEALTHY) {
            info.status = HealthStatus::DEGRADED;
        }
    }

    return info;
}

ClusterHealthInfo HealthCheckSystem::checkClusterHealth(const std::map<std::string, std::string>& shard_endpoints) {
    ClusterHealthInfo cluster_info;
    cluster_info.total_shards = static_cast<int>(shard_endpoints.size());
    cluster_info.healthy_shards = 0;
    cluster_info.degraded_shards = 0;
    cluster_info.unhealthy_shards = 0;
    cluster_info.critical_shards = 0;

    // Check each shard
    for (const auto& [shard_id, endpoint] : shard_endpoints) {
        std::string cert_path = "/etc/themis/pki/" + shard_id + ".crt";  // Default path
        auto shard_health = checkShardHealth(shard_id, endpoint, cert_path);
        cluster_info.shard_health.push_back(shard_health);

        switch (shard_health.status) {
            case HealthStatus::HEALTHY:
                cluster_info.healthy_shards++;
                break;
            case HealthStatus::DEGRADED:
                cluster_info.degraded_shards++;
                break;
            case HealthStatus::UNHEALTHY:
                cluster_info.unhealthy_shards++;
                break;
            case HealthStatus::CRITICAL:
                cluster_info.critical_shards++;
                break;
        }
    }

    // Check quorum
    cluster_info.has_quorum = hasQuorum(cluster_info.healthy_shards, cluster_info.total_shards);
    if (!cluster_info.has_quorum) {
        cluster_info.cluster_warnings.push_back("No quorum - less than 50% shards healthy");
    }

    // Aggregate cluster status
    cluster_info.cluster_status = aggregateHealth(cluster_info.shard_health);

    return cluster_info;
}

void HealthCheckSystem::registerCallback(HealthCheckCallback callback) {
    callback_ = callback;
}

void HealthCheckSystem::startPeriodicChecks(const std::map<std::string, std::string>& shard_endpoints) {
    running_ = true;
    
    std::thread([this, shard_endpoints]() {
        while (running_) {
            auto health = checkClusterHealth(shard_endpoints);
            current_health_ = health;
            
            if (callback_) {
                callback_(health);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.check_interval_ms));
        }
    }).detach();
}

void HealthCheckSystem::stopPeriodicChecks() {
    running_ = false;
}

ClusterHealthInfo HealthCheckSystem::getCurrentHealth() const {
    return current_health_;
}

bool HealthCheckSystem::checkCertificateValidity(const std::string& cert_path, int64_t& seconds_until_expiry) {
    FILE* fp = fopen(cert_path.c_str(), "r");
    if (!fp) {
        return false;
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) {
        return false;
    }

    // Check expiration
    // Future: Calculate actual expiry time from certificate
    // ASN1_TIME* not_after = X509_get_notAfter(cert);
    // time_t now = time(nullptr);
    
    // Calculate seconds until expiry (simplified)
    seconds_until_expiry = 30 * 86400;  // Placeholder: 30 days
    
    X509_free(cert);
    return true;
}

bool HealthCheckSystem::checkStorageCapacity(const std::string& endpoint, double& usage_percent) {
    (void)endpoint; // Future: make HTTP call to shard
    // Placeholder implementation - would make HTTP call to shard
    usage_percent = 75.0;  // Mock value
    return true;
}

bool HealthCheckSystem::checkNetworkConnectivity(const std::string& endpoint, double& response_time_ms) {
    (void)endpoint; // Future: ping the endpoint
    // Placeholder implementation - would ping the endpoint
    auto start = std::chrono::steady_clock::now();
    
    // Simulate network check
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto end = std::chrono::steady_clock::now();
    response_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    return true;
}

HealthStatus HealthCheckSystem::aggregateHealth(const std::vector<ShardHealthInfo>& shard_health) {
    int critical_count = 0;
    int unhealthy_count = 0;
    int degraded_count = 0;

    for (const auto& shard : shard_health) {
        switch (shard.status) {
            case HealthStatus::CRITICAL:
                critical_count++;
                break;
            case HealthStatus::UNHEALTHY:
                unhealthy_count++;
                break;
            case HealthStatus::DEGRADED:
                degraded_count++;
                break;
            default:
                break;
        }
    }

    // Aggregate logic
    if (critical_count > 0) {
        return HealthStatus::CRITICAL;
    } else if (unhealthy_count > 0) {
        return HealthStatus::UNHEALTHY;
    } else if (degraded_count > 0) {
        return HealthStatus::DEGRADED;
    }
    
    return HealthStatus::HEALTHY;
}

bool HealthCheckSystem::hasQuorum(int healthy_shards, int total_shards) {
    if (total_shards == 0) return false;
    return healthy_shards > total_shards / 2;
}

} // namespace sharding
} // namespace themis
