#include "governance/policy_engine.h"
#include "utils/logger.h"
#include "utils/audit_logger.h"

#include <algorithm>
#include <fstream>
#include <chrono>
#include <yaml-cpp/yaml.h>

namespace themis {
namespace governance {

std::string PolicyEngine::normalize(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    // trim spaces
    auto is_space = [](unsigned char c){ return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!out.empty() && is_space(static_cast<unsigned char>(out.front()))) out.erase(out.begin());
    while (!out.empty() && is_space(static_cast<unsigned char>(out.back()))) out.pop_back();
    return out;
}

bool PolicyEngine::isStrictClass(const std::string& cls) {
    auto c = normalize(cls);
    return (c == "geheim" || c == "streng-geheim");
}

bool PolicyEngine::loadFromYAML(const std::string& yaml_path) {
    try {
        YAML::Node config = YAML::LoadFile(yaml_path);
        
        // Load VS classification profiles
        if (config["vs_classification"]) {
            const auto& vs = config["vs_classification"];
            for (const auto& kv : vs) {
                std::string level = kv.first.as<std::string>();
                ClassificationProfile profile;
                profile.level = level;
                
                const auto& val = kv.second;
                if (val["encryption_required"]) profile.encryption_required = val["encryption_required"].as<bool>();
                if (val["ann_allowed"]) profile.ann_allowed = val["ann_allowed"].as<bool>();
                if (val["export_allowed"]) profile.export_allowed = val["export_allowed"].as<bool>();
                if (val["cache_allowed"]) profile.cache_allowed = val["cache_allowed"].as<bool>();
                if (val["redaction_level"]) profile.redaction_level = val["redaction_level"].as<std::string>();
                if (val["retention_days"]) profile.retention_days = val["retention_days"].as<int>();
                if (val["log_encryption"]) profile.log_encryption = val["log_encryption"].as<bool>();
                
                classification_profiles_[normalize(level)] = profile;
            }
        }
        
        // Load enforcement resource mappings
        if (config["enforcement"] && config["enforcement"]["resource_mapping"]) {
            const auto& mappings = config["enforcement"]["resource_mapping"];
            for (const auto& kv : mappings) {
                std::string resource = kv.first.as<std::string>();
                std::string min_class = kv.second.as<std::string>();
                resource_mapping_[resource] = normalize(min_class);
            }
        }
        
        // Load default mode
        if (config["enforcement"] && config["enforcement"]["default_mode"]) {
            default_mode_ = normalize(config["enforcement"]["default_mode"].as<std::string>());
        }
        
        THEMIS_INFO("Loaded governance policies from {}: {} classifications, {} resource mappings",
            yaml_path, classification_profiles_.size(), resource_mapping_.size());
        return true;
        
    } catch (const YAML::Exception& e) {
        THEMIS_ERROR("Failed to load governance YAML from {}: {}", yaml_path, e.what());
        return false;
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to load governance config: {}", e.what());
        return false;
    }
}

std::optional<ClassificationProfile> PolicyEngine::getClassificationProfile(const std::string& level) const {
    auto it = classification_profiles_.find(normalize(level));
    if (it == classification_profiles_.end()) return std::nullopt;
    return it->second;
}

void PolicyEngine::setAuditLogger(std::shared_ptr<themis::utils::AuditLogger> logger) {
    audit_logger_ = std::move(logger);
}

PolicyDecision PolicyEngine::evaluate(const std::unordered_map<std::string, std::string>& headers,
                                      const std::string& route) const {
    auto get = [&](const char* key) -> std::string {
        auto it = headers.find(key);
        if (it != headers.end()) return it->second;
        return std::string();
    };

    PolicyDecision d;

    // Classification
    auto cls = normalize(get("X-Classification"));
    if (cls.empty()) {
        // Check resource mapping for default
        auto res_it = resource_mapping_.find(route);
        if (res_it != resource_mapping_.end()) {
            cls = res_it->second;
        } else {
            cls = "vs-nfd"; // ultimate default
        }
    }
    d.classification = cls;

    // Mode
    auto mode = normalize(get("X-Governance-Mode"));
    if (mode != "observe") mode = default_mode_;
    d.mode = mode;

    // Lookup profile
    auto profile = getClassificationProfile(cls);
    if (profile) {
        d.encrypt_logs = profile->log_encryption;
        d.redaction = profile->redaction_level;
        d.ann_allowed = profile->ann_allowed;
        d.require_content_encryption = profile->encryption_required;
        d.export_allowed = profile->export_allowed;
        d.cache_allowed = profile->cache_allowed;
        d.retention_days = profile->retention_days;
    } else {
        // Fallback if profile not found (MVP heuristics)
        bool strict = isStrictClass(cls);
        d.encrypt_logs = strict;
        d.redaction = strict ? "strict" : "standard";
        d.ann_allowed = !strict;
        d.require_content_encryption = strict;
        d.export_allowed = !strict;
        d.cache_allowed = !strict;
        d.retention_days = 365;
    }

    // Allow header override for encrypt_logs
    auto enc_logs = normalize(get("X-Encrypt-Logs"));
    if (!enc_logs.empty()) {
        if (enc_logs == "true" || enc_logs == "1" || enc_logs == "yes") {
            d.encrypt_logs = true;
        } else if (enc_logs == "false" || enc_logs == "0" || enc_logs == "no") {
            d.encrypt_logs = false;
        }
    }

    // Allow header override for redaction
    auto redact = normalize(get("X-Redaction-Level"));
    if (!redact.empty()) {
        d.redaction = redact;
    }

    // Audit log if in enforce mode and logger is configured
    if (audit_logger_ && d.mode == "enforce") {
        nlohmann::json audit_event = {
            {"event_type", "policy_evaluation"},
            {"route", route},
            {"classification", d.classification},
            {"mode", d.mode},
            {"require_content_encryption", d.require_content_encryption},
            {"encrypt_logs", d.encrypt_logs},
            {"redaction", d.redaction},
            {"retention_days", d.retention_days},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        // Add user context if available in headers
        auto user_it = headers.find("X-User-Id");
        if (user_it != headers.end()) {
            audit_event["user_id"] = user_it->second;
        }
        
        audit_logger_->logEvent(audit_event);
    }

    return d;
}

} // namespace governance
} // namespace themis
