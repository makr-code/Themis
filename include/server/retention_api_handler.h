#pragma once

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "utils/retention_manager.h"

namespace themis { namespace server {

/**
 * @brief Query filter for retention policies
 */
struct RetentionQueryFilter {
    std::string name_filter;           // Filter by policy name (substring match)
    std::string classification_filter; // Filter by classification level
    int page = 1;
    int page_size = 100;
};

/**
 * @brief Retention Admin API Handler
 * 
 * Provides REST endpoints for managing retention policies:
 * - GET /api/retention/policies - List all policies
 * - POST /api/retention/policies - Create/update policy
 * - DELETE /api/retention/policies/{name} - Delete policy
 * - GET /api/retention/history - Get retention actions history
 */
class RetentionApiHandler {
public:
    /**
     * @brief Initialize with retention manager instance
     * @param retention_manager Shared retention manager (optional; creates default if null)
     */
    explicit RetentionApiHandler(std::shared_ptr<vcc::RetentionManager> retention_manager = nullptr);

    /**
     * @brief List retention policies with optional filtering and pagination
     * @param filter Query filter
     * @return JSON response: { "items": [...], "total": N, "page": P, "page_size": S }
     */
    nlohmann::json listPolicies(const RetentionQueryFilter& filter);

    /**
     * @brief Create or update a retention policy
     * @param policy_json JSON representation of RetentionPolicy
     * @return JSON response: { "status": "created"|"updated", "name": "..." }
     */
    nlohmann::json createOrUpdatePolicy(const nlohmann::json& policy_json);

    /**
     * @brief Delete a retention policy by name
     * @param policy_name Name of policy to delete
     * @return JSON response: { "status": "deleted", "name": "..." }
     */
    nlohmann::json deletePolicy(const std::string& policy_name);

    /**
     * @brief Get retention actions history
     * @param limit Maximum number of actions to return (0 = all)
     * @return JSON response: { "items": [...], "total": N }
     */
    nlohmann::json getHistory(size_t limit = 100);

    /**
     * @brief Get statistics for a specific policy
     * @param policy_name Name of policy
     * @return JSON response with policy stats
     */
    nlohmann::json getPolicyStats(const std::string& policy_name);

private:
    std::shared_ptr<vcc::RetentionManager> retention_manager_;

    // Helper: convert RetentionPolicy to JSON
    nlohmann::json policyToJson(const vcc::RetentionManager::RetentionPolicy& policy);

    // Helper: parse JSON to RetentionPolicy
    vcc::RetentionManager::RetentionPolicy jsonToPolicy(const nlohmann::json& j);

    // Helper: convert RetentionAction to JSON
    nlohmann::json actionToJson(const vcc::RetentionManager::RetentionAction& action);
};

}} // namespace themis::server
