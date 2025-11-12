#include "cdc/changefeed.h"
#include "utils/logger.h"
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/utilities/transaction.h>
#include <thread>
#include <chrono>

namespace themis {

// ===== ChangeEvent JSON Serialization =====

nlohmann::json Changefeed::ChangeEvent::toJson() const {
    nlohmann::json j;
    j["sequence"] = sequence;
    
    // Convert ChangeEventType to string
    switch (type) {
        case ChangeEventType::EVENT_PUT: j["type"] = "PUT"; break;
        case ChangeEventType::EVENT_DELETE: j["type"] = "DELETE"; break;
        case ChangeEventType::EVENT_TRANSACTION_COMMIT: j["type"] = "TRANSACTION_COMMIT"; break;
        case ChangeEventType::EVENT_TRANSACTION_ROLLBACK: j["type"] = "TRANSACTION_ROLLBACK"; break;
    }
    
    j["key"] = key;
    if (value.has_value()) {
        j["value"] = *value;
    } else {
        j["value"] = nullptr;
    }
    j["timestamp_ms"] = timestamp_ms;
    j["metadata"] = metadata;
    
    return j;
}

Changefeed::ChangeEvent Changefeed::ChangeEvent::fromJson(const nlohmann::json& j) {
    ChangeEvent event;
    event.sequence = j.value("sequence", uint64_t(0));
    
    // Parse ChangeEventType
    std::string type_str = j.value("type", "PUT");
    if (type_str == "PUT") event.type = ChangeEventType::EVENT_PUT;
    else if (type_str == "DELETE") event.type = ChangeEventType::EVENT_DELETE;
    else if (type_str == "TRANSACTION_COMMIT") event.type = ChangeEventType::EVENT_TRANSACTION_COMMIT;
    else if (type_str == "TRANSACTION_ROLLBACK") event.type = ChangeEventType::EVENT_TRANSACTION_ROLLBACK;
    else event.type = ChangeEventType::EVENT_PUT; // default
    
    event.key = j.value("key", "");
    
    if (j.contains("value") && !j["value"].is_null()) {
        event.value = j["value"].get<std::string>();
    }
    
    event.timestamp_ms = j.value("timestamp_ms", int64_t(0));
    
    if (j.contains("metadata")) {
        event.metadata = j["metadata"];
    }
    
    return event;
}

// ===== Changefeed Implementation =====

Changefeed::Changefeed(rocksdb::TransactionDB* db, 
                       rocksdb::ColumnFamilyHandle* cf)
    : db_(db), cf_(cf) {
    if (!db_) {
        throw std::invalid_argument("Changefeed: db cannot be null");
    }
}

std::string Changefeed::makeKey(uint64_t sequence) const {
    // Zero-pad sequence for lexicographic ordering
    char buf[128];
    snprintf(buf, sizeof(buf), "%s%020llu", KEY_PREFIX, (unsigned long long)sequence);
    return std::string(buf);
}

uint64_t Changefeed::nextSequence() {
    // Simple atomic increment using RocksDB merge operator would be ideal,
    // but for MVP we'll do a read-modify-write with a transaction
    
    std::string seq_value;
    rocksdb::ReadOptions read_opts;
    rocksdb::Status s;
    
    if (cf_) {
        s = db_->Get(read_opts, cf_, SEQUENCE_KEY, &seq_value);
    } else {
        s = db_->Get(read_opts, SEQUENCE_KEY, &seq_value);
    }
    
    uint64_t next_seq = 1;
    if (s.ok() && !seq_value.empty()) {
        next_seq = std::stoull(seq_value) + 1;
    }
    
    // Write back incremented sequence
    rocksdb::WriteOptions write_opts;
    std::string next_seq_str = std::to_string(next_seq);
    
    if (cf_) {
        db_->Put(write_opts, cf_, SEQUENCE_KEY, next_seq_str);
    } else {
        db_->Put(write_opts, SEQUENCE_KEY, next_seq_str);
    }
    
    return next_seq;
}

Changefeed::ChangeEvent Changefeed::recordEvent(ChangeEvent event) {
    // Assign sequence number
    event.sequence = nextSequence();
    
    // Set timestamp if not set
    if (event.timestamp_ms == 0) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        event.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }
    
    // Serialize to JSON
    std::string value = event.toJson().dump();
    std::string key = makeKey(event.sequence);
    
    // Store in RocksDB
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s;
    
    if (cf_) {
        s = db_->Put(write_opts, cf_, key, value);
    } else {
        s = db_->Put(write_opts, key, value);
    }
    
    if (!s.ok()) {
        THEMIS_ERROR("Failed to record change event {}: {}", event.sequence, s.ToString());
        throw std::runtime_error("Failed to record change event: " + s.ToString());
    }
    
    THEMIS_DEBUG("Recorded change event {} (type={}, key={})", 
                 event.sequence, static_cast<int>(event.type), event.key);
    
    return event;
}

std::vector<Changefeed::ChangeEvent> Changefeed::listEvents(const ListOptions& options) const {
    std::vector<ChangeEvent> results;
    
    // Long-poll support: wait for events if none available
    if (options.long_poll_ms > 0) {
        uint64_t latest = getLatestSequence();
        if (latest <= options.from_sequence) {
            // No new events, wait
            waitForEvents(options.from_sequence, options.long_poll_ms);
        }
    }
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    // Start from requested sequence
    std::string start_key = makeKey(options.from_sequence + 1);
    it->Seek(start_key);
    
    size_t count = 0;
    for (; it->Valid() && count < options.limit; it->Next()) {
        std::string key = it->key().ToString();
        
        // Stop if we've left the changefeed prefix
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        try {
            nlohmann::json j = nlohmann::json::parse(it->value().ToString());
            ChangeEvent event = ChangeEvent::fromJson(j);
            
            // Apply filters
            bool matches = true;
            
            if (options.key_prefix.has_value() &&
                event.key.find(*options.key_prefix) != 0) {
                matches = false;
            }
            
            if (options.event_type.has_value() &&
                event.type != *options.event_type) {
                matches = false;
            }
            
            if (matches) {
                results.push_back(event);
                count++;
            }
        } catch (const std::exception& e) {
            THEMIS_WARN("Failed to parse change event at key {}: {}", key, e.what());
            continue;
        }
    }
    
    return results;
}

std::vector<Changefeed::ChangeEvent> Changefeed::listEvents() const {
    return listEvents(ListOptions{});
}

uint64_t Changefeed::getLatestSequence() const {
    std::string seq_value;
    rocksdb::ReadOptions read_opts;
    rocksdb::Status s;
    
    if (cf_) {
        s = db_->Get(read_opts, cf_, SEQUENCE_KEY, &seq_value);
    } else {
        s = db_->Get(read_opts, SEQUENCE_KEY, &seq_value);
    }
    
    if (s.ok() && !seq_value.empty()) {
        return std::stoull(seq_value);
    }
    
    return 0;
}

bool Changefeed::waitForEvents(uint64_t from_sequence, uint32_t timeout_ms) const {
    // Simple polling implementation (production would use event notifications)
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms);
    
    while (true) {
        uint64_t latest = getLatestSequence();
        if (latest > from_sequence) {
            return true; // New events available
        }
        
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            return false; // Timeout
        }
        
        // Sleep for a short interval before checking again
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

Changefeed::Stats Changefeed::getStats() const {
    Stats stats{};
    stats.latest_sequence = getLatestSequence();
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    it->Seek(KEY_PREFIX);
    
    for (; it->Valid(); it->Next()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        stats.total_events++;
        stats.total_size_bytes += it->key().size() + it->value().size();
    }
    
    return stats;
}

void Changefeed::clear() {
    rocksdb::ReadOptions read_opts;
    rocksdb::WriteOptions write_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    it->Seek(KEY_PREFIX);
    
    size_t count = 0;
    for (; it->Valid(); it->Next()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        rocksdb::Status s;
        if (cf_) {
            s = db_->Delete(write_opts, cf_, key);
        } else {
            s = db_->Delete(write_opts, key);
        }
        
        if (s.ok()) {
            count++;
        }
    }
    
    // Reset sequence counter
    if (cf_) {
        db_->Put(write_opts, cf_, SEQUENCE_KEY, "0");
    } else {
        db_->Put(write_opts, SEQUENCE_KEY, "0");
    }
    
    THEMIS_INFO("Cleared {} change events", count);
}

size_t Changefeed::deleteOldEvents(uint64_t before_sequence) {
    rocksdb::ReadOptions read_opts;
    rocksdb::WriteOptions write_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    it->Seek(KEY_PREFIX);
    
    size_t count = 0;
    for (; it->Valid(); it->Next()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        try {
            nlohmann::json j = nlohmann::json::parse(it->value().ToString());
            ChangeEvent event = ChangeEvent::fromJson(j);
            
            if (event.sequence < before_sequence) {
                rocksdb::Status s;
                if (cf_) {
                    s = db_->Delete(write_opts, cf_, key);
                } else {
                    s = db_->Delete(write_opts, key);
                }
                
                if (s.ok()) {
                    count++;
                }
            }
        } catch (const std::exception& e) {
            THEMIS_WARN("Failed to parse change event for deletion: {}", e.what());
            continue;
        }
    }
    
    THEMIS_INFO("Deleted {} old change events (before sequence {})", count, before_sequence);
    return count;
}

} // namespace themis
