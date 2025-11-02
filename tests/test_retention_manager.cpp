#include <gtest/gtest.h>
#include "utils/retention_manager.h"
#include <nlohmann/json.hpp>
#include <chrono>

using namespace vcc;
using namespace std::chrono;

class RetentionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mgr = std::make_unique<RetentionManager>();
    }

    std::unique_ptr<RetentionManager> mgr;
};

TEST_F(RetentionManagerTest, RegisterPolicy_Success) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "test_policy";
    policy.retention_period = hours(24 * 30); // 30 days
    policy.archive_after = hours(24 * 7);     // 7 days
    policy.auto_purge_enabled = true;
    policy.require_audit_trail = true;
    policy.classification_level = "offen";

    ASSERT_TRUE(mgr->registerPolicy(policy));
    
    auto policies = mgr->getPolicies();
    ASSERT_EQ(policies.size(), 1);
    EXPECT_EQ(policies[0].name, "test_policy");
    EXPECT_EQ(policies[0].retention_period, hours(24 * 30));
}

TEST_F(RetentionManagerTest, GetPolicy_ExistingPolicy) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "gdpr_policy";
    policy.retention_period = hours(24 * 365); // 1 year
    policy.archive_after = hours(24 * 30);
    policy.auto_purge_enabled = false;
    policy.require_audit_trail = true;

    mgr->registerPolicy(policy);
    
    auto* retrieved = mgr->getPolicy("gdpr_policy");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "gdpr_policy");
    EXPECT_EQ(retrieved->retention_period, hours(24 * 365));
    EXPECT_FALSE(retrieved->auto_purge_enabled);
}

TEST_F(RetentionManagerTest, GetPolicy_NonExistent_ReturnsNull) {
    auto* policy = mgr->getPolicy("nonexistent");
    EXPECT_EQ(policy, nullptr);
}

TEST_F(RetentionManagerTest, RemovePolicy_Success) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "temp_policy";
    policy.retention_period = hours(24);
    policy.archive_after = hours(12);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    ASSERT_EQ(mgr->getPolicies().size(), 1);
    
    ASSERT_TRUE(mgr->removePolicy("temp_policy"));
    EXPECT_EQ(mgr->getPolicies().size(), 0);
}

TEST_F(RetentionManagerTest, ShouldArchive_AfterArchivePeriod) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "archive_test";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7); // 7 days
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    // Entity created 10 days ago
    auto created_at = system_clock::now() - hours(24 * 10);
    
    EXPECT_TRUE(mgr->shouldArchive("entity_1", created_at, "archive_test"));
}

TEST_F(RetentionManagerTest, ShouldArchive_BeforeArchivePeriod_ReturnsFalse) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "archive_test";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    // Entity created 3 days ago
    auto created_at = system_clock::now() - hours(24 * 3);
    
    EXPECT_FALSE(mgr->shouldArchive("entity_1", created_at, "archive_test"));
}

TEST_F(RetentionManagerTest, ShouldPurge_AfterRetentionPeriod) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "purge_test";
    policy.retention_period = hours(24 * 30); // 30 days
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    // Entity created 35 days ago
    auto created_at = system_clock::now() - hours(24 * 35);
    
    EXPECT_TRUE(mgr->shouldPurge("entity_1", created_at, "purge_test"));
}

TEST_F(RetentionManagerTest, ShouldPurge_BeforeRetentionPeriod_ReturnsFalse) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "purge_test";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    // Entity created 20 days ago
    auto created_at = system_clock::now() - hours(24 * 20);
    
    EXPECT_FALSE(mgr->shouldPurge("entity_1", created_at, "purge_test"));
}

TEST_F(RetentionManagerTest, ShouldPurge_AutoPurgeDisabled_ReturnsFalse) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "no_purge";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = false; // Disabled

    mgr->registerPolicy(policy);
    
    // Entity created 35 days ago (past retention)
    auto created_at = system_clock::now() - hours(24 * 35);
    
    EXPECT_FALSE(mgr->shouldPurge("entity_1", created_at, "no_purge"));
}

TEST_F(RetentionManagerTest, ArchiveEntity_RecordsAction) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "archive_policy";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    bool handler_called = false;
    auto archive_handler = [&handler_called](const std::string& entity_id) -> bool {
        handler_called = true;
        return true;
    };
    
    auto action = mgr->archiveEntity("entity_123", "archive_policy", archive_handler);
    
    EXPECT_TRUE(action.success);
    EXPECT_TRUE(handler_called);
    EXPECT_EQ(action.entity_id, "entity_123");
    EXPECT_EQ(action.action, "archived");
    EXPECT_EQ(action.policy_name, "archive_policy");
}

TEST_F(RetentionManagerTest, PurgeEntity_RecordsAction) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "purge_policy";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    bool handler_called = false;
    auto purge_handler = [&handler_called](const std::string& entity_id) -> bool {
        handler_called = true;
        return true;
    };
    
    auto action = mgr->purgeEntity("entity_456", "purge_policy", purge_handler);
    
    EXPECT_TRUE(action.success);
    EXPECT_TRUE(handler_called);
    EXPECT_EQ(action.entity_id, "entity_456");
    EXPECT_EQ(action.action, "purged");
    EXPECT_EQ(action.policy_name, "purge_policy");
}

TEST_F(RetentionManagerTest, RunRetentionCheck_ProcessesEntities) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "check_policy";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    // Mock entity provider
    auto entity_provider = [](const std::string& policy_name) 
        -> std::vector<std::pair<std::string, system_clock::time_point>> {
        std::vector<std::pair<std::string, system_clock::time_point>> entities;
        
        // Entity 1: 10 days old (should archive)
        entities.emplace_back("entity_1", system_clock::now() - hours(24 * 10));
        
        // Entity 2: 40 days old (should purge)
        entities.emplace_back("entity_2", system_clock::now() - hours(24 * 40));
        
        // Entity 3: 3 days old (retain)
        entities.emplace_back("entity_3", system_clock::now() - hours(24 * 3));
        
        return entities;
    };
    
    int archive_count = 0;
    int purge_count = 0;
    
    auto archive_handler = [&archive_count](const std::string&) -> bool {
        archive_count++;
        return true;
    };
    
    auto purge_handler = [&purge_count](const std::string&) -> bool {
        purge_count++;
        return true;
    };
    
    auto stats = mgr->runRetentionCheck(entity_provider, archive_handler, purge_handler);
    
    EXPECT_EQ(stats.total_entities_scanned, 3);
    EXPECT_EQ(stats.archived_count, 1);  // entity_1
    EXPECT_EQ(stats.purged_count, 1);    // entity_2
    EXPECT_EQ(stats.retained_count, 1);  // entity_3
    EXPECT_EQ(stats.error_count, 0);
}

TEST_F(RetentionManagerTest, GetHistory_ReturnsRecentActions) {
    RetentionManager::RetentionPolicy policy;
    policy.name = "history_test";
    policy.retention_period = hours(24 * 30);
    policy.archive_after = hours(24 * 7);
    policy.auto_purge_enabled = true;

    mgr->registerPolicy(policy);
    
    auto archive_handler = [](const std::string&) -> bool { return true; };
    auto purge_handler = [](const std::string&) -> bool { return true; };
    
    mgr->archiveEntity("entity_1", "history_test", archive_handler);
    mgr->purgeEntity("entity_2", "history_test", purge_handler);
    
    auto history = mgr->getHistory(10);
    
    ASSERT_EQ(history.size(), 2);
    EXPECT_EQ(history[0].action, "archived");
    EXPECT_EQ(history[1].action, "purged");
}

TEST_F(RetentionManagerTest, LoadPolicies_FromYAML) {
    // Use the example retention_policies.yaml
    bool loaded = mgr->loadPolicies("./config/retention_policies.yaml");
    
    if (loaded) {
        auto policies = mgr->getPolicies();
        EXPECT_GT(policies.size(), 0);
        
        // Check if user_personal_data policy exists
        auto* user_policy = mgr->getPolicy("user_personal_data");
        if (user_policy) {
            EXPECT_EQ(user_policy->classification_level, "geheim");
            EXPECT_TRUE(user_policy->require_audit_trail);
        }
    } else {
        // File might not exist in test environment
        GTEST_SKIP() << "retention_policies.yaml not found, skipping load test";
    }
}
