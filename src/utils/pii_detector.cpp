#include "utils/pii_detector.h"
#include "utils/pii_detection_engine.h"
#include <algorithm>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace themis {
namespace utils {

PIIDetector::PIIDetector(const std::string& config_path, 
                         std::shared_ptr<VCCPKIClient> pki_client)
    : config_path_(config_path)
    , pki_client_(pki_client)
    , default_redaction_mode_("strict") {
    
    // Try loading from YAML
    if (!loadFromYaml(config_path_)) {
        spdlog::warn("PIIDetector: Failed to load {}, using embedded defaults", config_path_);
        spdlog::warn("PIIDetector: Error: {}", last_error_);
        initializeDefaultEngine();
    }
    
    spdlog::info("PIIDetector: Initialized with {} engine(s)", engines_.size());
}

bool PIIDetector::reload(const std::string& config_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string path = config_path.empty() ? config_path_ : config_path;
    
    // Backup current engines
    auto old_engines = std::move(engines_);
    engines_.clear();
    
    last_error_.clear();
    
    if (!loadFromYaml(path)) {
        // Restore old engines on failure
        engines_ = std::move(old_engines);
        spdlog::error("PIIDetector: Reload failed, retained previous engines. Error: {}", last_error_);
        return false;
    }
    
    if (!config_path.empty()) {
        config_path_ = config_path;
    }
    
    spdlog::info("PIIDetector: Reloaded {} engine(s) from {}", engines_.size(), path);
    return true;
}

void PIIDetector::setPKIClient(std::shared_ptr<VCCPKIClient> pki_client) {
    std::lock_guard<std::mutex> lock(mutex_);
    pki_client_ = pki_client;
}

bool PIIDetector::isPKIVerificationEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pki_client_ != nullptr;
}

std::string PIIDetector::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

std::vector<PIIFinding> PIIDetector::detectInText(const std::string& text) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PIIFinding> all_findings;
    
    // Run all enabled engines
    for (const auto& engine : engines_) {
        if (!engine->isEnabled()) continue;
        
        try {
            auto engine_findings = engine->detectInText(text);
            all_findings.insert(all_findings.end(), 
                              engine_findings.begin(), engine_findings.end());
        } catch (const std::exception& e) {
            spdlog::error("PIIDetector: Engine '{}' threw exception: {}", 
                         engine->getName(), e.what());
        }
    }
    
    // Deduplicate and sort
    return deduplicateFindings(std::move(all_findings));
}

std::unordered_map<std::string, std::vector<PIIFinding>> PIIDetector::detectInJson(
    const nlohmann::json& json_obj) const {
    // Wichtig: Kein globaler Lock während der Rekursion halten, da detectInText/classifyFieldName intern sperren
    std::unordered_map<std::string, std::vector<PIIFinding>> result;
    scanJsonRecursive(json_obj, "", result);
    return result;
}

PIIType PIIDetector::classifyFieldName(const std::string& field_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Query all engines, return first non-UNKNOWN result
    for (const auto& engine : engines_) {
        if (!engine->isEnabled()) continue;
        
        PIIType type = engine->classifyFieldName(field_name);
        if (type != PIIType::UNKNOWN) {
            return type;
        }
    }
    
    return PIIType::UNKNOWN;
}

std::string PIIDetector::getRedactionRecommendation(PIIType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Query first enabled engine
    for (const auto& engine : engines_) {
        if (!engine->isEnabled()) continue;
        
        std::string mode = engine->getRedactionRecommendation(type);
        if (mode != "none" && mode != default_redaction_mode_) {
            return mode;
        }
    }
    
    return default_redaction_mode_;
}

std::string PIIDetector::maskValue(PIIType type, const std::string& value) const {
    std::string mode = getRedactionRecommendation(type);
    return PIITypeUtils::maskValue(type, value, mode);
}

std::vector<std::string> PIIDetector::getEnabledEngines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> enabled;
    for (const auto& engine : engines_) {
        if (engine->isEnabled()) {
            enabled.push_back(engine->getName());
        }
    }
    
    return enabled;
}

nlohmann::json PIIDetector::getEngineMetadata() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    nlohmann::json metadata;
    metadata["total_engines"] = engines_.size();
    metadata["enabled_engines"] = 0;
    metadata["pki_verification_enabled"] = (pki_client_ != nullptr);
    
    nlohmann::json engines_array = nlohmann::json::array();
    
    for (const auto& engine : engines_) {
        if (engine->isEnabled()) {
            metadata["enabled_engines"] = metadata["enabled_engines"].get<int>() + 1;
        }
        
        engines_array.push_back(engine->getMetadata());
    }
    
    metadata["engines"] = engines_array;
    
    return metadata;
}

bool PIIDetector::loadFromYaml(const std::string& path) {
    try {
        // If the provided path doesn't point to an existing file, and is
        // relative, try walking up a few parent directories to find a
        // repository-level `config/` directory (useful when running tests
        // from the `build/` directory).
        std::string resolved = path;
        if (!std::filesystem::exists(resolved)) {
            if (!std::filesystem::path(resolved).is_absolute()) {
                std::filesystem::path cur = std::filesystem::current_path();
                bool found = false;
                // Try up to 4 levels up
                for (int i = 0; i < 4; ++i) {
                    std::filesystem::path candidate = cur;
                    for (int j = 0; j < i; ++j) candidate = candidate.parent_path();
                    candidate /= resolved;
                    if (std::filesystem::exists(candidate)) {
                        resolved = candidate.string();
                        found = true;
                        break;
                    }
                }
                (void)found; // keep current behavior if not found
            }
        }

        YAML::Node config = YAML::LoadFile(resolved);
        
        // Load global settings
        if (config["global_settings"]) {
            auto settings = config["global_settings"];
            if (settings["default_redaction_mode"]) {
                default_redaction_mode_ = settings["default_redaction_mode"].as<std::string>();
            }
        }
        
        // Load detection engines
        if (!config["detection_engines"]) {
            last_error_ = "No 'detection_engines' section found in YAML";
            return false;
        }
        
        bool loaded_any = false;
        
        for (const auto& engine_node : config["detection_engines"]) {
            // Convert YAML to JSON for engine loading
            nlohmann::json engine_config;
            
            // Recursive YAML->JSON conversion
            std::function<void(const YAML::Node&, nlohmann::json&)> convertYamlToJson = 
                [&](const YAML::Node& yaml_node, nlohmann::json& json_node) {
                    if (yaml_node.IsScalar()) {
                        // Try to parse as native types (bool, int, double) before falling back to string
                        try {
                            json_node = yaml_node.as<bool>();
                            return;
                        } catch (...) {}
                        
                        try {
                            json_node = yaml_node.as<int>();
                            return;
                        } catch (...) {}
                        
                        try {
                            json_node = yaml_node.as<double>();
                            return;
                        } catch (...) {}
                        
                        // Fall back to string
                        try {
                            json_node = yaml_node.as<std::string>();
                        } catch (...) {
                            json_node = nullptr;
                        }
                    } else if (yaml_node.IsSequence()) {
                        json_node = nlohmann::json::array();
                        for (const auto& item : yaml_node) {
                            nlohmann::json item_json;
                            convertYamlToJson(item, item_json);
                            json_node.push_back(item_json);
                        }
                    } else if (yaml_node.IsMap()) {
                        json_node = nlohmann::json::object();
                        for (const auto& kv : yaml_node) {
                            std::string key = kv.first.as<std::string>();
                            nlohmann::json value_json;
                            convertYamlToJson(kv.second, value_json);
                            json_node[key] = value_json;
                        }
                    }
                };
            
            convertYamlToJson(engine_node, engine_config);
            
            // Try to load and verify engine
            if (verifyAndLoadEngine(engine_config)) {
                loaded_any = true;
            }
        }
        
        if (!loaded_any) {
            last_error_ = "No engines successfully loaded from YAML";
            return false;
        }
        
        return true;
        
    } catch (const YAML::Exception& e) {
        last_error_ = std::string("YAML parse error: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("Error loading YAML: ") + e.what();
        return false;
    }
}

void PIIDetector::initializeDefaultEngine() {
    engines_.clear();
    
    // Create unsigned regex engine with embedded defaults
    auto engine = PIIDetectionEngineFactory::createUnsigned("regex");
    if (!engine) {
        spdlog::error("PIIDetector: Failed to create default regex engine");
        return;
    }
    
    // Initialize with empty config (will use embedded defaults)
    nlohmann::json empty_config;
    empty_config["enabled"] = true;
    
    if (!engine->initialize(empty_config)) {
        spdlog::error("PIIDetector: Failed to initialize default regex engine: {}", 
                     engine->getLastError());
        return;
    }
    
    engines_.push_back(std::move(engine));
    spdlog::info("PIIDetector: Initialized with embedded unsigned regex engine");
}

bool PIIDetector::verifyAndLoadEngine(const nlohmann::json& engine_config) {
    std::string engine_type;
    
    try {
        engine_type = engine_config.value("type", "");
        if (engine_type.empty()) {
            spdlog::warn("PIIDetector: Engine config missing 'type' field");
            return false;
        }
        
        bool enabled = engine_config.value("enabled", false);
        if (!enabled) {
            spdlog::info("PIIDetector: Skipping disabled engine '{}'", engine_type);
            return false;
        }
        
        std::unique_ptr<IPIIDetectionEngine> engine;
        std::string error_msg;
        
        // If PKI client is configured, use signed loading
        if (pki_client_) {
            engine = PIIDetectionEngineFactory::createSigned(
                engine_type, engine_config, *pki_client_, error_msg);
            
            if (!engine) {
                spdlog::error("PIIDetector: PKI verification failed for '{}': {}", 
                             engine_type, error_msg);
                
                // Check if fallback to unsigned is allowed
                auto global_settings = engine_config.value("global_settings", nlohmann::json::object());
                bool allow_fallback = global_settings.value("allow_embedded_fallback", true);
                
                if (allow_fallback && engine_type == "regex") {
                    spdlog::warn("PIIDetector: Falling back to unsigned regex engine");
                    engine = PIIDetectionEngineFactory::createUnsigned(engine_type);
                    if (engine) {
                        engine->initialize(engine_config);
                    }
                } else {
                    return false;
                }
            }
        } else {
            // No PKI client - load unsigned
            spdlog::warn("PIIDetector: Loading engine '{}' WITHOUT PKI verification", engine_type);
            engine = PIIDetectionEngineFactory::createUnsigned(engine_type);
            
            if (!engine) {
                spdlog::error("PIIDetector: Failed to create engine '{}'", engine_type);
                return false;
            }
            
            if (!engine->initialize(engine_config)) {
                spdlog::error("PIIDetector: Engine '{}' initialization failed: {}", 
                             engine_type, engine->getLastError());
                return false;
            }
        }
        
        if (engine) {
            spdlog::info("PIIDetector: Loaded engine '{}' v{}", 
                        engine->getName(), engine->getVersion());
            engines_.push_back(std::move(engine));
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        spdlog::error("PIIDetector: Exception loading engine '{}': {}", engine_type, e.what());
        return false;
    }
}

void PIIDetector::scanJsonRecursive(
    const nlohmann::json& obj,
    const std::string& path,
    std::unordered_map<std::string, std::vector<PIIFinding>>& findings) const {
    
    if (obj.is_object()) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            std::string key = it.key();
            std::string new_path = path.empty() ? key : path + "." + key;
            
            // Check field name for PII hints
            PIIType field_type = classifyFieldName(key);
            if (field_type != PIIType::UNKNOWN && it.value().is_string()) {
                PIIFinding finding;
                finding.type = field_type;
                finding.value = it.value().get<std::string>();
                finding.start_offset = 0;
                finding.end_offset = finding.value.length();
                finding.confidence = 0.85; // Field name heuristic confidence
                finding.pattern_name = PIITypeUtils::toString(field_type) + "_FIELD_HINT";
                finding.engine_name = "field_hint";
                findings[new_path].push_back(finding);
            }
            
            // Recurse for nested content
            if (it.value().is_object() || it.value().is_array()) {
                scanJsonRecursive(it.value(), new_path, findings);
            } else if (it.value().is_string()) {
                // Scan string values for PII patterns
                std::string value = it.value().get<std::string>();
                auto text_findings = detectInText(value);
                if (!text_findings.empty()) {
                    findings[new_path].insert(findings[new_path].end(), 
                                             text_findings.begin(), text_findings.end());
                }
            }
        }
    } else if (obj.is_array()) {
        for (size_t i = 0; i < obj.size(); ++i) {
            std::string new_path = path + "[" + std::to_string(i) + "]";
            scanJsonRecursive(obj[i], new_path, findings);
        }
    } else if (obj.is_string()) {
        std::string value = obj.get<std::string>();
        auto text_findings = detectInText(value);
        if (!text_findings.empty()) {
            findings[path].insert(findings[path].end(), 
                                 text_findings.begin(), text_findings.end());
        }
    }
}

std::vector<PIIFinding> PIIDetector::deduplicateFindings(
    std::vector<PIIFinding> findings) const {
    
    if (findings.size() <= 1) {
        return findings;
    }
    
    // Sort by offset
    std::sort(findings.begin(), findings.end(), 
              [](const PIIFinding& a, const PIIFinding& b) {
                  return a.start_offset < b.start_offset;
              });
    
    // Remove overlapping findings (keep higher confidence)
    std::vector<PIIFinding> deduplicated;
    deduplicated.push_back(findings[0]);
    
    for (size_t i = 1; i < findings.size(); ++i) {
        const auto& prev = deduplicated.back();
        const auto& curr = findings[i];
        
        // Check for overlap
        if (curr.start_offset < prev.end_offset) {
            // Prefer non-PHONE over PHONE when overlapping
            if (prev.type == PIIType::PHONE && curr.type != PIIType::PHONE) {
                deduplicated.back() = curr;
            } else if (curr.type == PIIType::PHONE && prev.type != PIIType::PHONE) {
                // keep prev
            } else {
                // Fall back to higher confidence
                if (curr.confidence > prev.confidence) {
                    deduplicated.back() = curr;
                }
            }
        } else {
            // No overlap - add to result
            deduplicated.push_back(curr);
        }
    }
    
    return deduplicated;
}

} // namespace utils
} // namespace themis
