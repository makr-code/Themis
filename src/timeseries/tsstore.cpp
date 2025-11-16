#include "timeseries/tsstore.h"
#include "utils/logger.h"
#include "timeseries/gorilla.h"
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/write_batch.h>
#include <sstream>
#include <iomanip>
#include <map>
#include <algorithm>

namespace themis {

// ===== DataPoint JSON Serialization =====

nlohmann::json TSStore::DataPoint::toJson() const {
    nlohmann::json j;
    j["metric"] = metric;
    j["entity"] = entity;
    j["timestamp_ms"] = timestamp_ms;
    j["value"] = value;
    j["tags"] = tags;
    j["metadata"] = metadata;
    return j;
}

TSStore::DataPoint TSStore::DataPoint::fromJson(const nlohmann::json& j) {
    DataPoint point;
    point.metric = j.value("metric", "");
    point.entity = j.value("entity", "");
    point.timestamp_ms = j.value("timestamp_ms", int64_t(0));
    point.value = j.value("value", 0.0);
    point.tags = j.value("tags", nlohmann::json::object());
    point.metadata = j.value("metadata", nlohmann::json::object());
    return point;
}

// ===== TimeSeriesStore Implementation =====

TSStore::TSStore(rocksdb::TransactionDB* db, 
                 rocksdb::ColumnFamilyHandle* cf,
                 Config config)
    : db_(db), cf_(cf), config_(std::move(config)) {
    if (!db_) {
        throw std::invalid_argument("TimeSeriesStore: db cannot be null");
    }
}

// Delegating constructor: default config
// Delegating constructor: default config (matches header convenience ctor)
TSStore::TSStore(rocksdb::TransactionDB* db, rocksdb::ColumnFamilyHandle* cf)
    : TSStore(db, cf, Config{}) {}

std::string TSStore::makeKey(const std::string& metric, 
                                     const std::string& entity, 
                                     int64_t timestamp_ms) const {
    // Format: "ts:{metric}:{entity}:{timestamp_ms_padded}"
    // Zero-pad timestamp for lexicographic ordering
    std::ostringstream oss;
    oss << KEY_PREFIX << metric << ":" << entity << ":" 
        << std::setw(20) << std::setfill('0') << timestamp_ms;
    return oss.str();
}

std::optional<TSStore::KeyComponents> 
TSStore::parseKey(const std::string& key) const {
    // Expected format: "ts:{metric}:{entity}:{timestamp_ms}"
    if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
        return std::nullopt;
    }
    
    size_t pos1 = strlen(KEY_PREFIX);
    size_t pos2 = key.find(':', pos1);
    if (pos2 == std::string::npos) return std::nullopt;
    
    size_t pos3 = key.find(':', pos2 + 1);
    if (pos3 == std::string::npos) return std::nullopt;
    
    KeyComponents comp;
    comp.metric = key.substr(pos1, pos2 - pos1);
    comp.entity = key.substr(pos2 + 1, pos3 - pos2 - 1);
    
    try {
        comp.timestamp_ms = std::stoll(key.substr(pos3 + 1));
    } catch (...) {
        return std::nullopt;
    }
    
    return comp;
}

bool TSStore::matchesTagFilter(const DataPoint& point, 
                                       const nlohmann::json& tag_filter) const {
    if (tag_filter.is_null() || tag_filter.empty()) {
        return true; // No filter = match all
    }
    
    for (auto it = tag_filter.begin(); it != tag_filter.end(); ++it) {
        const std::string& tag_key = it.key();
        const auto& tag_value = it.value();
        
        if (!point.tags.contains(tag_key) || point.tags[tag_key] != tag_value) {
            return false;
        }
    }
    
    return true;
}

TSStore::Status TSStore::putDataPoint(const DataPoint& point) {
    if (point.metric.empty()) {
        return Status::Error("Metric name cannot be empty");
    }
    if (point.entity.empty()) {
        return Status::Error("Entity ID cannot be empty");
    }
    
    // For Gorilla compression, we'd need to buffer points and flush in chunks
    // For now, fall back to raw storage for single points with Gorilla config
    // TODO: Implement buffering strategy for single-point inserts with Gorilla
    
    std::string key = makeKey(point.metric, point.entity, point.timestamp_ms);
    std::string value = point.toJson().dump();
    
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s;
    
    if (cf_) {
        s = db_->Put(write_opts, cf_, key, value);
    } else {
        s = db_->Put(write_opts, key, value);
    }
    
    if (!s.ok()) {
        THEMIS_ERROR("Failed to write data point {}: {}", key, s.ToString());
        return Status::Error("Failed to write data point: " + s.ToString());
    }
    
    THEMIS_DEBUG("Wrote data point: metric={}, entity={}, timestamp={}, value={}, compression={}", 
                 point.metric, point.entity, point.timestamp_ms, point.value,
                 config_.compression == CompressionType::Gorilla ? "gorilla" : "none");
    
    return Status::OK();
}

TSStore::Status TSStore::putDataPoints(const std::vector<DataPoint>& points) {
    if (points.empty()) {
        return Status::OK();
    }
    
    // If Gorilla compression is enabled, group points by metric+entity and compress
    if (config_.compression == CompressionType::Gorilla) {
        // Group points by metric:entity
        std::map<std::string, std::vector<DataPoint>> grouped;
        for (const auto& point : points) {
            if (point.metric.empty() || point.entity.empty()) {
                return Status::Error("Invalid data point: metric and entity cannot be empty");
            }
            std::string group_key = point.metric + ":" + point.entity;
            grouped[group_key].push_back(point);
        }
        
        rocksdb::WriteBatch batch;
        
        for (auto& [group_key, group_points] : grouped) {
            // Sort by timestamp for Gorilla efficiency
            std::sort(group_points.begin(), group_points.end(),
                [](const DataPoint& a, const DataPoint& b) {
                    return a.timestamp_ms < b.timestamp_ms;
                });
            
            // Extract timestamps and values
            std::vector<int64_t> timestamps;
            std::vector<double> values;
            timestamps.reserve(group_points.size());
            values.reserve(group_points.size());
            
            for (const auto& p : group_points) {
                timestamps.push_back(p.timestamp_ms);
                values.push_back(p.value);
            }
            
            // Compress with Gorilla
            try {
                GorillaEncoder encoder;
                for (size_t i = 0; i < timestamps.size(); ++i) {
                    encoder.add(timestamps[i], values[i]);
                }
                std::vector<uint8_t> compressed = encoder.finish();
                
                // Store compressed chunk with special prefix
                // Key format: "tsc:{metric}:{entity}:{first_ts}:{last_ts}"
                std::string chunk_key = std::string(GORILLA_CHUNK_PREFIX) +
                    group_points.front().metric + ":" +
                    group_points.front().entity + ":" +
                    std::to_string(timestamps.front()) + ":" +
                    std::to_string(timestamps.back());
                
                // Store tags/metadata from first point (assuming homogeneous in chunk)
                nlohmann::json chunk_meta;
                chunk_meta["compression"] = "gorilla";
                chunk_meta["count"] = group_points.size();
                chunk_meta["tags"] = group_points.front().tags;
                chunk_meta["metadata"] = group_points.front().metadata;
                chunk_meta["data"] = nlohmann::json::binary(compressed);
                
                std::string value = chunk_meta.dump();
                
                if (cf_) {
                    batch.Put(cf_, chunk_key, value);
                } else {
                    batch.Put(chunk_key, value);
                }
                
                THEMIS_DEBUG("Gorilla compressed {} points for {} (ratio: {:.2f}x)",
                    group_points.size(), group_key,
                    static_cast<double>(group_points.size() * (sizeof(int64_t) + sizeof(double))) / compressed.size());
                
            } catch (const std::exception& e) {
                THEMIS_ERROR("Gorilla compression failed for {}: {}", group_key, e.what());
                return Status::Error("Gorilla compression failed: " + std::string(e.what()));
            }
        }
        
        rocksdb::WriteOptions write_opts;
        rocksdb::Status s = db_->Write(write_opts, &batch);
        
        if (!s.ok()) {
            THEMIS_ERROR("Failed to write Gorilla-compressed batch: {}", s.ToString());
            return Status::Error("Failed to write batch: " + s.ToString());
        }
        
        THEMIS_INFO("Wrote Gorilla-compressed batch of {} data points ({} chunks)", 
            points.size(), grouped.size());
        return Status::OK();
    }
    
    // No compression: write raw JSON
    rocksdb::WriteBatch batch;
    
    for (const auto& point : points) {
        if (point.metric.empty() || point.entity.empty()) {
            return Status::Error("Invalid data point: metric and entity cannot be empty");
        }
        
        std::string key = makeKey(point.metric, point.entity, point.timestamp_ms);
        std::string value = point.toJson().dump();
        
        if (cf_) {
            batch.Put(cf_, key, value);
        } else {
            batch.Put(key, value);
        }
    }
    
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s = db_->Write(write_opts, &batch);
    
    if (!s.ok()) {
        THEMIS_ERROR("Failed to write batch of {} data points: {}", points.size(), s.ToString());
        return Status::Error("Failed to write batch: " + s.ToString());
    }
    
    THEMIS_INFO("Wrote batch of {} data points (raw)", points.size());
    return Status::OK();
}

std::pair<TSStore::Status, std::vector<TSStore::DataPoint>>
TSStore::query(const QueryOptions& options) const {
    std::vector<DataPoint> results;
    
    if (options.metric.empty()) {
        return {Status::Error("Metric name is required"), results};
    }
    
    // Scan both raw data (ts:) and compressed chunks (tsc:)
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    size_t count = 0;
    
    // First, scan raw data points (ts: prefix)
    {
        std::string start_key, end_key;
        
        if (options.entity.has_value()) {
            start_key = makeKey(options.metric, *options.entity, options.from_timestamp_ms);
            end_key = makeKey(options.metric, *options.entity, options.to_timestamp_ms);
        } else {
            start_key = KEY_PREFIX + options.metric + ":";
            end_key = KEY_PREFIX + options.metric + ";";
        }
        
        it->Seek(start_key);
        
        while (it->Valid() && count < options.limit) {
            std::string key = it->key().ToString();
            
            if (key > end_key) break;
            
            if (!options.entity.has_value()) {
                std::string expected_prefix = KEY_PREFIX + options.metric + ":";
                if (key.compare(0, expected_prefix.size(), expected_prefix) != 0) {
                    break;
                }
            }
            
            try {
                nlohmann::json j = nlohmann::json::parse(it->value().ToString());
                DataPoint point = DataPoint::fromJson(j);
                
                if (point.timestamp_ms >= options.from_timestamp_ms && 
                    point.timestamp_ms <= options.to_timestamp_ms &&
                    matchesTagFilter(point, options.tag_filter)) {
                    results.push_back(point);
                    count++;
                }
            } catch (const std::exception& e) {
                THEMIS_WARN("Failed to parse data point at key {}: {}", key, e.what());
            }
            
            it->Next();
        }
    }
    
    // Second, scan Gorilla-compressed chunks (tsc: prefix)
    if (count < options.limit) {
        std::string start_key, end_key;
        
        if (options.entity.has_value()) {
            start_key = std::string(GORILLA_CHUNK_PREFIX) + options.metric + ":" + *options.entity + ":";
            end_key = std::string(GORILLA_CHUNK_PREFIX) + options.metric + ":" + *options.entity + ";";
        } else {
            start_key = std::string(GORILLA_CHUNK_PREFIX) + options.metric + ":";
            end_key = std::string(GORILLA_CHUNK_PREFIX) + options.metric + ";";
        }
        
        it->Seek(start_key);
        
        while (it->Valid() && count < options.limit) {
            std::string key = it->key().ToString();
            
            if (key > end_key) break;
            
            try {
                nlohmann::json chunk_meta = nlohmann::json::parse(it->value().ToString());
                
                if (!chunk_meta.contains("compression") || chunk_meta["compression"] != "gorilla") {
                    it->Next();
                    continue;
                }
                
                // Decode Gorilla-compressed data
                auto compressed_data = chunk_meta["data"].get<std::vector<uint8_t>>();
                GorillaDecoder decoder(compressed_data);
                
                // Extract tags/metadata from chunk
                nlohmann::json tags = chunk_meta.value("tags", nlohmann::json::object());
                nlohmann::json metadata = chunk_meta.value("metadata", nlohmann::json::object());
                
                // Parse chunk key to get metric and entity
                // Key format: "tsc:{metric}:{entity}:{first_ts}:{last_ts}"
                size_t pos1 = strlen(GORILLA_CHUNK_PREFIX);
                size_t pos2 = key.find(':', pos1);
                size_t pos3 = key.find(':', pos2 + 1);
                
                std::string metric = key.substr(pos1, pos2 - pos1);
                std::string entity = key.substr(pos2 + 1, pos3 - pos2 - 1);
                
                // Decode all points from chunk
                while (auto point_opt = decoder.next()) {
                    auto [timestamp_ms, value] = *point_opt;
                    
                    // Check timestamp bounds
                    if (timestamp_ms < options.from_timestamp_ms || timestamp_ms > options.to_timestamp_ms) {
                        continue;
                    }
                    
                    DataPoint dp;
                    dp.metric = metric;
                    dp.entity = entity;
                    dp.timestamp_ms = timestamp_ms;
                    dp.value = value;
                    dp.tags = tags;
                    dp.metadata = metadata;
                    
                    if (matchesTagFilter(dp, options.tag_filter)) {
                        results.push_back(dp);
                        count++;
                        if (count >= options.limit) break;
                    }
                }
                
            } catch (const std::exception& e) {
                THEMIS_WARN("Failed to decode Gorilla chunk at key {}: {}", key, e.what());
            }
            
            it->Next();
        }
    }
    
    // Sort results by timestamp (mixed raw + compressed may be out of order)
    std::sort(results.begin(), results.end(),
        [](const DataPoint& a, const DataPoint& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });
    
    if (!it->status().ok()) {
        return {Status::Error("Scan failed: " + it->status().ToString()), results};
    }
    
    THEMIS_DEBUG("Query returned {} data points for metric={}", results.size(), options.metric);
    return {Status::OK(), results};
}

std::pair<TSStore::Status, TSStore::AggregationResult>
TSStore::aggregate(const QueryOptions& options) const {
    AggregationResult result;
    
    auto [status, data_points] = query(options);
    if (!status.ok) {
        return {status, result};
    }
    
    if (data_points.empty()) {
        return {Status::OK(), result};
    }
    
    result.count = data_points.size();
    result.min = data_points[0].value;
    result.max = data_points[0].value;
    result.sum = 0.0;
    result.first_timestamp_ms = data_points[0].timestamp_ms;
    result.last_timestamp_ms = data_points[data_points.size() - 1].timestamp_ms;
    
    for (const auto& point : data_points) {
        result.min = std::min(result.min, point.value);
        result.max = std::max(result.max, point.value);
        result.sum += point.value;
    }
    
    result.avg = result.sum / static_cast<double>(result.count);
    
    THEMIS_DEBUG("Aggregation: count={}, min={}, max={}, avg={}, sum={}", 
                 result.count, result.min, result.max, result.avg, result.sum);
    
    return {Status::OK(), result};
}

TSStore::Stats TSStore::getStats() const {
    Stats stats;
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    std::unordered_set<std::string> unique_metrics;
    int64_t oldest_ts = INT64_MAX;
    int64_t newest_ts = 0;
    size_t total_size = 0;
    
    it->Seek(KEY_PREFIX);
    
    while (it->Valid()) {
        std::string key = it->key().ToString();
        
        // Stop if we've left the time-series prefix
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        auto comp = parseKey(key);
        if (comp.has_value()) {
            unique_metrics.insert(comp->metric);
            oldest_ts = std::min(oldest_ts, comp->timestamp_ms);
            newest_ts = std::max(newest_ts, comp->timestamp_ms);
            total_size += key.size() + it->value().size();
            stats.total_data_points++;
        }
        
        it->Next();
    }
    
    stats.total_metrics = unique_metrics.size();
    stats.oldest_timestamp_ms = (oldest_ts == INT64_MAX) ? 0 : oldest_ts;
    stats.newest_timestamp_ms = newest_ts;
    stats.total_size_bytes = total_size;
    
    return stats;
}

size_t TSStore::deleteOldData(int64_t before_timestamp_ms) {
    size_t deleted_count = 0;
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    rocksdb::WriteBatch batch;
    
    it->Seek(KEY_PREFIX);
    
    while (it->Valid()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        auto comp = parseKey(key);
        if (comp.has_value() && comp->timestamp_ms < before_timestamp_ms) {
            if (cf_) {
                batch.Delete(cf_, key);
            } else {
                batch.Delete(key);
            }
            deleted_count++;
        }
        
        it->Next();
    }
    
    if (deleted_count > 0) {
        rocksdb::WriteOptions write_opts;
        rocksdb::Status s = db_->Write(write_opts, &batch);
        
        if (!s.ok()) {
            THEMIS_ERROR("Failed to delete old data: {}", s.ToString());
            return 0;
        }
        
        THEMIS_INFO("Deleted {} old data points (before timestamp {})", 
                    deleted_count, before_timestamp_ms);
    }
    
    return deleted_count;
}

size_t TSStore::deleteOldDataForMetric(const std::string& metric, int64_t before_timestamp_ms) {
    if (metric.empty()) return 0;
    size_t deleted_count = 0;
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    if (cf_) it.reset(db_->NewIterator(read_opts, cf_)); else it.reset(db_->NewIterator(read_opts));

    std::string prefix = KEY_PREFIX + metric + ":";
    std::string end_key;
    {
        // end_key as first key with timestamp >= cutoff for any entity; we'll check entities in loop
        // We will iterate all keys with metric prefix and delete those with timestamp < cutoff
    }

    rocksdb::WriteBatch batch;
    it->Seek(prefix);
    while (it->Valid()) {
        std::string key = it->key().ToString();
        if (key.compare(0, prefix.size(), prefix) != 0) break;
        auto comp = parseKey(key);
        if (comp.has_value() && comp->metric == metric && comp->timestamp_ms < before_timestamp_ms) {
            if (cf_) batch.Delete(cf_, key); else batch.Delete(key);
            deleted_count++;
        }
        it->Next();
    }
    if (deleted_count > 0) {
        rocksdb::WriteOptions write_opts;
        rocksdb::Status s = db_->Write(write_opts, &batch);
        if (!s.ok()) {
            THEMIS_ERROR("Failed to delete old data for metric {}: {}", metric, s.ToString());
            return 0;
        }
        THEMIS_INFO("Deleted {} old data points for metric {} (before {})", deleted_count, metric, before_timestamp_ms);
    }
    return deleted_count;
}

TSStore::Status TSStore::deleteMetric(const std::string& metric) {
    if (metric.empty()) {
        return Status::Error("Metric name cannot be empty");
    }
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    rocksdb::WriteBatch batch;
    std::string prefix = KEY_PREFIX + metric + ":";
    size_t count = 0;
    
    it->Seek(prefix);
    
    while (it->Valid()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        
        if (cf_) {
            batch.Delete(cf_, key);
        } else {
            batch.Delete(key);
        }
        count++;
        it->Next();
    }
    
    if (count > 0) {
        rocksdb::WriteOptions write_opts;
        rocksdb::Status s = db_->Write(write_opts, &batch);
        
        if (!s.ok()) {
            THEMIS_ERROR("Failed to delete metric {}: {}", metric, s.ToString());
            return Status::Error("Failed to delete metric: " + s.ToString());
        }
        
        THEMIS_INFO("Deleted metric {} ({} data points)", metric, count);
    }
    
    return Status::OK();
}

void TSStore::clear() {
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    rocksdb::WriteBatch batch;
    size_t count = 0;
    
    it->Seek(KEY_PREFIX);
    
    while (it->Valid()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        if (cf_) {
            batch.Delete(cf_, key);
        } else {
            batch.Delete(key);
        }
        count++;
        it->Next();
    }
    
    if (count > 0) {
        rocksdb::WriteOptions write_opts;
        db_->Write(write_opts, &batch);
        THEMIS_INFO("Cleared all time-series data ({} data points)", count);
    }
}

} // namespace themis
