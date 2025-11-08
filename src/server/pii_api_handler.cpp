#include "server/pii_api_handler.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <rocksdb/utilities/transaction_db.h>

using nlohmann::json;

namespace themis { namespace server {

// ===== PiiMapping serialization =====
nlohmann::json PiiMapping::toJson() const {
    return json{{"original_uuid", original_uuid},
                {"pseudonym", pseudonym},
                {"active", active},
                {"created_at", created_at},
                {"updated_at", updated_at}};
}

PiiMapping PiiMapping::fromJson(const nlohmann::json& j) {
    PiiMapping m;
    m.original_uuid = j.value("original_uuid", "");
    m.pseudonym = j.value("pseudonym", "");
    m.active = j.value("active", true);
    m.created_at = j.value("created_at", "");
    m.updated_at = j.value("updated_at", "");
    return m;
}

// ===== PIIApiHandler Implementation =====
PIIApiHandler::PIIApiHandler(rocksdb::TransactionDB* db, rocksdb::ColumnFamilyHandle* cf)
    : db_(db), cf_(cf) {}

std::string PIIApiHandler::nowIso8601() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

bool PIIApiHandler::addMapping(const PiiMapping& mappingIn) {
    if (!db_) return false;
    PiiMapping mapping = mappingIn;
    if (mapping.created_at.empty()) mapping.created_at = nowIso8601();
    mapping.updated_at = mapping.created_at;

    std::string key = makeKey(mapping.original_uuid);
    std::string existing;
    rocksdb::ReadOptions ro;
    rocksdb::Status gs = cf_ ? db_->Get(ro, cf_, key, &existing) : db_->Get(ro, key, &existing);
    if (gs.ok()) {
        // duplicate
        return false;
    }

    std::string value = mapping.toJson().dump();
    rocksdb::WriteOptions wo;
    rocksdb::Status s = cf_ ? db_->Put(wo, cf_, key, value) : db_->Put(wo, key, value);
    return s.ok();
}

std::optional<PiiMapping> PIIApiHandler::getMapping(const std::string& original_uuid) const {
    if (!db_) return std::nullopt;
    std::string key = makeKey(original_uuid);
    std::string value;
    rocksdb::ReadOptions ro;
    rocksdb::Status s = cf_ ? db_->Get(ro, cf_, key, &value) : db_->Get(ro, key, &value);
    if (!s.ok()) return std::nullopt;
    try {
        json j = json::parse(value);
        return PiiMapping::fromJson(j);
    } catch (...) {
        return std::nullopt;
    }
}

bool PIIApiHandler::deleteMapping(const std::string& original_uuid) {
    if (!db_) return false;
    std::string key = makeKey(original_uuid);
    rocksdb::WriteOptions wo;
    rocksdb::Status s = cf_ ? db_->Delete(wo, cf_, key) : db_->Delete(wo, key);
    return s.ok();
}

json PIIApiHandler::listMappings(const PiiQueryFilter& filter) {
    json out_items = json::array();
    if (!db_) {
        return json{{"items", out_items}, {"total", 0}, {"page", 1}, {"page_size", 0}};
    }

    // Full scan over prefix "pii:" in the configured CF
    rocksdb::ReadOptions ro;
    std::unique_ptr<rocksdb::Iterator> it(cf_ ? db_->NewIterator(ro, cf_) : db_->NewIterator(ro));
    const std::string prefix = KEY_PREFIX;
    int total = 0;
    int page = std::max(1, filter.page);
    int page_size = std::max(1, filter.page_size);
    int start = (page - 1) * page_size;
    int end = start + page_size;
    int index = 0;

    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
        const auto& k = it->key();
        const auto& v = it->value();
        try {
            json j = json::parse(v.ToString());
            // Apply filters
            if (filter.active_only && j.value("active", false) == false) continue;
            if (!filter.original_uuid.empty()) {
                auto val = j.value("original_uuid", std::string());
                if (val.find(filter.original_uuid) == std::string::npos) continue;
            }
            if (!filter.pseudonym.empty()) {
                auto val = j.value("pseudonym", std::string());
                if (val.find(filter.pseudonym) == std::string::npos) continue;
            }
            // Count and paginate
            if (index >= start && index < end) {
                out_items.push_back(j);
            }
            ++index;
            ++total;
        } catch (...) {
            // skip malformed entries
        }
    }

    return json{{"items", out_items}, {"total", total}, {"page", page}, {"page_size", page_size}};
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
    bool ok = deleteMapping(uuid);
    return json{{"status", ok ? "deleted" : "not_found"}, {"uuid", uuid}};
}

}} // namespace themis::server
