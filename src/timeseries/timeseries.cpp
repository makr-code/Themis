#include "timeseries/timeseries.h"
#include "utils/logger.h"
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/utilities/transaction.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace themis {

// ===== DataPoint JSON Serialization =====

nlohmann::json TimeSeriesStore::DataPoint::toJson() const {
    nlohmann::json j;
    j["timestamp_ms"] = timestamp_ms;
    j["value"] = value;
    j["metadata"] = metadata;
    return j;
}

TimeSeriesStore::DataPoint TimeSeriesStore::DataPoint::fromJson(const nlohmann::json& j) {
    DataPoint point;
    point.timestamp_ms = j.value("timestamp_ms", int64_t(0));
    point.value = j.value("value", 0.0);
    point.metadata = j.value("metadata", nlohmann::json::object());
    return point;
}

// ===== Aggregation JSON Serialization =====

nlohmann::json TimeSeriesStore::Aggregation::toJson() const {
    nlohmann::json j;
    j["min"] = min;
    j["max"] = max;
    j["avg"] = avg;
    j["sum"] = sum;
    j["count"] = count;
    return j;
}

// ===== TimeSeriesStore Implementation =====

TimeSeriesStore::TimeSeriesStore(rocksdb::TransactionDB* db, rocksdb::ColumnFamilyHandle* cf)
    : db_(db), cf_(cf) {
}

std::string TimeSeriesStore::makeKey(std::string_view metric, 
                                     std::string_view entity,
                                     int64_t timestamp_ms) const {
    // Format: ts:{metric}:{entity}:{timestamp_ms}
    // Pad timestamp with zeros for lexicographic ordering
    std::ostringstream oss;
    oss << KEY_PREFIX << metric << ":" << entity << ":" 
        << std::setw(20) << std::setfill('0') << timestamp_ms;
    return oss.str();
}

std::string TimeSeriesStore::makePrefix(std::string_view metric,
                                       std::string_view entity) const {
    std::ostringstream oss;
    oss << KEY_PREFIX << metric << ":" << entity << ":";
    return oss.str();
}

bool TimeSeriesStore::put(std::string_view metric, 
                         std::string_view entity,
                         const DataPoint& point) {
    if (!db_) {
        THEMIS_ERROR("TimeSeriesStore::put - DB not initialized");
        return false;
    }
    
    std::string key = makeKey(metric, entity, point.timestamp_ms);
    nlohmann::json value_json = point.toJson();
    std::string value_str = value_json.dump();
    
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s = db_->Put(write_opts, cf_, key, value_str);
    
    if (!s.ok()) {
        THEMIS_ERROR("TimeSeriesStore::put - RocksDB Put failed: {}", s.ToString());
        return false;
    }
    
    return true;
}

std::vector<TimeSeriesStore::DataPoint> TimeSeriesStore::query(
    std::string_view metric,
    std::string_view entity,
    const RangeQuery& query) const {
    
    std::vector<DataPoint> results;
    if (!db_) return results;
    
    std::string prefix = makePrefix(metric, entity);
    std::string from_key = makeKey(metric, entity, query.from_ms);
    std::string to_key = makeKey(metric, entity, query.to_ms);
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_));
    
    if (query.descending) {
        // Start from to_key and iterate backwards
        it->Seek(to_key);
        if (!it->Valid() || !it->key().starts_with(prefix)) {
            // Seek to last key with this prefix
            it->SeekForPrev(to_key);
        }
        
        while (it->Valid() && it->key().starts_with(prefix) && results.size() < query.limit) {
            if (it->key().ToString() < from_key) break;
            
            try {
                nlohmann::json j = nlohmann::json::parse(it->value().ToString());
                results.push_back(DataPoint::fromJson(j));
            } catch (const std::exception& e) {
                THEMIS_WARN("TimeSeriesStore::query - Failed to parse data point: {}", e.what());
            }
            
            it->Prev();
        }
    } else {
        // Forward iteration
        it->Seek(from_key);
        
        while (it->Valid() && it->key().starts_with(prefix) && results.size() < query.limit) {
            if (it->key().ToString() > to_key) break;
            
            try {
                nlohmann::json j = nlohmann::json::parse(it->value().ToString());
                results.push_back(DataPoint::fromJson(j));
            } catch (const std::exception& e) {
                THEMIS_WARN("TimeSeriesStore::query - Failed to parse data point: {}", e.what());
            }
            
            it->Next();
        }
    }
    
    return results;
}

TimeSeriesStore::Aggregation TimeSeriesStore::aggregate(
    std::string_view metric,
    std::string_view entity,
    const RangeQuery& query) const {
    
    Aggregation agg;
    auto points = this->query(metric, entity, query);
    
    if (points.empty()) return agg;
    
    agg.min = points[0].value;
    agg.max = points[0].value;
    agg.sum = 0.0;
    
    for (const auto& p : points) {
        agg.min = std::min(agg.min, p.value);
        agg.max = std::max(agg.max, p.value);
        agg.sum += p.value;
    }
    
    agg.count = points.size();
    agg.avg = agg.sum / agg.count;
    
    return agg;
}

size_t TimeSeriesStore::deleteOldPoints(std::string_view metric,
                                       std::string_view entity,
                                       int64_t before_ms) {
    if (!db_) return 0;
    
    std::string prefix = makePrefix(metric, entity);
    std::string end_key = makeKey(metric, entity, before_ms);
    
    size_t deleted = 0;
    rocksdb::WriteOptions write_opts;
    rocksdb::ReadOptions read_opts;
    
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_));
    it->Seek(prefix);
    
    while (it->Valid() && it->key().starts_with(prefix)) {
        if (it->key().ToString() >= end_key) break;
        
        rocksdb::Status s = db_->Delete(write_opts, cf_, it->key());
        if (s.ok()) {
            deleted++;
        }
        
        it->Next();
    }
    
    return deleted;
}

std::optional<TimeSeriesStore::DataPoint> TimeSeriesStore::getLatest(
    std::string_view metric,
    std::string_view entity) const {
    
    if (!db_) return std::nullopt;
    
    std::string prefix = makePrefix(metric, entity);
    std::string end_key = prefix + "\xFF"; // Seek to end of prefix range
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_opts, cf_));
    
    it->SeekForPrev(end_key);
    
    if (it->Valid() && it->key().starts_with(prefix)) {
        try {
            nlohmann::json j = nlohmann::json::parse(it->value().ToString());
            return DataPoint::fromJson(j);
        } catch (const std::exception& e) {
            THEMIS_WARN("TimeSeriesStore::getLatest - Failed to parse data point: {}", e.what());
        }
    }
    
    return std::nullopt;
}

} // namespace themis
