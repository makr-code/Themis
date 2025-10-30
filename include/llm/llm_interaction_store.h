#ifndef THEMIS_LLM_INTERACTION_STORE_H
#define THEMIS_LLM_INTERACTION_STORE_H

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstdint>
#include <nlohmann/json.hpp>

// Forward declarations for RocksDB types
namespace rocksdb {
    class TransactionDB;
    class ColumnFamilyHandle;
}

namespace themis {

/**
 * @brief LLM Interaction Store - persists and retrieves LLM conversation/interaction data
 * 
 * Features:
 * - Chain-of-Thought (CoT) storage: structured reasoning steps
 * - Prompt template versioning
 * - Token count and latency tracking
 * - Metadata (model version, feedback, etc.)
 * 
 * Storage: RocksDB with JSON serialization
 * Key format: "llm_interaction:{interaction_id}"
 */
class LLMInteractionStore {
public:
    struct Interaction {
        std::string id;                        // UUID or generated ID
        std::string prompt_template_id;        // Reference to prompt template version
        std::string prompt;                    // Actual prompt sent
        std::vector<std::string> reasoning_chain; // CoT steps
        std::string response;                  // Final LLM response
        std::string model_version;             // e.g., "gpt-4o-mini"
        int64_t timestamp_ms;                  // Creation timestamp
        int latency_ms;                        // Response latency
        int token_count;                       // Total tokens used
        nlohmann::json metadata;               // Additional fields (feedback, user_id, etc.)

        // Serialization
        nlohmann::json toJson() const;
        static Interaction fromJson(const nlohmann::json& j);
    };

    struct ListOptions {
        size_t limit = 100;                    // Max interactions to return
        std::optional<std::string> start_after_id; // Pagination cursor
        std::optional<std::string> filter_model;   // Filter by model version
        std::optional<int64_t> since_timestamp_ms; // Filter by time
    };

    struct Stats {
        size_t total_interactions;
        int64_t total_tokens;
        double avg_latency_ms;
        size_t total_size_bytes;
    };

    /**
     * @brief Construct LLMInteractionStore
     * @param db RocksDB TransactionDB instance (not owned)
     * @param cf Optional column family handle (nullptr = default CF)
     */
    explicit LLMInteractionStore(rocksdb::TransactionDB* db, 
                                   rocksdb::ColumnFamilyHandle* cf = nullptr);

    ~LLMInteractionStore() = default;

    /**
     * @brief Store a new interaction
     * @param interaction Interaction to store (id will be generated if empty)
     * @return Stored interaction with generated ID
     */
    Interaction createInteraction(Interaction interaction);

    /**
     * @brief Retrieve interaction by ID
     * @param id Interaction ID
     * @return Interaction if found, nullopt otherwise
     */
    std::optional<Interaction> getInteraction(const std::string& id) const;

    /**
     * @brief List interactions with optional filters
     * @param options List options (pagination, filters)
     * @return Vector of interactions
     */
    std::vector<Interaction> listInteractions(const ListOptions& options = {}) const;

    /**
     * @brief Get store statistics
     * @return Stats struct
     */
    Stats getStats() const;

    /**
     * @brief Delete interaction by ID
     * @param id Interaction ID
     * @return true if deleted, false if not found
     */
    bool deleteInteraction(const std::string& id);

    /**
     * @brief Clear all interactions
     */
    void clear();

private:
    rocksdb::TransactionDB* db_;
    rocksdb::ColumnFamilyHandle* cf_; // nullptr = default CF

    static constexpr const char* KEY_PREFIX = "llm_interaction:";
    
    std::string makeKey(const std::string& id) const;
    std::string generateId() const;
};

} // namespace themis

#endif // THEMIS_LLM_INTERACTION_STORE_H
