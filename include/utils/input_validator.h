#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace themis {
namespace utils {

class InputValidator {
public:
    explicit InputValidator(std::string schema_dir);

    // Basic JSON schema stub validation: checks required keys and basic types from a simple stub schema
    // Returns std::nullopt if valid, otherwise an error message
    std::optional<std::string> validateJsonStub(
        const nlohmann::json& payload,
        const std::string& schema_name
    ) const;

    // Validate AQL request payload (expects keys like {"query": "...", "bindVars": {...}})
    // Performs minimal checks: required fields, max length, disallowed characters/patterns
    std::optional<std::string> validateAqlRequest(const nlohmann::json& payload) const;

    // Validate path segment (e.g., entity key); rejects traversal and separators
    bool validatePathSegment(const std::string& segment) const;

    // Sanitize strings for logs (strip control chars and truncate)
    std::string sanitizeForLogs(const std::string& input, size_t max_len = 512) const;

    // Configure/query schema directory
    const std::string& schemaDir() const { return schema_dir_; }

private:
    std::string schema_dir_;

    // Helper to load a stub schema from schema_dir_/name.json
    std::optional<nlohmann::json> loadSchema(const std::string& schema_name) const;
};

} // namespace utils
} // namespace themis
