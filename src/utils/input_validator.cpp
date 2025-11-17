#include "utils/input_validator.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace themis {
namespace utils {

InputValidator::InputValidator(std::string schema_dir)
    : schema_dir_(std::move(schema_dir)) {}

std::optional<nlohmann::json> InputValidator::loadSchema(const std::string& schema_name) const {
    try {
        std::string path = schema_dir_;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') path += "/";
        path += schema_name + ".json";
        std::ifstream in(path);
        if (!in.good()) {
            return std::nullopt; // schema optional
        }
        std::stringstream buf;
        buf << in.rdbuf();
        auto j = nlohmann::json::parse(buf.str());
        return j;
    } catch (...) {
        return std::nullopt;
    }
}

static bool isAsciiControl(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return (uc < 0x20) || (uc == 0x7F);
}

std::string InputValidator::sanitizeForLogs(const std::string& input, size_t max_len) const {
    std::string out;
    out.reserve(std::min(input.size(), max_len));
    for (char c : input) {
        if (out.size() >= max_len) break;
        if (!isAsciiControl(c)) out.push_back(c);
    }
    return out;
}

bool InputValidator::validatePathSegment(const std::string& segment) const {
    if (segment.empty()) return false;
    if (segment.size() > 1024) return false; // arbitrary sane limit
    // Reject traversal or separators
    if (segment.find("..") != std::string::npos) return false;
    if (segment.find('/') != std::string::npos) return false;
    if (segment.find('\\') != std::string::npos) return false;
    if (segment.find('%') != std::string::npos) {
        // rudimentary: block encoded traversal attempts
        std::string lower = segment;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("%2e") != std::string::npos) return false;
    }
    // No control chars
    for (char c : segment) {
        if (isAsciiControl(c)) return false;
    }
    return true;
}

std::optional<std::string> InputValidator::validateJsonStub(
    const nlohmann::json& payload,
    const std::string& schema_name
) const {
    auto schema = loadSchema(schema_name);
    if (!schema.has_value()) {
        return std::nullopt; // no schema -> accept (stub mode)
    }
    // Very small subset: {"type":"object","required":[...],"properties":{k:{"type":"string|object|number|boolean"}}}
    try {
        if (!schema->is_object()) return std::string("invalid schema format");
        if (schema->contains("type") && (*schema)["type"].is_string()) {
            if ((*schema)["type"].get<std::string>() != "object") {
                return std::string("only object schemas supported in stub");
            }
        }
        if (!payload.is_object()) return std::string("payload must be object");
        if (schema->contains("required") && (*schema)["required"].is_array()) {
            for (const auto& k : (*schema)["required"]) {
                if (!k.is_string()) continue;
                auto key = k.get<std::string>();
                if (!payload.contains(key)) {
                    return std::string("missing required field: ") + key;
                }
            }
        }
        if (schema->contains("properties") && (*schema)["properties"].is_object()) {
            for (auto it = (*schema)["properties"].begin(); it != (*schema)["properties"].end(); ++it) {
                const std::string key = it.key();
                const auto& prop = it.value();
                if (!payload.contains(key)) continue;
                if (prop.contains("type") && prop["type"].is_string()) {
                    const std::string t = prop["type"].get<std::string>();
                    const auto& v = payload.at(key);
                    if (t == "string" && !v.is_string()) return std::string("field '") + key + "' must be string";
                    if (t == "object" && !v.is_object()) return std::string("field '") + key + "' must be object";
                    if (t == "number" && !v.is_number()) return std::string("field '") + key + "' must be number";
                    if (t == "boolean" && !v.is_boolean()) return std::string("field '") + key + "' must be boolean";
                    if (t == "array" && !v.is_array()) return std::string("field '") + key + "' must be array";
                }
            }
        }
        return std::nullopt;
    } catch (...) {
        return std::string("schema validation error");
    }
}

std::optional<std::string> InputValidator::validateAqlRequest(const nlohmann::json& payload) const {
    // Basic structure
    if (!payload.is_object()) return std::string("AQL request must be a JSON object");
    if (!payload.contains("query") || !payload.at("query").is_string()) {
        return std::string("AQL request requires string field 'query'");
    }
    const std::string q = payload.at("query").get<std::string>();
    if (q.empty()) return std::string("AQL query must not be empty");
    if (q.size() > 100000) return std::string("AQL query too large (>100k)");

    // Disallow control characters and NULs
    for (char c : q) {
        if (isAsciiControl(c) && c != '\n' && c != '\t' && c != '\r') {
            return std::string("AQL query contains control characters");
        }
    }
    // Very conservative blacklist (injection & multiple statements patterns)
    std::string lower = q; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find(";;") != std::string::npos) return std::string("multiple statement separator not allowed");
    if (lower.find("\0") != std::string::npos) return std::string("NUL byte not allowed");
    // Disallow obvious DDL/DML tokens that don't belong in read-only endpoints (adjust as needed)
    static const char* forbidden[] = { "drop ", "truncate ", "alter ", "grant ", "revoke ", "create table", "insert ", "update ", "delete " };
    for (auto* f : forbidden) {
        if (lower.find(f) != std::string::npos) {
            return std::string("forbidden token in AQL query: '") + f + "'";
        }
    }
    // Optional: require bind variables to be an object if present
    if (payload.contains("bindVars") && !payload.at("bindVars").is_object()) {
        return std::string("'bindVars' must be an object");
    }

    // Pass minimal stub schema if available
    if (auto err = validateJsonStub(payload, "aql_request")) {
        return err;
    }

    return std::nullopt;
}

} // namespace utils
} // namespace themis
