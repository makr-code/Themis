#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <memory>

namespace themis {
namespace utils {
    class AuditLogger;
}

namespace governance {

struct ClassificationProfile {
    std::string level;  // offen, vs-nfd, geheim, streng-geheim
    bool encryption_required = false;
    bool ann_allowed = true;
    bool export_allowed = true;
    bool cache_allowed = true;
    std::string redaction_level = "standard";
    int retention_days = 365;
    bool log_encryption = false;
};

struct PolicyDecision {
    // Normalized classification: "offen", "vs-nfd", "geheim", "streng-geheim"
    std::string classification;
    // Mode: "enforce" | "observe"
    std::string mode;
    // Whether logs must be encrypted (Encrypt-then-Sign handled by logger)
    bool encrypt_logs = true;
    // Redaction profile: "none" | "standard" | "strict"
    std::string redaction = "standard";

    // Derived, route-relevant decisions
    bool ann_allowed = true;                 // Approximate NN allowed
    bool require_content_encryption = false; // Content blobs must be encrypted
    bool export_allowed = true;
    bool cache_allowed = true;
    int retention_days = 365;
};

class PolicyEngine {
public:
    PolicyEngine() = default;

    // Load policies from YAML file (returns false on error)
    bool loadFromYAML(const std::string& yaml_path);

    // Set audit logger for automatic logging of policy evaluations
    void setAuditLogger(std::shared_ptr<themis::utils::AuditLogger> logger);

    // Evaluate headers for a given route key (e.g., "/vector/search" or handler name)
    // If audit logger is set and mode is "enforce", logs the policy decision
    PolicyDecision evaluate(const std::unordered_map<std::string, std::string>& headers,
                            const std::string& route) const;

    // Get classification profile by name
    std::optional<ClassificationProfile> getClassificationProfile(const std::string& level) const;

    static bool isStrictClass(const std::string& cls);

private:
    std::unordered_map<std::string, ClassificationProfile> classification_profiles_;
    std::unordered_map<std::string, std::string> resource_mapping_;
    std::string default_mode_ = "enforce";
    std::shared_ptr<themis::utils::AuditLogger> audit_logger_;

    static std::string normalize(const std::string& s);
};

} // namespace governance
} // namespace themis
