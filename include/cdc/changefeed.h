#ifndef THEMIS_CHANGEFEED_H
#define THEMIS_CHANGEFEED_H

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
 * @brief Minimal Change Data Capture (CDC) implementation
 * 
 * Features:
 * - Sequence-based event tracking
 * - Long-polling support for real-time updates
 * - Event filtering by type/key prefix
 * 
 * Implementation: Simple key-value storage of change events with sequence numbers
 * Key format: "changefeed:{sequence_number}"
 * 
 * Note: This is a minimal MVP implementation. Production-grade CDC would use:
 * - RocksDB WAL tailing for lower latency
 * - Persistent subscriptions with checkpointing
 * - Materialized views with automatic updates
 * - Stream-table duality patterns
 */
class Changefeed {
public:
    enum class ChangeEventType {
        EVENT_PUT,
        EVENT_DELETE,
        EVENT_TRANSACTION_COMMIT,
        EVENT_TRANSACTION_ROLLBACK
    };

    struct ChangeEvent {
        uint64_t sequence;                // Monotonic sequence number
        ChangeEventType type;             // Event type
        std::string key;                  // Affected key
        std::optional<std::string> value; // Value (nullopt for DELETE)
        int64_t timestamp_ms;             // Event timestamp
        nlohmann::json metadata;          // Additional metadata (tx_id, user, etc.)

        // Serialization
        nlohmann::json toJson() const;
        static ChangeEvent fromJson(const nlohmann::json& j);
    };

    struct ListOptions {
        uint64_t from_sequence = 0;       // Start after this sequence
        size_t limit = 100;               // Max events to return
        uint32_t long_poll_ms = 0;        // Long-poll timeout (0 = immediate)
        std::optional<std::string> key_prefix; // Filter by key prefix
        std::optional<ChangeEventType> event_type;   // Filter by event type
    };

    struct Stats {
        uint64_t total_events;
        uint64_t latest_sequence;
        size_t total_size_bytes;
    };

    /**
     * @brief Construct Changefeed
     * @param db RocksDB TransactionDB instance (not owned)
     * @param cf Optional column family handle (nullptr = default CF)
     */
    explicit Changefeed(rocksdb::TransactionDB* db, 
                        rocksdb::ColumnFamilyHandle* cf = nullptr);

    ~Changefeed() = default;

    /**
     * @brief Record a change event
     * @param event Event to record (sequence will be auto-generated)
     * @return Event with assigned sequence number
     */
    ChangeEvent recordEvent(ChangeEvent event);

    /**
     * @brief List change events with optional filters
     * @param options List options (pagination, filters, long-poll)
     * @return Vector of change events
     */
    std::vector<ChangeEvent> listEvents() const;
    std::vector<ChangeEvent> listEvents(const ListOptions& options) const;

    /**
     * @brief Get the latest sequence number
     * @return Latest sequence, or 0 if no events
     */
    uint64_t getLatestSequence() const;

    /**
     * @brief Get changefeed statistics
     * @return Stats struct
     */
    Stats getStats() const;

    /**
     * @brief Clear all events (admin operation)
     */
    void clear();

    /**
     * @brief Delete events older than a given sequence (retention policy)
     * @param before_sequence Delete events with sequence < this value
     * @return Number of events deleted
     */
    size_t deleteOldEvents(uint64_t before_sequence);

private:
    rocksdb::TransactionDB* db_;
    rocksdb::ColumnFamilyHandle* cf_;

    static constexpr const char* KEY_PREFIX = "changefeed:";
    static constexpr const char* SEQUENCE_KEY = "changefeed_sequence";

    std::string makeKey(uint64_t sequence) const;
    uint64_t nextSequence();
    
    // Helper to wait for new events (for long-poll)
    bool waitForEvents(uint64_t from_sequence, uint32_t timeout_ms) const;
};

} // namespace themis

#endif // THEMIS_CHANGEFEED_H
