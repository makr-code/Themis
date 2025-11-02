#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace themis { namespace server {

struct PiiQueryFilter {
    std::string original_uuid;
    std::string pseudonym;
    bool active_only{false};
    int page{1};
    int page_size{100};
};

class PIIApiHandler {
public:
    PIIApiHandler() = default;

    // Returns a JSON object: { "items": [ ... ], "total": N }
    nlohmann::json listMappings(const PiiQueryFilter& filter);

    // Returns CSV string with header
    std::string exportCsv(const PiiQueryFilter& filter);

    // Returns JSON { status, uuid }
    nlohmann::json deleteByUuid(const std::string& uuid);
};

}} // namespace themis::server
