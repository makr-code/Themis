#include "llm/prompt_manager.h"
#include "storage/rocksdb_wrapper.h"
#include "utils/logger.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstring>

namespace themis {

PromptManager::PromptManager() = default;

PromptManager::PromptManager(RocksDBWrapper* db, rocksdb::ColumnFamilyHandle* cf)
    : db_(db), cf_(cf) {}

PromptManager::PromptTemplate PromptManager::createTemplate(PromptManager::PromptTemplate t) {
    std::lock_guard<std::mutex> lock(mu_);
    if (t.id.empty()) t.id = generateId();
    store_[t.id] = t;

    // Persist if DB configured
    if (db_) {
        std::string key = std::string(KEY_PREFIX) + t.id;
        std::string v = t.toJson().dump();
        std::vector<uint8_t> bytes(v.begin(), v.end());
        bool ok = db_->put(key, bytes);
        if (!ok) {
            THEMIS_ERROR("Failed to persist prompt template {}", t.id);
        }
    }

    THEMIS_DEBUG("Created prompt template {} (version={})", t.id, t.version);
    return t;
}

std::optional<PromptManager::PromptTemplate> PromptManager::getTemplate(const std::string& id) const {
    // If persisted, try DB first
    if (db_) {
        std::string key = std::string(KEY_PREFIX) + id;
        auto val_opt = db_->get(key);
        if (val_opt.has_value()) {
            try {
                std::string s(reinterpret_cast<const char*>(val_opt->data()), val_opt->size());
                auto j = nlohmann::json::parse(s);
                    PromptTemplate t;
                // The PromptTemplate::fromJson isn't defined here; parse manually
                t.id = j.value("id", id);
                t.name = j.value("name", "");
                t.version = j.value("version", "");
                t.content = j.value("content", "");
                if (j.contains("metadata")) t.metadata = j["metadata"];
                t.active = j.value("active", true);
                return t;
            } catch (const std::exception& e) {
                THEMIS_WARN("Failed to parse persisted prompt template {}: {}", id, e.what());
            }
        }
        // fallthrough to in-memory
    }

    std::lock_guard<std::mutex> lock(mu_);
    auto it = store_.find(id);
    if (it == store_.end()) return std::nullopt;
    return it->second;
}

std::vector<PromptManager::PromptTemplate> PromptManager::listTemplates() const {
    // If persisted, scan DB
    if (db_) {
        std::vector<PromptTemplate> out;
        std::string prefix = KEY_PREFIX;
        db_->scanPrefix(prefix, [&](std::string_view key, std::string_view value) -> bool {
            (void)key; // silence unused parameter warning
            try {
                auto j = nlohmann::json::parse(std::string(value));
                PromptTemplate t;
                t.id = j.value("id", "");
                t.name = j.value("name", "");
                t.version = j.value("version", "");
                t.content = j.value("content", "");
                if (j.contains("metadata")) t.metadata = j["metadata"];
                t.active = j.value("active", true);
                out.push_back(t);
            } catch (const std::exception& e) {
                THEMIS_WARN("Failed to parse prompt template during scan: {}", e.what());
            }
            return true; // continue scanning
        });
        return out;
    }

    std::lock_guard<std::mutex> lock(mu_);
    std::vector<PromptTemplate> out;
    out.reserve(store_.size());
    for (const auto& kv : store_) out.push_back(kv.second);
    return out;
}

bool PromptManager::updateTemplate(const std::string& id, const nlohmann::json& metadata, bool active) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = store_.find(id);
    if (it == store_.end()) return false;
    it->second.metadata = metadata;
    it->second.active = active;

    if (db_) {
        std::string key = std::string(KEY_PREFIX) + id;
        nlohmann::json j = it->second.toJson();
        std::string s = j.dump();
        std::vector<uint8_t> bytes(s.begin(), s.end());
        if (!db_->put(key, bytes)) {
            THEMIS_ERROR("Failed to persist updated template {}", id);
        }
    }

    THEMIS_DEBUG("Updated prompt template {} active={} metadata={}", id, active, metadata.dump());
    return true;
}

bool PromptManager::assignExperiment(const std::string& id, const std::string& experiment_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = store_.find(id);
    if (it == store_.end()) return false;
    it->second.metadata["experiment_id"] = experiment_id;

    if (db_) {
        std::string key = std::string(KEY_PREFIX) + id;
        nlohmann::json j = it->second.toJson();
        std::string s = j.dump();
        std::vector<uint8_t> bytes(s.begin(), s.end());
        if (!db_->put(key, bytes)) {
            THEMIS_ERROR("Failed to persist experiment assignment for {}", id);
        }
    }

    THEMIS_DEBUG("Assigned experiment {} to template {}", experiment_id, id);
    return true;
}

std::string PromptManager::generateId() const {
    static thread_local std::mt19937_64 gen((std::random_device())());
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

} // namespace themis
