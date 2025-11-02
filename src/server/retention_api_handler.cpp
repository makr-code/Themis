#include "server/retention_api_handler.h"

#include <algorithm>
#include <spdlog/spdlog.h>

using nlohmann::json;

namespace themis { namespace server {

RetentionApiHandler::RetentionApiHandler(std::shared_ptr<vcc::RetentionManager> retention_manager)
    : retention_manager_(std::move(retention_manager))
{
    if (!retention_manager_) {
        // Create default instance if none provided
        retention_manager_ = std::make_shared<vcc::RetentionManager>();
        spdlog::info("RetentionApiHandler: Created default RetentionManager instance");
    }
}

json RetentionApiHandler::listPolicies(const RetentionQueryFilter& filter) {
    auto all_policies = retention_manager_->getPolicies();

    // Apply filters
    std::vector<vcc::RetentionManager::RetentionPolicy> filtered;
    filtered.reserve(all_policies.size());
    
    for (const auto& policy : all_policies) {
        bool matches = true;
        
        // Name filter (substring match)
        if (!filter.name_filter.empty()) {
            if (policy.name.find(filter.name_filter) == std::string::npos) {
                matches = false;
            }
        }
        
        // Classification filter
        if (!filter.classification_filter.empty()) {
            if (policy.classification_level != filter.classification_filter) {
                matches = false;
            }
        }
        
        if (matches) {
            filtered.push_back(policy);
        }
    }

    // Pagination
    int total = static_cast<int>(filtered.size());
    int page = std::max(1, filter.page);
    int page_size = std::max(1, std::min(1000, filter.page_size)); // Cap at 1000
    int start = (page - 1) * page_size;
    int end = std::min(start + page_size, total);

    json items = json::array();
    if (start < total) {
        for (int i = start; i < end; ++i) {
            items.push_back(policyToJson(filtered[static_cast<size_t>(i)]));
        }
    }

    return json{
        {"items", items},
        {"total", total},
        {"page", page},
        {"page_size", page_size}
    };
}

json RetentionApiHandler::createOrUpdatePolicy(const json& policy_json) {
    try {
        auto policy = jsonToPolicy(policy_json);
        
        // Check if policy already exists
        bool exists = (retention_manager_->getPolicy(policy.name) != nullptr);
        
        if (!retention_manager_->registerPolicy(policy)) {
            return json{
                {"status", "error"},
                {"error", retention_manager_->getLastError()}
            };
        }
        
        spdlog::info("RetentionApiHandler: {} policy '{}'", 
                     exists ? "Updated" : "Created", policy.name);
        
        return json{
            {"status", exists ? "updated" : "created"},
            {"name", policy.name}
        };
    }
    catch (const std::exception& e) {
        return json{
            {"status", "error"},
            {"error", std::string("Invalid policy JSON: ") + e.what()}
        };
    }
}

json RetentionApiHandler::deletePolicy(const std::string& policy_name) {
    if (!retention_manager_->removePolicy(policy_name)) {
        return json{
            {"status", "error"},
            {"error", "Policy not found or could not be deleted"}
        };
    }
    
    spdlog::info("RetentionApiHandler: Deleted policy '{}'", policy_name);
    
    return json{
        {"status", "deleted"},
        {"name", policy_name}
    };
}

json RetentionApiHandler::getHistory(size_t limit) {
    auto actions = retention_manager_->getHistory(limit);
    
    json items = json::array();
    for (const auto& action : actions) {
        items.push_back(actionToJson(action));
    }
    
    return json{
        {"items", items},
        {"total", actions.size()},
        {"limit", limit}
    };
}

json RetentionApiHandler::getPolicyStats(const std::string& policy_name) {
    auto stats = retention_manager_->getPolicyStats(policy_name);
    
    return json{
        {"policy_name", policy_name},
        {"total_scanned", stats.total_entities_scanned},
        {"archived", stats.archived_count},
        {"purged", stats.purged_count},
        {"retained", stats.retained_count},
        {"errors", stats.error_count},
        {"duration_ms", stats.duration.count()}
    };
}

// Helper methods

json RetentionApiHandler::policyToJson(const vcc::RetentionManager::RetentionPolicy& policy) {
    return json{
        {"name", policy.name},
        {"retention_period_days", policy.retention_period.count() / 86400},
        {"archive_after_days", policy.archive_after.count() / 86400},
        {"auto_purge_enabled", policy.auto_purge_enabled},
        {"require_audit_trail", policy.require_audit_trail},
        {"classification_level", policy.classification_level},
        {"metadata", policy.metadata}
    };
}

vcc::RetentionManager::RetentionPolicy RetentionApiHandler::jsonToPolicy(const json& j) {
    vcc::RetentionManager::RetentionPolicy policy;
    
    policy.name = j.at("name").get<std::string>();
    
    // Parse retention_period (days -> seconds)
    int retention_days = j.at("retention_period_days").get<int>();
    policy.retention_period = std::chrono::seconds(retention_days * 86400);
    
    // Parse archive_after (optional, default to retention_period / 2)
    if (j.contains("archive_after_days")) {
        int archive_days = j["archive_after_days"].get<int>();
        policy.archive_after = std::chrono::seconds(archive_days * 86400);
    } else {
        policy.archive_after = policy.retention_period / 2;
    }
    
    // Optional fields
    policy.auto_purge_enabled = j.value("auto_purge_enabled", false);
    policy.require_audit_trail = j.value("require_audit_trail", true);
    policy.classification_level = j.value("classification_level", "offen");
    
    if (j.contains("metadata")) {
        policy.metadata = j["metadata"];
    }
    
    return policy;
}

json RetentionApiHandler::actionToJson(const vcc::RetentionManager::RetentionAction& action) {
    // Convert timestamp to ISO 8601 string
    auto timestamp_t = std::chrono::system_clock::to_time_t(action.timestamp);
    std::tm tm;
    #ifdef _WIN32
    localtime_s(&tm, &timestamp_t);
    #else
    localtime_r(&timestamp_t, &tm);
    #endif
    
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    
    json result = json{
        {"entity_id", action.entity_id},
        {"action", action.action},
        {"policy_name", action.policy_name},
        {"timestamp", std::string(buf)},
        {"success", action.success}
    };
    
    if (!action.success) {
        result["error"] = action.error_message;
    }
    
    return result;
}

}} // namespace themis::server
