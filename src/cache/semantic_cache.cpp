#include "cache/semantic_cache.h"
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <rocksdb/iterator.h>

namespace themis {

// CacheEntry JSON serialization
nlohmann::json SemanticCache::CacheEntry::toJson() const {
    return {
        {"response", response},
        {"metadata", metadata},
        {"timestamp_ms", timestamp_ms},
        {"ttl_seconds", ttl_seconds}
    };
}

std::optional<SemanticCache::CacheEntry> SemanticCache::CacheEntry::fromJson(const nlohmann::json& j) {
    try {
        CacheEntry entry;
        entry.response = j.at("response").get<std::string>();
        entry.metadata = j.value("metadata", nlohmann::json::object());
        entry.timestamp_ms = j.at("timestamp_ms").get<int64_t>();
        entry.ttl_seconds = j.at("ttl_seconds").get<int>();
        return entry;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Stats JSON serialization
nlohmann::json SemanticCache::Stats::toJson() const {
    return {
        {"hit_count", hit_count},
        {"miss_count", miss_count},
        {"total_entries", total_entries},
        {"total_size_bytes", total_size_bytes},
        {"hit_rate", hit_rate},
        {"avg_latency_ms", avg_latency_ms}
    };
}

// Constructor
SemanticCache::SemanticCache(
    rocksdb::TransactionDB* db,
    rocksdb::ColumnFamilyHandle* cf_handle,
    int default_ttl_seconds
) : db_(db), cf_handle_(cf_handle), default_ttl_seconds_(default_ttl_seconds) {}

// Compute SHA256 hash of prompt + params
std::string SemanticCache::computeKey(const std::string& prompt, const nlohmann::json& params) const {
    std::string input = prompt + params.dump();
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

int64_t SemanticCache::getCurrentTimestampMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool SemanticCache::isExpired(const CacheEntry& entry) const {
    if (entry.ttl_seconds <= 0) {
        return false; // No expiry
    }
    
    int64_t now_ms = getCurrentTimestampMs();
    int64_t expiry_ms = entry.timestamp_ms + (entry.ttl_seconds * 1000);
    return now_ms > expiry_ms;
}

bool SemanticCache::put(
    const std::string& prompt,
    const nlohmann::json& params,
    const std::string& response,
    const nlohmann::json& metadata,
    int ttl_seconds
) {
    std::string key = computeKey(prompt, params);
    
    int effective_ttl = (ttl_seconds == 0) ? default_ttl_seconds_ : ttl_seconds;
    
    CacheEntry entry;
    entry.response = response;
    entry.metadata = metadata;
    entry.timestamp_ms = getCurrentTimestampMs();
    entry.ttl_seconds = effective_ttl;
    
    std::string value = entry.toJson().dump();
    
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s;
    if (cf_handle_) {
        s = db_->Put(write_opts, cf_handle_, key, value);
    } else {
        s = db_->Put(write_opts, key, value);
    }
    
    return s.ok();
}

std::optional<SemanticCache::CacheEntry> SemanticCache::query(
    const std::string& prompt,
    const nlohmann::json& params
) {
    auto start = std::chrono::steady_clock::now();
    
    std::string key = computeKey(prompt, params);
    std::string value;
    
    rocksdb::ReadOptions read_opts;
    rocksdb::Status s;
    if (cf_handle_) {
        s = db_->Get(read_opts, cf_handle_, key, &value);
    } else {
        s = db_->Get(read_opts, key, &value);
    }
    
    auto end = std::chrono::steady_clock::now();
    double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
    total_query_latency_ms_ += latency_ms;
    
    if (!s.ok()) {
        miss_count_++;
        return std::nullopt;
    }
    
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(value);
    } catch (const std::exception&) {
        miss_count_++;
        return std::nullopt;
    }
    
    auto entry = CacheEntry::fromJson(j);
    if (!entry || isExpired(*entry)) {
        miss_count_++;
        return std::nullopt;
    }
    
    hit_count_++;
    return entry;
}

SemanticCache::Stats SemanticCache::getStats() const {
    Stats stats;
    stats.hit_count = hit_count_;
    stats.miss_count = miss_count_;
    
    uint64_t total_queries = hit_count_ + miss_count_;
    if (total_queries > 0) {
        stats.hit_rate = static_cast<double>(hit_count_) / total_queries;
        stats.avg_latency_ms = total_query_latency_ms_ / total_queries;
    }
    
    // Count entries in cache
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it(
        cf_handle_ ? db_->NewIterator(read_opts, cf_handle_) : db_->NewIterator(read_opts)
    );
    
    uint64_t count = 0;
    uint64_t size = 0;
    
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        count++;
        size += it->key().size() + it->value().size();
    }
    
    stats.total_entries = count;
    stats.total_size_bytes = size;
    
    return stats;
}

uint64_t SemanticCache::clearExpired() {
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it(
        cf_handle_ ? db_->NewIterator(read_opts, cf_handle_) : db_->NewIterator(read_opts)
    );
    
    rocksdb::WriteBatch batch;
    uint64_t removed = 0;
    
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        std::string value = it->value().ToString();
        
        try {
            nlohmann::json j = nlohmann::json::parse(value);
            auto entry = CacheEntry::fromJson(j);
            
            if (entry && isExpired(*entry)) {
                if (cf_handle_) batch.Delete(cf_handle_, it->key());
                else batch.Delete(it->key());
                removed++;
            }
        } catch (const std::exception&) {
            // Invalid entry, remove it
            if (cf_handle_) batch.Delete(cf_handle_, it->key());
            else batch.Delete(it->key());
            removed++;
        }
    }
    
    if (removed > 0) {
        rocksdb::WriteOptions write_opts;
        db_->Write(write_opts, &batch);
    }
    
    return removed;
}

bool SemanticCache::clear() {
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it(
        cf_handle_ ? db_->NewIterator(read_opts, cf_handle_) : db_->NewIterator(read_opts)
    );
    
    rocksdb::WriteBatch batch;
    
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
    if (cf_handle_) batch.Delete(cf_handle_, it->key());
    else batch.Delete(it->key());
    }
    
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s = db_->Write(write_opts, &batch);
    
    // Reset metrics
    hit_count_ = 0;
    miss_count_ = 0;
    total_query_latency_ms_ = 0.0;
    
    return s.ok();
}

} // namespace themis
