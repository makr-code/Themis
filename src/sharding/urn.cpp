#include "sharding/urn.h"
#include <sstream>
#include <algorithm>
#include <cctype>

// Use xxHash for fast consistent hashing
// If xxHash is not available, we'll use std::hash as fallback
#ifdef __has_include
  #if __has_include(<xxhash.h>)
    #include <xxhash.h>
    #define HAS_XXHASH
  #endif
#endif

namespace themis::sharding {

namespace {
    // Valid model types
    const std::vector<std::string> VALID_MODELS = {
        "relational", "graph", "vector", "timeseries", "document"
    };
    
    // UUID validation regex: 8-4-4-4-12 hex pattern
    const std::regex UUID_PATTERN(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    );
}

std::optional<URN> URN::parse(std::string_view urn_str) {
    // Expected format: urn:themis:{model}:{namespace}:{collection}:{uuid}
    // Minimum length check: "urn:themis:a:b:c:d" = 18 chars minimum
    if (urn_str.size() < 18) {
        return std::nullopt;
    }
    
    // Must start with "urn:themis:"
    if (!urn_str.starts_with("urn:themis:")) {
        return std::nullopt;
    }
    
    // Split by colons
    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = 0;
    
    while (end != std::string_view::npos) {
        end = urn_str.find(':', start);
        if (end != std::string_view::npos) {
            parts.emplace_back(urn_str.substr(start, end - start));
            start = end + 1;
        } else {
            parts.emplace_back(urn_str.substr(start));
        }
    }
    
    // Expected: ["urn", "themis", model, namespace, collection, uuid]
    if (parts.size() != 6) {
        return std::nullopt;
    }
    
    // Validate prefix
    if (parts[0] != "urn" || parts[1] != "themis") {
        return std::nullopt;
    }
    
    URN urn;
    urn.model = parts[2];
    urn.namespace_ = parts[3];
    urn.collection = parts[4];
    urn.uuid = parts[5];
    
    // Validate components
    if (!urn.isValidModel()) {
        return std::nullopt;
    }
    
    if (urn.namespace_.empty() || urn.collection.empty()) {
        return std::nullopt;
    }
    
    if (!urn.isValidUUID()) {
        return std::nullopt;
    }
    
    return urn;
}

std::string URN::toString() const {
    std::ostringstream oss;
    oss << "urn:themis:" << model << ":" << namespace_ << ":" << collection << ":" << uuid;
    return oss.str();
}

uint64_t URN::hash() const {
#ifdef HAS_XXHASH
    // Use xxHash for fast, high-quality hashing
    return XXH64(uuid.data(), uuid.size(), 0);
#else
    // Fallback to std::hash
    std::hash<std::string> hasher;
    return hasher(uuid);
#endif
}

bool URN::isValidUUID() const {
    return std::regex_match(uuid, UUID_PATTERN);
}

bool URN::isValidModel() const {
    return std::find(VALID_MODELS.begin(), VALID_MODELS.end(), model) != VALID_MODELS.end();
}

} // namespace themis::sharding
