#include <gtest/gtest.h>
#include "sharding/rebalance_operation.h"
#include "sharding/data_migrator.h"
#include <thread>
#include <chrono>

#ifdef _MSC_VER
// Temporär deaktiviert unter MSVC, bis Sharding-APIs plattformweit angeglichen sind
TEST(RebalanceOperationTest, DisabledOnMSVC) { GTEST_SKIP() << "Rebalance/DataMigrator tests disabled on MSVC temporarily."; }
#else

using namespace themis::sharding;

// ============================================================================
// Rebalance Operation Tests
// ============================================================================

TEST(RebalanceOperationTest, InitialState) {
    RebalanceOperationConfig config{
        .source_shard_id = "shard_001",
        .target_shard_id = "shard_002",
        .token_range_start = 0,
        .token_range_end = 1000000,
        .operator_cert_path = "/etc/themis/pki/operator.crt",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt"
    };
    
    RebalanceOperation rebalance(config);
    EXPECT_EQ(rebalance.getState(), RebalanceState::PLANNED);
}

TEST(RebalanceOperationTest, InvalidConfig) {
    RebalanceOperationConfig config{
        .source_shard_id = "",
        .target_shard_id = "shard_002",
        .token_range_start = 0,
        .token_range_end = 1000000
    };

    EXPECT_THROW({ RebalanceOperation r(config); }, std::invalid_argument);
}

TEST(RebalanceOperationTest, InvalidTokenRange) {
    RebalanceOperationConfig config{
        .source_shard_id = "shard_001",
        .target_shard_id = "shard_002",
        .token_range_start = 1000000,
        .token_range_end = 0  // Invalid: start > end
    };

    EXPECT_THROW({ RebalanceOperation r(config); }, std::invalid_argument);
}

TEST(RebalanceOperationTest, StartWithValidSignature) {
    RebalanceOperationConfig config{
        .source_shard_id = "shard_001",
        .target_shard_id = "shard_002",
        .token_range_start = 0,
        .token_range_end = 1000000,
        .operator_cert_path = "/etc/themis/pki/operator.crt",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt"
    };
    
    RebalanceOperation rebalance(config);
    
    // Start with operator signature (placeholder)
    std::string operator_sig = "valid_signature";
    EXPECT_TRUE(rebalance.start(operator_sig));
    EXPECT_EQ(rebalance.getState(), RebalanceState::IN_PROGRESS);
}

TEST(RebalanceOperationTest, StartWithInvalidSignature) {
    RebalanceOperationConfig config{
        .source_shard_id = "shard_001",
        .target_shard_id = "shard_002",
        .token_range_start = 0,
        .token_range_end = 1000000,
        .operator_cert_path = "/etc/themis/pki/operator.crt",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt"
    };
    
    RebalanceOperation rebalance(config);
    
    // Start with empty signature (invalid)
    std::string operator_sig = "";
    EXPECT_FALSE(rebalance.start(operator_sig));
    EXPECT_EQ(rebalance.getState(), RebalanceState::PLANNED);
}

TEST(RebalanceOperationTest, ProgressTracking) {
    RebalanceOperationConfig config{
        .source_shard_id = "shard_001",
        .target_shard_id = "shard_002",
        .token_range_start = 0,
        .token_range_end = 1000000,
        .operator_cert_path = "/etc/themis/pki/operator.crt",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt"
    };
    
    RebalanceOperation rebalance(config);
    rebalance.start("valid_signature");
    
    // Update progress
    rebalance.updateProgress(500, 1024 * 1024);
    
    auto progress = rebalance.getProgress();
    EXPECT_EQ(progress.records_migrated, 500);
    EXPECT_EQ(progress.bytes_transferred, 1024 * 1024);
}

TEST(RebalanceOperationTest, ProgressCallback) {
    RebalanceOperationConfig config{
        .source_shard_id = "shard_001",
        .target_shard_id = "shard_002",
        .token_range_start = 0,
        .token_range_end = 1000000,
        .operator_cert_path = "/etc/themis/pki/operator.crt",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt"
    };
    
    RebalanceOperation rebalance(config);
    
    bool callback_invoked = false;
    rebalance.setProgressCallback([&](const RebalanceProgress& progress) {
        callback_invoked = true;
        EXPECT_GT(progress.records_migrated, 0);
    });
    
    rebalance.start("valid_signature");
    rebalance.updateProgress(100, 1024);
    
    EXPECT_TRUE(callback_invoked);
}

// ============================================================================
// Data Migrator Tests
// ============================================================================

TEST(DataMigratorTest, Configuration) {
    DataMigratorConfig config{
        .source_endpoint = "https://shard-001:8080",
        .target_endpoint = "https://shard-002:8080",
        .cert_path = "/etc/themis/pki/migrator.crt",
        .key_path = "/etc/themis/pki/migrator.key",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt",
        .batch_size = 1000,
        .verify_integrity = true
    };
    
    EXPECT_NO_THROW({ DataMigrator m(config); });
}

TEST(DataMigratorTest, InvalidConfiguration) {
    DataMigratorConfig config{
        .source_endpoint = "",  // Invalid: empty
        .target_endpoint = "https://shard-002:8080",
        .batch_size = 1000
    };

    EXPECT_THROW({ DataMigrator m(config); }, std::invalid_argument);
}

TEST(DataMigratorTest, InvalidBatchSize) {
    DataMigratorConfig config{
        .source_endpoint = "https://shard-001:8080",
        .target_endpoint = "https://shard-002:8080",
        .batch_size = 0  // Invalid: must be > 0
    };

    EXPECT_THROW({ DataMigrator m(config); }, std::invalid_argument);
}

TEST(DataMigratorTest, MigrationFlow) {
    DataMigratorConfig config{
        .source_endpoint = "https://shard-001:8080",
        .target_endpoint = "https://shard-002:8080",
        .cert_path = "/etc/themis/pki/migrator.crt",
        .key_path = "/etc/themis/pki/migrator.key",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt",
        .batch_size = 10,
        .verify_integrity = false  // Disable for test
    };
    
    DataMigrator migrator(config);
    
    auto result = migrator.migrate(
        "shard_001",
        "shard_002",
        0,
        1000000,
        nullptr  // No progress callback
    );
    
    EXPECT_TRUE(result.success);
    EXPECT_GT(result.records_migrated, 0);
}

TEST(DataMigratorTest, MigrationWithProgressCallback) {
    DataMigratorConfig config{
        .source_endpoint = "https://shard-001:8080",
        .target_endpoint = "https://shard-002:8080",
        .cert_path = "/etc/themis/pki/migrator.crt",
        .key_path = "/etc/themis/pki/migrator.key",
        .ca_cert_path = "/etc/themis/pki/root-ca.crt",
        .batch_size = 10,
        .verify_integrity = false
    };
    
    DataMigrator migrator(config);
    
    bool callback_invoked = false;
    uint64_t last_records = 0;
    
    auto result = migrator.migrate(
        "shard_001",
        "shard_002",
        0,
        1000000,
        [&](const MigrationProgress& progress) {
            callback_invoked = true;
            EXPECT_GE(progress.records_migrated, last_records);
            last_records = progress.records_migrated;
        }
    );
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(callback_invoked);
}

#endif // _MSC_VER
