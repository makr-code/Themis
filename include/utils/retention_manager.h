#pragma once

#include <string>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace vcc {

/**
 * @brief Retention policies for compliance (DSGVO, eIDAS, etc.)
 * 
 * Manages automatic archiving and purging of sensitive data based on
 * configurable retention periods and legal requirements.
 */
class RetentionManager {
public:
    /**
     * @brief Retention policy configuration
     */
    struct RetentionPolicy {
        std::string name;                           // Policy identifier (e.g., "gdpr_personal_data")
        std::chrono::seconds retention_period;      // How long to keep data
        std::chrono::seconds archive_after;         // When to move to archive storage
        bool auto_purge_enabled;                    // Automatically delete after retention_period
        bool require_audit_trail;                   // Log all retention actions
        std::string classification_level;           // Classification: "geheim", "vs-nfd", "offen"
        nlohmann::json metadata;                    // Additional policy-specific metadata
    };

    /**
     * @brief Retention action result
     */
    struct RetentionAction {
        std::string entity_id;                      // Affected entity/document ID
        std::string action;                         // "archived" | "purged" | "retained"
        std::string policy_name;                    // Applied policy
        std::chrono::system_clock::time_point timestamp;
        bool success;                               // Action succeeded
        std::string error_message;                  // If !success
    };

    /**
     * @brief Statistics for retention operations
     */
    struct RetentionStats {
        size_t total_entities_scanned;
        size_t archived_count;
        size_t purged_count;
        size_t retained_count;
        size_t error_count;
        std::chrono::milliseconds duration;
    };

    /**
     * @brief Construct retention manager
     * @param config_path Path to retention policies configuration (YAML/JSON)
     */
    explicit RetentionManager(const std::string& config_path = "");

    /**
     * @brief Register a retention policy
     * @param policy Policy configuration
     * @return true if registered successfully
     */
    bool registerPolicy(const RetentionPolicy& policy);

    /**
     * @brief Remove a retention policy
     * @param policy_name Name of policy to remove
     * @return true if removed successfully
     */
    bool removePolicy(const std::string& policy_name);

    /**
     * @brief Get all registered policies
     */
    std::vector<RetentionPolicy> getPolicies() const;

    /**
     * @brief Get a specific policy by name
     */
    const RetentionPolicy* getPolicy(const std::string& policy_name) const;

    /**
     * @brief Check if an entity should be archived
     * @param entity_id Entity identifier
     * @param created_at Entity creation timestamp
     * @param policy_name Policy to check against
     * @return true if entity should be archived
     */
    bool shouldArchive(
        const std::string& entity_id,
        std::chrono::system_clock::time_point created_at,
        const std::string& policy_name) const;

    /**
     * @brief Check if an entity should be purged
     * @param entity_id Entity identifier
     * @param created_at Entity creation timestamp
     * @param policy_name Policy to check against
     * @return true if entity should be purged
     */
    bool shouldPurge(
        const std::string& entity_id,
        std::chrono::system_clock::time_point created_at,
        const std::string& policy_name) const;

    /**
     * @brief Archive an entity (move to cold storage)
     * @param entity_id Entity to archive
     * @param policy_name Policy governing the archiving
     * @param archive_handler Custom archive implementation
     * @return RetentionAction result
     */
    RetentionAction archiveEntity(
        const std::string& entity_id,
        const std::string& policy_name,
        std::function<bool(const std::string&)> archive_handler);

    /**
     * @brief Purge an entity (permanently delete)
     * @param entity_id Entity to purge
     * @param policy_name Policy governing the purge
     * @param purge_handler Custom purge implementation
     * @return RetentionAction result
     */
    RetentionAction purgeEntity(
        const std::string& entity_id,
        const std::string& policy_name,
        std::function<bool(const std::string&)> purge_handler);

    /**
     * @brief Run retention check for all entities
     * @param entity_provider Callback that provides entities to check: (policy_name) -> vector<pair<entity_id, created_at>>
     * @param archive_handler Archive implementation
     * @param purge_handler Purge implementation
     * @return Statistics for the run
     */
    RetentionStats runRetentionCheck(
        std::function<std::vector<std::pair<std::string, std::chrono::system_clock::time_point>>(const std::string&)> entity_provider,
        std::function<bool(const std::string&)> archive_handler,
        std::function<bool(const std::string&)> purge_handler);

    /**
     * @brief Get retention actions history
     * @param limit Maximum number of actions to return (0 = all)
     * @return Recent retention actions
     */
    std::vector<RetentionAction> getHistory(size_t limit = 100) const;

    /**
     * @brief Get statistics for a specific policy
     */
    RetentionStats getPolicyStats(const std::string& policy_name) const;

    /**
     * @brief Load policies from configuration file
     * @param config_path Path to config file
     * @return true if loaded successfully
     */
    bool loadPolicies(const std::string& config_path);

    /**
     * @brief Get last error message
     */
    std::string getLastError() const { return last_error_; }

private:
    std::map<std::string, RetentionPolicy> policies_;
    std::vector<RetentionAction> action_history_;
    std::map<std::string, RetentionStats> policy_stats_;
    std::string last_error_;
    bool audit_enabled_;

    void logAction(const RetentionAction& action);
};

} // namespace vcc
