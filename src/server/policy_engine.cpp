#include "server/policy_engine.h"
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace themis {

using json = nlohmann::json;

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

bool PolicyEngine::loadFromFile(const std::string& path, std::string* err) {
    try {
        auto ends_with = [](const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };

        std::vector<Policy> loaded;

        if (ends_with(path, ".yaml") || ends_with(path, ".yml")) {
            // YAML parsing
            YAML::Node root = YAML::LoadFile(path);
            auto parse_policy_node = [&](const YAML::Node& n) -> std::optional<Policy> {
                try {
                    Policy p;
                    if (n["id"]) p.id = n["id"].as<std::string>("");
                    if (n["name"]) p.name = n["name"].as<std::string>("");
                    if (n["subjects"]) {
                        for (const auto& s : n["subjects"]) p.subjects.insert(s.as<std::string>());
                    }
                    if (n["actions"]) {
                        for (const auto& a : n["actions"]) p.actions.insert(a.as<std::string>());
                    }
                    if (n["resources"]) {
                        for (const auto& r : n["resources"]) p.resources.push_back(r.as<std::string>());
                    }
                    if (n["effect"]) {
                        auto eff = n["effect"].as<std::string>("allow");
                        p.effect_allow = (eff == "allow");
                    } else {
                        p.effect_allow = true;
                    }
                    if (n["allowed_ip_prefixes"]) {
                        for (const auto& ip : n["allowed_ip_prefixes"]) p.allowed_ip_prefixes.push_back(ip.as<std::string>());
                    }
                    return p;
                } catch (...) {
                    return std::nullopt;
                }
            };

            if (root.IsSequence()) {
                for (const auto& item : root) {
                    auto p = parse_policy_node(item);
                    if (p) loaded.push_back(std::move(*p));
                }
            } else if (root.IsMap() && root["policies"]) {
                const auto& arr = root["policies"];
                if (arr && arr.IsSequence()) {
                    for (const auto& item : arr) {
                        auto p = parse_policy_node(item);
                        if (p) loaded.push_back(std::move(*p));
                    }
                }
            } else {
                if (err) *err = "unsupported YAML structure (expect sequence or {policies: [...]})";
                return false;
            }
        } else {
            // JSON parsing
            std::ifstream f(path);
            if (!f) {
                if (err) *err = "cannot open policies file";
                return false;
            }
            json j; f >> j;
            if (j.is_array()) {
                for (const auto& pj : j) {
                    auto p = fromJson(pj);
                    if (p) loaded.push_back(std::move(*p));
                }
            } else if (j.is_object() && j.contains("policies")) {
                for (const auto& pj : j["policies"]) {
                    auto p = fromJson(pj);
                    if (p) loaded.push_back(std::move(*p));
                }
            }
        }

        setPolicies(std::move(loaded));
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool PolicyEngine::saveToFile(const std::string& path, std::string* err) const {
    try {
        json out = json::array();
        auto list = listPolicies();
        for (const auto& p : list) out.push_back(toJson(p));
        std::ofstream f(path);
        if (!f) { if (err) *err = "cannot write policies file"; return false; }
        f << out.dump(2);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

void PolicyEngine::setPolicies(std::vector<Policy> policies) {
    std::lock_guard<std::mutex> lock(mutex_);
    policies_ = std::move(policies);
}

void PolicyEngine::addPolicy(const Policy& p) {
    std::lock_guard<std::mutex> lock(mutex_);
    policies_.push_back(p);
}

bool PolicyEngine::removePolicy(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto size_before = policies_.size();
    policies_.erase(std::remove_if(policies_.begin(), policies_.end(), [&](const Policy& p){ return p.id == id; }), policies_.end());
    return policies_.size() != size_before;
}

std::vector<PolicyEngine::Policy> PolicyEngine::listPolicies() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return policies_;
}

PolicyEngine::Decision PolicyEngine::authorize(const std::string& user_id,
                                               const std::string& action,
                                               const std::string& resource_path,
                                               const std::optional<std::string>& client_ip) const {
    metrics_.policy_eval_total++;
    std::lock_guard<std::mutex> lock(mutex_);

    // If no policies defined, default allow
    if (policies_.empty()) {
        metrics_.policy_allow_total++;
        return {true, "", "no_policies_default_allow"};
    }

    // Evaluate in order: first matching policy decides
    for (const auto& p : policies_) {
        if (!matchSubject(p, user_id)) continue;
        if (!matchAction(p, action)) continue;
        if (!matchResource(p, resource_path)) continue;
        if (!matchConditions(p, client_ip)) continue;

        if (p.effect_allow) {
            metrics_.policy_allow_total++;
            return {true, p.id, "matched_allow_policy"};
        } else {
            metrics_.policy_deny_total++;
            return {false, p.id, "matched_deny_policy"};
        }
    }

    // No match -> deny by default (secure by default)
    metrics_.policy_deny_total++;
    return {false, "", "no_matching_policy"};
}

bool PolicyEngine::matchSubject(const Policy& p, const std::string& user_id) const {
    if (p.subjects.count("*") > 0) return true;
    return p.subjects.count(user_id) > 0;
}

bool PolicyEngine::matchAction(const Policy& p, const std::string& action) const {
    if (p.actions.count("*") > 0) return true;
    return p.actions.count(action) > 0;
}

bool PolicyEngine::matchResource(const Policy& p, const std::string& resource_path) const {
    if (p.resources.empty()) return true; // no restriction
    for (const auto& r : p.resources) {
        if (starts_with(resource_path, r)) return true;
    }
    return false;
}

bool PolicyEngine::matchConditions(const Policy& p, const std::optional<std::string>& client_ip) const {
    if (!p.allowed_ip_prefixes.empty()) {
        if (!client_ip) return false; // IP required to evaluate
        bool ok = false;
        for (const auto& prefix : p.allowed_ip_prefixes) {
            if (starts_with(*client_ip, prefix)) { ok = true; break; }
        }
        if (!ok) return false;
    }
    return true;
}

json PolicyEngine::toJson(const Policy& p) {
    json j;
    j["id"] = p.id;
    j["name"] = p.name;
    j["subjects"] = json::array(); for (const auto& s : p.subjects) j["subjects"].push_back(s);
    j["actions"] = json::array(); for (const auto& a : p.actions) j["actions"].push_back(a);
    j["resources"] = p.resources;
    j["effect"] = p.effect_allow ? "allow" : "deny";
    if (!p.allowed_ip_prefixes.empty()) j["allowed_ip_prefixes"] = p.allowed_ip_prefixes;
    return j;
}

std::optional<PolicyEngine::Policy> PolicyEngine::fromJson(const json& j) {
    try {
        Policy p;
        p.id = j.value("id", "");
        p.name = j.value("name", "");
        if (j.contains("subjects")) for (const auto& s : j["subjects"]) p.subjects.insert(s.get<std::string>());
        if (j.contains("actions")) for (const auto& a : j["actions"]) p.actions.insert(a.get<std::string>());
        if (j.contains("resources")) for (const auto& r : j["resources"]) p.resources.push_back(r.get<std::string>());
        std::string eff = j.value("effect", std::string("allow"));
        p.effect_allow = (eff == "allow");
        if (j.contains("allowed_ip_prefixes")) for (const auto& ip : j["allowed_ip_prefixes"]) p.allowed_ip_prefixes.push_back(ip.get<std::string>());
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace themis
