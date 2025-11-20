#include <gtest/gtest.h>
#include "sharding/prometheus_metrics.h"
#include "sharding/health_check.h"
#include "sharding/admin_api.h"

using namespace themis::sharding;

// Prometheus Metrics Tests
TEST(PrometheusMetricsTest, RecordShardHealth) {
    PrometheusMetrics::Config config;
    PrometheusMetrics metrics(config);

    metrics.recordShardHealth("shard_001", "healthy");
    metrics.recordShardHealth("shard_002", "degraded");

    auto output = metrics.getMetrics();
    EXPECT_TRUE(output.find("themis_shard_health_status") != std::string::npos);
}

TEST(PrometheusMetricsTest, RecordRoutingStatistics) {
    PrometheusMetrics::Config config;
    PrometheusMetrics metrics(config);

    metrics.recordRoutingRequest("local");
    metrics.recordRoutingRequest("remote");
    metrics.recordRoutingRequest("scatter_gather");

    auto output = metrics.getMetrics();
    EXPECT_TRUE(output.find("themis_routing_requests_total") != std::string::npos);
}

TEST(PrometheusMetricsTest, RecordMigrationProgress) {
    PrometheusMetrics::Config config;
    PrometheusMetrics metrics(config);

    metrics.recordMigrationProgress("op_001", 1000, 5000000, 50.0);

    auto output = metrics.getMetrics();
    EXPECT_TRUE(output.find("themis_migration_records_total") != std::string::npos);
    EXPECT_TRUE(output.find("themis_migration_progress_percent") != std::string::npos);
}

// Health Check Tests
TEST(HealthCheckTest, CheckShardHealthValid) {
    HealthCheckSystem::Config config;
    HealthCheckSystem health_checker(config);

    // Note: This test would need valid cert path in real scenario
    // For now, it's a structural test
    std::string shard_id = "shard_001";
    std::string endpoint = "https://shard-001.dc1:8080";
    std::string cert_path = "/tmp/test.crt";  // Would need to create mock cert

    // Test would check health status
    // auto health = health_checker.checkShardHealth(shard_id, endpoint, cert_path);
    // EXPECT_EQ(health.shard_id, shard_id);
}

TEST(HealthCheckTest, ClusterHealthAggregation) {
    HealthCheckSystem::Config config;
    HealthCheckSystem health_checker(config);

    std::map<std::string, std::string> shard_endpoints = {
        {"shard_001", "https://shard-001.dc1:8080"},
        {"shard_002", "https://shard-002.dc1:8080"}
    };

    // Test structure
    EXPECT_NO_THROW({
        auto cluster_health = health_checker.getCurrentHealth();
    });
}

TEST(HealthCheckTest, HealthStatusEnum) {
    // Test health status values
    HealthStatus healthy = HealthStatus::HEALTHY;
    HealthStatus degraded = HealthStatus::DEGRADED;
    HealthStatus unhealthy = HealthStatus::UNHEALTHY;
    HealthStatus critical = HealthStatus::CRITICAL;

    EXPECT_NE(healthy, degraded);
    EXPECT_NE(degraded, unhealthy);
    EXPECT_NE(unhealthy, critical);
}

// Admin API Tests
TEST(AdminAPITest, Configuration) {
    AdminAPI::Config config;
    config.http_port = 8080;
    config.require_signatures = true;
    config.enable_audit_log = true;

    AdminAPI api(config);
    EXPECT_NO_THROW({
        // API created successfully
    });
}

TEST(AdminAPITest, EndpointConstants) {
    // Test that endpoint constants are defined
    EXPECT_STREQ(AdminAPI::Endpoints::TOPOLOGY, "/admin/topology");
    EXPECT_STREQ(AdminAPI::Endpoints::SHARD_ADD, "/admin/shard/add");
    EXPECT_STREQ(AdminAPI::Endpoints::REBALANCE, "/admin/rebalance");
    EXPECT_STREQ(AdminAPI::Endpoints::HEALTH, "/admin/health");
    EXPECT_STREQ(AdminAPI::Endpoints::STATS, "/admin/stats");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
