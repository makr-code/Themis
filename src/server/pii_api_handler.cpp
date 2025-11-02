#include "server/pii_api_handler.h"

#include <algorithm>

using nlohmann::json;

namespace themis { namespace server {

static std::vector<json> demoMappings() {
    // Simple in-memory demo data set
    return std::vector<json>{
        json{{"original_uuid", "11111111-1111-1111-1111-111111111111"}, {"pseudonym", "user_0001"}, {"active", true}, {"created_at", "2025-10-01T10:00:00"}, {"updated_at", "2025-10-15T08:30:00"}},
        json{{"original_uuid", "22222222-2222-2222-2222-222222222222"}, {"pseudonym", "user_0002"}, {"active", false}, {"created_at", "2025-09-12T09:10:00"}, {"updated_at", "2025-10-12T11:11:00"}},
        json{{"original_uuid", "33333333-3333-3333-3333-333333333333"}, {"pseudonym", "user_0003"}, {"active", true}, {"created_at", "2025-08-20T12:45:00"}, {"updated_at", "2025-10-01T17:00:00"}},
    };
}

json PIIApiHandler::listMappings(const PiiQueryFilter& filter) {
    auto items = demoMappings();

    auto matches = [&](const json& j) -> bool {
        if (filter.active_only && j.value("active", false) == false) return false;
        if (!filter.original_uuid.empty()) {
            auto v = j.value("original_uuid", std::string());
            if (v.find(filter.original_uuid) == std::string::npos) return false;
        }
        if (!filter.pseudonym.empty()) {
            auto v = j.value("pseudonym", std::string());
            if (v.find(filter.pseudonym) == std::string::npos) return false;
        }
        return true;
    };

    std::vector<json> filtered;
    filtered.reserve(items.size());
    for (const auto& j : items) if (matches(j)) filtered.push_back(j);

    int total = static_cast<int>(filtered.size());
    int page = std::max(1, filter.page);
    int page_size = std::max(1, filter.page_size);
    int start = (page - 1) * page_size;
    int end = std::min(start + page_size, total);

    json out_items = json::array();
    if (start < total) {
        for (int i = start; i < end; ++i) out_items.push_back(filtered[static_cast<size_t>(i)]);
    }

    return json{
        {"items", out_items},
        {"total", total},
        {"page", page},
        {"page_size", page_size}
    };
}

std::string PIIApiHandler::exportCsv(const PiiQueryFilter& filter) {
    auto js = listMappings(filter);
    std::string csv = "original_uuid,pseudonym,active,created_at,updated_at\n";
    for (const auto& r : js["items"]) {
        csv += r.value("original_uuid", ""); csv += ",";
        csv += r.value("pseudonym", ""); csv += ",";
        csv += (r.value("active", false) ? "true" : "false"); csv += ",";
        csv += r.value("created_at", ""); csv += ",";
        csv += r.value("updated_at", ""); csv += "\n";
    }
    return csv;
}

json PIIApiHandler::deleteByUuid(const std::string& uuid) {
    // Demo: pretend we deleted (real implementation would affect storage)
    return json{{"status", "accepted"}, {"uuid", uuid}};
}

}} // namespace themis::server
