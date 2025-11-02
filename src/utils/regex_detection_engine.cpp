#include "utils/regex_detection_engine.h"
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>

namespace themis {
namespace utils {

RegexDetectionEngine::RegexDetectionEngine()
    : enabled_(false)
    , min_confidence_(0.75)
    , enable_field_hints_(true)
    , default_redaction_mode_("strict")
    , max_regex_length_(500) {
    
    // Initialize with embedded signature (for unsigned fallback)
    signature_.engine_type = "regex";
    signature_.version = "1.0.0";
    signature_.signature_id = "embedded-regex-engine";
    signature_.signer = "Embedded Default";
    signature_.signed_at = "2025-11-01T00:00:00Z";
    signature_.cert_serial = "EMBEDDED";
}

std::string RegexDetectionEngine::getName() const {
    return "regex";
}

std::string RegexDetectionEngine::getVersion() const {
    return signature_.version;
}

bool RegexDetectionEngine::isEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

PluginSignature RegexDetectionEngine::getSignature() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signature_;
}

bool RegexDetectionEngine::initialize(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    last_error_.clear();
    
    try {
        // Extract basic config
        enabled_ = config.value("enabled", true);
        
        if (config.contains("version")) {
            signature_.version = config["version"];
        }
        
        // Extract signature if present
        if (config.contains("signature")) {
            auto sig = config["signature"];
            signature_.config_hash = sig.value("config_hash", "");
            signature_.signature = sig.value("signature", "");
            signature_.signature_id = sig.value("signature_id", "");
            signature_.cert_serial = sig.value("cert_serial", "");
            signature_.signed_at = sig.value("signed_at", "");
            signature_.signer = sig.value("signer", "");
        }
        
        // Load settings
        if (config.contains("settings")) {
            auto settings = config["settings"];
            min_confidence_ = settings.value("min_confidence", 0.75);
            enable_field_hints_ = settings.value("enable_field_hints", true);
            default_redaction_mode_ = settings.value("default_redaction_mode", "strict");
            max_regex_length_ = settings.value("max_regex_length", 500);
        }
        
        // Load patterns
        if (!loadPatternsFromConfig(config)) {
            spdlog::warn("RegexDetectionEngine: Pattern loading failed, using embedded defaults");
            loadEmbeddedDefaults();
        }
        
        rebuildFieldHints();
        
        spdlog::info("RegexDetectionEngine: Initialized with {} patterns", patterns_.size());
        return true;
        
    } catch (const std::exception& e) {
        last_error_ = std::string("Initialization failed: ") + e.what();
        spdlog::error("RegexDetectionEngine: {}", last_error_);
        return false;
    }
}

bool RegexDetectionEngine::reload(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Save old state
    auto old_patterns = patterns_;
    auto old_field_hints = field_name_hints_;
    auto old_redaction_modes = redaction_modes_;
    
    last_error_.clear();
    
    // Try loading new config
    if (!loadPatternsFromConfig(config)) {
        // Restore old state on failure
        patterns_ = old_patterns;
        field_name_hints_ = old_field_hints;
        redaction_modes_ = old_redaction_modes;
        spdlog::error("RegexDetectionEngine: Reload failed, retained previous patterns");
        return false;
    }
    
    rebuildFieldHints();
    
    spdlog::info("RegexDetectionEngine: Reloaded {} patterns", patterns_.size());
    return true;
}

std::vector<PIIFinding> RegexDetectionEngine::detectInText(const std::string& text) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enabled_) {
        return {};
    }
    
    std::vector<PIIFinding> findings;
    
    for (const auto& pattern : patterns_) {
        if (!pattern.enabled) continue;
        
        PIIType type = PIITypeUtils::fromString(pattern.name);
        if (type == PIIType::UNKNOWN) continue;
        
        std::sregex_iterator it(text.begin(), text.end(), pattern.compiled_regex);
        std::sregex_iterator end;
        
        for (; it != end; ++it) {
            std::smatch match = *it;
            std::string value = match.str();
            
            // Apply validation if configured
            if (pattern.validation == "luhn" && !luhnCheck(value)) {
                continue; // Skip invalid credit card numbers
            }
            
            PIIFinding finding;
            finding.type = type;
            finding.value = value;
            finding.start_offset = match.position();
            finding.end_offset = match.position() + match.length();
            finding.confidence = pattern.confidence;
            finding.pattern_name = pattern.name;
            finding.engine_name = "regex";
            
            findings.push_back(finding);
        }
    }
    
    // Sort by start offset
    std::sort(findings.begin(), findings.end(), 
              [](const PIIFinding& a, const PIIFinding& b) {
                  return a.start_offset < b.start_offset;
              });
    
    return findings;
}

PIIType RegexDetectionEngine::classifyFieldName(const std::string& field_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enable_field_hints_) {
        return PIIType::UNKNOWN;
    }
    
    std::string lower = field_name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Exact match
    auto it = field_name_hints_.find(lower);
    if (it != field_name_hints_.end()) {
        return it->second;
    }
    
    // Partial match (field name contains hint)
    for (const auto& [hint, type] : field_name_hints_) {
        if (lower.find(hint) != std::string::npos) {
            return type;
        }
    }
    
    return PIIType::UNKNOWN;
}

std::string RegexDetectionEngine::getRedactionRecommendation(PIIType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = redaction_modes_.find(type);
    if (it != redaction_modes_.end()) {
        return it->second;
    }
    
    return default_redaction_mode_;
}

std::string RegexDetectionEngine::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

nlohmann::json RegexDetectionEngine::getMetadata() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    nlohmann::json metadata;
    metadata["engine_type"] = "regex";
    metadata["version"] = signature_.version;
    metadata["enabled"] = enabled_;
    metadata["pattern_count"] = std::count_if(patterns_.begin(), patterns_.end(),
                                              [](const RegexPattern& p) { return p.enabled; });
    metadata["total_patterns"] = patterns_.size();
    metadata["signature_id"] = signature_.signature_id;
    metadata["signer"] = signature_.signer;
    metadata["signed_at"] = signature_.signed_at;
    
    return metadata;
}

void RegexDetectionEngine::loadEmbeddedDefaults() {
    patterns_.clear();
    redaction_modes_.clear();
    
    // EMAIL
    RegexPattern email;
    email.name = "EMAIL";
    email.description = "Email address";
    email.regex_str = R"([a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,})";
    email.flags = std::regex::icase;
    email.confidence = 0.95;
    email.redaction_mode = "partial";
    email.field_hints = {"email", "e_mail", "mail", "email_address", "user_email"};
    email.validation = "none";
    email.enabled = true;
    if (validateAndCompilePattern(email)) {
        patterns_.push_back(email);
        redaction_modes_[PIIType::EMAIL] = email.redaction_mode;
    }
    
    // PHONE
    RegexPattern phone;
    phone.name = "PHONE";
    phone.description = "Phone number";
    phone.regex_str = R"((?:\+\d{1,3}[\-.\s]?)?(?:\(\d{2,4}\)?[\-.\s]?)?[\d\-.\s]{7,15})";
    phone.flags = std::regex::icase;
    phone.confidence = 0.85;
    phone.redaction_mode = "partial";
    phone.field_hints = {"phone", "telephone", "phone_number", "tel", "mobile", "cell"};
    phone.validation = "none";
    phone.enabled = true;
    if (validateAndCompilePattern(phone)) {
        patterns_.push_back(phone);
        redaction_modes_[PIIType::PHONE] = phone.redaction_mode;
    }
    
    // SSN
    RegexPattern ssn;
    ssn.name = "SSN";
    ssn.description = "US Social Security Number";
    ssn.regex_str = R"(\b\d{3}\-?\d{2}\-?\d{4}\b)";
    ssn.flags = std::regex::ECMAScript;
    ssn.confidence = 0.98;
    ssn.redaction_mode = "strict";
    ssn.field_hints = {"ssn", "social_security", "social_security_number"};
    ssn.validation = "none";
    ssn.enabled = true;
    if (validateAndCompilePattern(ssn)) {
        patterns_.push_back(ssn);
        redaction_modes_[PIIType::SSN] = ssn.redaction_mode;
    }
    
    // CREDIT_CARD
    RegexPattern card;
    card.name = "CREDIT_CARD";
    card.description = "Credit card number";
    card.regex_str = R"((?:\b|^)[3456][0-9]{3}(?:-[0-9]{4}){3}(?:\b|$))";
    card.flags = std::regex::ECMAScript;
    card.confidence = 0.90;
    card.redaction_mode = "strict";
    card.field_hints = {"card", "credit_card", "card_number", "cc", "payment_card"};
    card.validation = "luhn";
    card.enabled = true;
    if (validateAndCompilePattern(card)) {
        patterns_.push_back(card);
        redaction_modes_[PIIType::CREDIT_CARD] = card.redaction_mode;
    }
    
    // IBAN
    RegexPattern iban;
    iban.name = "IBAN";
    iban.description = "International Bank Account Number";
    iban.regex_str = R"(\b[A-Z]{2}\d{2}[A-Z0-9]{1,30}\b)";
    iban.flags = std::regex::icase;
    iban.confidence = 0.92;
    iban.redaction_mode = "partial";
    iban.field_hints = {"iban", "bank_account", "account_number"};
    iban.validation = "none";
    iban.enabled = true;
    if (validateAndCompilePattern(iban)) {
        patterns_.push_back(iban);
        redaction_modes_[PIIType::IBAN] = iban.redaction_mode;
    }
    
    // IP_ADDRESS
    RegexPattern ip;
    ip.name = "IP_ADDRESS";
    ip.description = "IPv4 address";
    ip.regex_str = R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)";
    ip.flags = std::regex::ECMAScript;
    ip.confidence = 0.80;
    ip.redaction_mode = "partial";
    ip.field_hints = {"ip", "ip_address", "ipv4", "host_ip"};
    ip.validation = "none";
    ip.enabled = true;
    if (validateAndCompilePattern(ip)) {
        patterns_.push_back(ip);
        redaction_modes_[PIIType::IP_ADDRESS] = ip.redaction_mode;
    }
    
    // URL
    RegexPattern url;
    url.name = "URL";
    url.description = "HTTP/HTTPS URL";
    url.regex_str = R"(https?://[^\s]+)";
    url.flags = std::regex::icase;
    url.confidence = 0.90;
    url.redaction_mode = "partial";
    url.field_hints = {"url", "link", "website", "uri"};
    url.validation = "none";
    url.enabled = true;
    if (validateAndCompilePattern(url)) {
        patterns_.push_back(url);
        redaction_modes_[PIIType::URL] = url.redaction_mode;
    }
    
    spdlog::info("RegexDetectionEngine: Loaded {} embedded default patterns", patterns_.size());
}

bool RegexDetectionEngine::loadPatternsFromConfig(const nlohmann::json& config) {
    if (!config.contains("patterns")) {
        last_error_ = "No 'patterns' section found in configuration";
        return false;
    }
    
    patterns_.clear();
    redaction_modes_.clear();
    
    for (const auto& pattern_node : config["patterns"]) {
        RegexPattern pattern;
        
        try {
            pattern.name = pattern_node.value("name", "");
            pattern.description = pattern_node.value("description", "");
            pattern.regex_str = pattern_node.value("regex", "");
            pattern.confidence = pattern_node.value("confidence", 0.80);
            pattern.redaction_mode = pattern_node.value("redaction_mode", default_redaction_mode_);
            pattern.validation = pattern_node.value("validation", "none");
            pattern.enabled = pattern_node.value("enabled", true);
            
            // Parse regex flags
            std::vector<std::string> flag_strings;
            if (pattern_node.contains("flags") && pattern_node["flags"].is_array()) {
                for (const auto& flag : pattern_node["flags"]) {
                    flag_strings.push_back(flag.get<std::string>());
                }
            }
            pattern.flags = parseRegexFlags(flag_strings);
            
            // Parse field hints
            if (pattern_node.contains("field_hints") && pattern_node["field_hints"].is_array()) {
                for (const auto& hint : pattern_node["field_hints"]) {
                    pattern.field_hints.push_back(hint.get<std::string>());
                }
            }
            
        } catch (const std::exception& e) {
            spdlog::warn("RegexDetectionEngine: Failed to parse pattern: {}", e.what());
            continue;
        }
        
        // Validate regex complexity
        if (!validateRegexComplexity(pattern.regex_str)) {
            spdlog::warn("RegexDetectionEngine: Pattern '{}' exceeds max regex length", pattern.name);
            continue;
        }
        
        // Compile and validate pattern
        if (!validateAndCompilePattern(pattern)) {
            spdlog::warn("RegexDetectionEngine: Failed to compile pattern '{}'", pattern.name);
            continue;
        }
        
        // Store redaction mode
        PIIType type = PIITypeUtils::fromString(pattern.name);
        if (type != PIIType::UNKNOWN) {
            redaction_modes_[type] = pattern.redaction_mode;
        }
        
        patterns_.push_back(pattern);
    }
    
    if (patterns_.empty()) {
        last_error_ = "No valid patterns loaded from configuration";
        return false;
    }
    
    return true;
}

bool RegexDetectionEngine::validateAndCompilePattern(RegexPattern& pattern) {
    try {
        pattern.compiled_regex = std::regex(pattern.regex_str, pattern.flags);
        return true;
    } catch (const std::regex_error& e) {
        spdlog::error("RegexDetectionEngine: Regex compilation failed for '{}': {}", 
                      pattern.name, e.what());
        return false;
    }
}

void RegexDetectionEngine::rebuildFieldHints() {
    field_name_hints_.clear();
    
    if (!enable_field_hints_) {
        return;
    }
    
    for (const auto& pattern : patterns_) {
        if (!pattern.enabled) continue;
        
        PIIType type = PIITypeUtils::fromString(pattern.name);
        if (type == PIIType::UNKNOWN) continue;
        
        for (const auto& hint : pattern.field_hints) {
            std::string lower_hint = hint;
            std::transform(lower_hint.begin(), lower_hint.end(), lower_hint.begin(), ::tolower);
            field_name_hints_[lower_hint] = type;
        }
    }
}

bool RegexDetectionEngine::validateRegexComplexity(const std::string& regex_str) const {
    return regex_str.length() <= max_regex_length_;
}

std::regex::flag_type RegexDetectionEngine::parseRegexFlags(
    const std::vector<std::string>& flag_strings) const {
    
    std::regex::flag_type flags = std::regex::ECMAScript;
    
    for (const auto& flag : flag_strings) {
        if (flag == "icase") {
            flags |= std::regex::icase;
        } else if (flag == "nosubs") {
            flags |= std::regex::nosubs;
        } else if (flag == "optimize") {
            flags |= std::regex::optimize;
        }
    }
    
    return flags;
}

bool RegexDetectionEngine::luhnCheck(const std::string& number) const {
    std::string digits;
    for (char c : number) {
        if (std::isdigit(c)) {
            digits += c;
        }
    }
    
    if (digits.length() < 13 || digits.length() > 19) {
        return false;
    }
    
    int sum = 0;
    bool alternate = false;
    
    for (int i = static_cast<int>(digits.length()) - 1; i >= 0; --i) {
        int digit = digits[i] - '0';
        
        if (alternate) {
            digit *= 2;
            if (digit > 9) {
                digit -= 9;
            }
        }
        
        sum += digit;
        alternate = !alternate;
    }
    
    return (sum % 10) == 0;
}

// Factory function for createUnsigned
std::unique_ptr<IPIIDetectionEngine> createRegexEngine() {
    return std::make_unique<RegexDetectionEngine>();
}

} // namespace utils
} // namespace themis
