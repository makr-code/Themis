#include "llm/llm_interaction_store.h"
#include "utils/logger.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace themis {

// ===== Interaction JSON Serialization =====

nlohmann::json LLMInteractionStore::Interaction::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["prompt_template_id"] = prompt_template_id;
    j["prompt"] = prompt;
    j["reasoning_chain"] = reasoning_chain;
    j["response"] = response;
    j["model_version"] = model_version;
    j["timestamp_ms"] = timestamp_ms;
    j["latency_ms"] = latency_ms;
    j["token_count"] = token_count;
    j["metadata"] = metadata;
    return j;
}

LLMInteractionStore::Interaction LLMInteractionStore::Interaction::fromJson(const nlohmann::json& j) {
    Interaction interaction;
    interaction.id = j.value("id", "");
    interaction.prompt_template_id = j.value("prompt_template_id", "");
    interaction.prompt = j.value("prompt", "");
    
    if (j.contains("reasoning_chain") && j["reasoning_chain"].is_array()) {
        interaction.reasoning_chain = j["reasoning_chain"].get<std::vector<std::string>>();
    }
    
    interaction.response = j.value("response", "");
    interaction.model_version = j.value("model_version", "");
    interaction.timestamp_ms = j.value("timestamp_ms", int64_t(0));
    interaction.latency_ms = j.value("latency_ms", 0);
    interaction.token_count = j.value("token_count", 0);
    
    if (j.contains("metadata")) {
        interaction.metadata = j["metadata"];
    }
    
    return interaction;
}

// ===== LLMInteractionStore Implementation =====

LLMInteractionStore::LLMInteractionStore(rocksdb::TransactionDB* db, 
                                           rocksdb::ColumnFamilyHandle* cf)
    : db_(db), cf_(cf) {
    if (!db_) {
        throw std::invalid_argument("LLMInteractionStore: db cannot be null");
    }
}

std::string LLMInteractionStore::makeKey(const std::string& id) const {
    return std::string(KEY_PREFIX) + id;
}

std::string LLMInteractionStore::generateId() const {
    // Simple UUID-like ID generation (timestamp + random)
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(12) << ms
        << "-"
        << std::setw(16) << dis(gen);
    
    return oss.str();
}

LLMInteractionStore::Interaction LLMInteractionStore::createInteraction(Interaction interaction) {
    // Generate ID if empty
    if (interaction.id.empty()) {
        interaction.id = generateId();
    }
    
    // Set timestamp if not set
    if (interaction.timestamp_ms == 0) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        interaction.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }
    
    // Serialize to JSON
    std::string value = interaction.toJson().dump();
    std::string key = makeKey(interaction.id);
    
    // Store in RocksDB
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s;
    
    if (cf_) {
        s = db_->Put(write_opts, cf_, key, value);
    } else {
        s = db_->Put(write_opts, key, value);
    }
    
    if (!s.ok()) {
        THEMIS_ERROR("Failed to store LLM interaction {}: {}", interaction.id, s.ToString());
        throw std::runtime_error("Failed to store interaction: " + s.ToString());
    }
    
    THEMIS_DEBUG("Stored LLM interaction {} ({} tokens, {} ms)", 
                 interaction.id, interaction.token_count, interaction.latency_ms);
    
    return interaction;
}

std::optional<LLMInteractionStore::Interaction> LLMInteractionStore::getInteraction(const std::string& id) const {
    std::string key = makeKey(id);
    std::string value;
    
    rocksdb::ReadOptions read_opts;
    rocksdb::Status s;
    
    if (cf_) {
        s = db_->Get(read_opts, cf_, key, &value);
    } else {
        s = db_->Get(read_opts, key, &value);
    }
    
    if (s.IsNotFound()) {
        return std::nullopt;
    }
    
    if (!s.ok()) {
        THEMIS_ERROR("Failed to read LLM interaction {}: {}", id, s.ToString());
        return std::nullopt;
    }
    
    try {
        nlohmann::json j = nlohmann::json::parse(value);
        return Interaction::fromJson(j);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to parse LLM interaction {}: {}", id, e.what());
        return std::nullopt;
    }
}

std::vector<LLMInteractionStore::Interaction> LLMInteractionStore::listInteractions(const ListOptions& options) const {
    std::vector<Interaction> results;
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    // Determine start position
    std::string start_key = KEY_PREFIX;
    if (options.start_after_id.has_value()) {
        start_key = makeKey(*options.start_after_id);
        it->Seek(start_key);
        // Skip the start_after_id itself
        if (it->Valid() && it->key().ToString() == start_key) {
            it->Next();
        }
    } else {
        it->Seek(start_key);
    }
    
    // Iterate and collect
    size_t count = 0;
    for (; it->Valid() && count < options.limit; it->Next()) {
        std::string key = it->key().ToString();
        
        // Stop if we've left the llm_interaction prefix
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        try {
            nlohmann::json j = nlohmann::json::parse(it->value().ToString());
            Interaction interaction = Interaction::fromJson(j);
            
            // Apply filters
            bool matches = true;
            
            if (options.filter_model.has_value() && 
                interaction.model_version != *options.filter_model) {
                matches = false;
            }
            
            if (options.since_timestamp_ms.has_value() &&
                interaction.timestamp_ms < *options.since_timestamp_ms) {
                matches = false;
            }
            
            if (matches) {
                results.push_back(interaction);
                count++;
            }
        } catch (const std::exception& e) {
            THEMIS_WARN("Failed to parse interaction at key {}: {}", key, e.what());
            continue;
        }
    }
    
    return results;
}

LLMInteractionStore::Stats LLMInteractionStore::getStats() const {
    Stats stats{};
    
    rocksdb::ReadOptions read_opts;
    std::unique_ptr<rocksdb::Iterator> it;
    
    if (cf_) {
        it.reset(db_->NewIterator(read_opts, cf_));
    } else {
        it.reset(db_->NewIterator(read_opts));
    }
    
    it->Seek(KEY_PREFIX);
    
    int64_t total_latency = 0;
    
    for (; it->Valid(); it->Next()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, strlen(KEY_PREFIX), KEY_PREFIX) != 0) {
            break;
        }
        
        try {
            nlohmann::json j = nlohmann::json::parse(it->value().ToString());
            Interaction interaction = Interaction::fromJson(j);
            
            stats.total_interactions++;
            stats.total_tokens += interaction.token_count;
            total_latency += interaction.latency_ms;
            stats.total_size_bytes += it->key().size() + it->value().size();
            
        } catch (const std::exception& e) {
            THEMIS_WARN("Failed to parse interaction for stats: {}", e.what());
            continue;
        }
    }
    
    if (stats.total_interactions > 0) {
        stats.avg_latency_ms = static_cast<double>(total_latency) / stats.total_interactions;
    }
    
    return stats;
}

bool LLMInteractionStore::deleteInteraction(const std::string& id) {
    std::string key = makeKey(id);
    
    rocksdb::WriteOptions write_opts;
    rocksdb::Status s;
    
    if (cf_) {
        s = db_->Delete(write_opts, cf_, key);
    } else {
        s = db_->Delete(write_opts, key);
    }
    
    if (s.IsNotFound()) {
        return false;
    }
    
    if (!s.ok()) {
        THEMIS_ERROR("Failed to delete LLM interaction {}: {}", id, s.ToString());
        return false;
    }
    
    THEMIS_DEBUG("Deleted LLM interaction {}", id);
    return true;
}

void LLMInteractionStore::clear() {
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
    
    THEMIS_INFO("Cleared {} LLM interactions", count);
}

} // namespace themis
