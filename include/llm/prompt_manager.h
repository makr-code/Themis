#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <nlohmann/json.hpp>
// Forward declaration
namespace rocksdb { class ColumnFamilyHandle; }

namespace themis {
class RocksDBWrapper;

class PromptManager {
public:
    struct PromptTemplate {
        std::string id;           // generated id
        std::string name;         // human readable name
        std::string version;      // version string, e.g. "v1", "2.3"
        std::string content;      // template body
        nlohmann::json metadata;  // arbitrary metadata (experiment flags etc.)
        bool active = true;

        nlohmann::json toJson() const {
            nlohmann::json j;
            j["id"] = id;
            j["name"] = name;
            j["version"] = version;
            j["content"] = content;
            j["metadata"] = metadata;
            j["active"] = active;
            return j;
        }
    };

    // In-memory only manager
    PromptManager();

    // RocksDB-backed manager (does not take ownership of db or cf)
    PromptManager(RocksDBWrapper* db, rocksdb::ColumnFamilyHandle* cf = nullptr);
    ~PromptManager() = default;

    // Create a template; if template.id empty one is generated
    PromptTemplate createTemplate(PromptTemplate t);

    // Retrieve template by id
    std::optional<PromptTemplate> getTemplate(const std::string& id) const;

    // List all templates
    std::vector<PromptTemplate> listTemplates() const;

    // Update metadata/active flag of template; returns false if not found
    bool updateTemplate(const std::string& id, const nlohmann::json& metadata, bool active);

    // Assign an experiment id to a template (stores in metadata["experiment_id"])
    bool assignExperiment(const std::string& id, const std::string& experiment_id);

private:
    std::string generateId() const;

    mutable std::mutex mu_;
    std::unordered_map<std::string, PromptTemplate> store_;

    // Optional persistence
    RocksDBWrapper* db_ = nullptr; // not owned
    rocksdb::ColumnFamilyHandle* cf_ = nullptr; // not owned

    static constexpr const char* KEY_PREFIX = "prompt_template:";
};

} // namespace themis
