#include <gtest/gtest.h>
#include "server/retention_api_handler.h"
#include "utils/retention_manager.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class RetentionApiHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        retention_mgr = std::make_shared<vcc::RetentionManager>();
        handler = std::make_unique<themis::server::RetentionApiHandler>(retention_mgr);
        
        // Create a test policy
        vcc::RetentionManager::RetentionPolicy test_policy;
        test_policy.name = "test_gdpr";
        test_policy.retention_period = std::chrono::seconds(30 * 86400); // 30 days
        test_policy.archive_after = std::chrono::seconds(15 * 86400);    // 15 days
        test_policy.auto_purge_enabled = false;
        test_policy.require_audit_trail = true;
        test_policy.classification_level = "offen";
        
        retention_mgr->registerPolicy(test_policy);
    }

    std::shared_ptr<vcc::RetentionManager> retention_mgr;
    std::unique_ptr<themis::server::RetentionApiHandler> handler;
};

TEST_F(RetentionApiHandlerTest, ListPolicies_ReturnsAll) {
    themis::server::RetentionQueryFilter filter;
    filter.page = 1;
    filter.page_size = 100;
    
    auto result = handler->listPolicies(filter);
    
    ASSERT_TRUE(result.contains("items"));
    ASSERT_TRUE(result.contains("total"));
    EXPECT_EQ(result["total"].get<int>(), 1);
    
    auto items = result["items"];
    ASSERT_EQ(items.size(), 1);
    EXPECT_EQ(items[0]["name"].get<std::string>(), "test_gdpr");
    EXPECT_EQ(items[0]["retention_period_days"].get<int>(), 30);
}

TEST_F(RetentionApiHandlerTest, ListPolicies_FilterByName) {
    themis::server::RetentionQueryFilter filter;
    filter.name_filter = "gdpr";
    filter.page = 1;
    filter.page_size = 100;
    
    auto result = handler->listPolicies(filter);
    EXPECT_EQ(result["total"].get<int>(), 1);
    
    // Non-matching filter
    filter.name_filter = "nonexistent";
    result = handler->listPolicies(filter);
    EXPECT_EQ(result["total"].get<int>(), 0);
}

TEST_F(RetentionApiHandlerTest, CreatePolicy_Success) {
    json policy_json = {
        {"name", "new_policy"},
        {"retention_period_days", 60},
        {"archive_after_days", 30},
        {"auto_purge_enabled", true},
        {"require_audit_trail", true},
        {"classification_level", "vs-nfd"}
    };
    
    auto result = handler->createOrUpdatePolicy(policy_json);
    
    ASSERT_TRUE(result.contains("status"));
    EXPECT_EQ(result["status"].get<std::string>(), "created");
    EXPECT_EQ(result["name"].get<std::string>(), "new_policy");
    
    // Verify policy was created
    themis::server::RetentionQueryFilter filter;
    auto list = handler->listPolicies(filter);
    EXPECT_EQ(list["total"].get<int>(), 2);
}

TEST_F(RetentionApiHandlerTest, UpdatePolicy_Success) {
    json policy_json = {
        {"name", "test_gdpr"},
        {"retention_period_days", 90}, // Changed
        {"archive_after_days", 45},
        {"auto_purge_enabled", true},
        {"require_audit_trail", true},
        {"classification_level", "offen"}
    };
    
    auto result = handler->createOrUpdatePolicy(policy_json);
    
    ASSERT_TRUE(result.contains("status"));
    EXPECT_EQ(result["status"].get<std::string>(), "updated");
    
    // Verify update
    auto policy = retention_mgr->getPolicy("test_gdpr");
    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(policy->retention_period.count(), 90 * 86400);
}

TEST_F(RetentionApiHandlerTest, DeletePolicy_Success) {
    auto result = handler->deletePolicy("test_gdpr");
    
    ASSERT_TRUE(result.contains("status"));
    EXPECT_EQ(result["status"].get<std::string>(), "deleted");
    
    // Verify deletion
    themis::server::RetentionQueryFilter filter;
    auto list = handler->listPolicies(filter);
    EXPECT_EQ(list["total"].get<int>(), 0);
}

TEST_F(RetentionApiHandlerTest, DeletePolicy_NotFound) {
    auto result = handler->deletePolicy("nonexistent");
    
    ASSERT_TRUE(result.contains("status"));
    EXPECT_EQ(result["status"].get<std::string>(), "error");
}

TEST_F(RetentionApiHandlerTest, GetHistory_ReturnsRecentActions) {
    auto result = handler->getHistory(100);
    
    ASSERT_TRUE(result.contains("items"));
    ASSERT_TRUE(result.contains("total"));
    // Initially empty since no retention operations have been performed
    EXPECT_EQ(result["total"].get<size_t>(), 0);
}

TEST_F(RetentionApiHandlerTest, GetPolicyStats_ReturnsStats) {
    auto result = handler->getPolicyStats("test_gdpr");
    
    ASSERT_TRUE(result.contains("policy_name"));
    EXPECT_EQ(result["policy_name"].get<std::string>(), "test_gdpr");
    ASSERT_TRUE(result.contains("total_scanned"));
    ASSERT_TRUE(result.contains("archived"));
    ASSERT_TRUE(result.contains("purged"));
    
    // Initially all zero
    EXPECT_EQ(result["total_scanned"].get<size_t>(), 0);
    EXPECT_EQ(result["archived"].get<size_t>(), 0);
    EXPECT_EQ(result["purged"].get<size_t>(), 0);
}

TEST_F(RetentionApiHandlerTest, CreatePolicy_InvalidJSON) {
    json policy_json = {
        {"name", "invalid_policy"}
        // Missing required retention_period_days
    };
    
    auto result = handler->createOrUpdatePolicy(policy_json);
    
    ASSERT_TRUE(result.contains("status"));
    EXPECT_EQ(result["status"].get<std::string>(), "error");
    ASSERT_TRUE(result.contains("error"));
}

TEST_F(RetentionApiHandlerTest, Pagination_Works) {
    // Create multiple policies
    for (int i = 1; i <= 5; ++i) {
        json policy_json = {
            {"name", "policy_" + std::to_string(i)},
            {"retention_period_days", 30},
            {"archive_after_days", 15}
        };
        handler->createOrUpdatePolicy(policy_json);
    }
    
    // Test pagination
    themis::server::RetentionQueryFilter filter;
    filter.page = 1;
    filter.page_size = 2;
    
    auto result = handler->listPolicies(filter);
    EXPECT_EQ(result["total"].get<int>(), 6); // 5 new + 1 test_gdpr
    EXPECT_EQ(result["items"].size(), 2);     // Page size
    
    // Second page
    filter.page = 2;
    result = handler->listPolicies(filter);
    EXPECT_EQ(result["items"].size(), 2);
}
