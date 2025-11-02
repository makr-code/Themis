#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <optional>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

namespace themis {

// Simple Ranger-like Policy Engine (MVP)
// - Subject: users or wildcard "*"
// - Actions: read, write, delete, query, admin, vector.search, vector.write
// - Resources: path patterns (e.g., "/entities/users:*", "/query", "/vector/*")
// - Conditions (optional): allowed_ip_prefixes (e.g., "10.0.", "192.168."), time window (TODO)
//
// Configuration formats:
// - Supports JSON and YAML files for loading policies. Saving currently writes JSON.

class PolicyEngine {
public:
    struct Policy {
        std::string id;
        std::string name;
        std::unordered_set<std::string> subjects;   // user ids or "*"
        std::unordered_set<std::string> actions;    // e.g., "read", "write", "query", "admin", "vector.search"
        std::vector<std::string> resources;         // simple path-prefix matching (starts_with)
        bool effect_allow = true;                   // allow=true, deny=false
        // Optional conditions
        std::vector<std::string> allowed_ip_prefixes; // any match passes; empty -> ignore
        // Future: attributes/time windows
    };

    struct Decision {
        bool allowed = true;            // default allow when no policies configured
        std::string policy_id;          // matched policy id
        std::string reason;             // explanation
    };

    struct Metrics {
        std::atomic<uint64_t> policy_allow_total{0};
        std::atomic<uint64_t> policy_deny_total{0};
        std::atomic<uint64_t> policy_eval_total{0};
    };

    PolicyEngine() = default;

    // Load policies from JSON or YAML file (detected by extension)
    bool loadFromFile(const std::string& path, std::string* err = nullptr);
    // Save policies to JSON file
    bool saveToFile(const std::string& path, std::string* err = nullptr) const;

    // Replace all policies
    void setPolicies(std::vector<Policy> policies);
    // Append single policy
    void addPolicy(const Policy& p);
    // Remove by id
    bool removePolicy(const std::string& id);
    // List
    std::vector<Policy> listPolicies() const;

    // Evaluate
    Decision authorize(const std::string& user_id,
                       const std::string& action,
                       const std::string& resource_path,
                       const std::optional<std::string>& client_ip = std::nullopt) const;

    const Metrics& getMetrics() const { return metrics_; }

    // JSON helpers
    static nlohmann::json toJson(const Policy& p);
    static std::optional<Policy> fromJson(const nlohmann::json& j);

private:
    bool matchSubject(const Policy& p, const std::string& user_id) const;
    bool matchAction(const Policy& p, const std::string& action) const;
    bool matchResource(const Policy& p, const std::string& resource_path) const;
    bool matchConditions(const Policy& p, const std::optional<std::string>& client_ip) const;

    mutable std::mutex mutex_;
    std::vector<Policy> policies_;
    mutable Metrics metrics_;
};

} // namespace themis
