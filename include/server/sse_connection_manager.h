#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace themis {

class Changefeed;

namespace server {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

/**
 * @brief Manages active SSE connections for Changefeed streaming
 * 
 * Responsibilities:
 * - Track active SSE connections with unique IDs
 * - Send heartbeat comments to prevent timeout
 * - Push new events to subscribed connections
 * - Clean up on client disconnect or server shutdown
 * - Memory limits per connection (buffered events)
 */
class SseConnectionManager {
public:
    struct ConnectionConfig {
        uint32_t heartbeat_interval_ms = 15000;  // Send heartbeat every 15s
        uint32_t max_buffered_events = 1000;     // Max events per connection buffer
        uint32_t event_poll_interval_ms = 500;   // Poll changefeed every 500ms
        uint32_t retry_ms = 3000;                // SSE client reconnect delay hint
        uint32_t max_events_per_second = 0;      // 0 = unlimited (server-side rate control)
        bool drop_oldest_on_overflow = true;     // Backpressure policy: drop oldest if buffer full
    };

    struct ConnectionStats {
        size_t active_connections = 0;
        uint64_t total_events_sent = 0;
        uint64_t total_heartbeats_sent = 0;
        uint64_t total_disconnects = 0;
        uint64_t total_dropped_events = 0;
    };

    explicit SseConnectionManager(
        std::shared_ptr<Changefeed> changefeed,
        boost::asio::io_context& ioc,
        const ConnectionConfig& config = {}
    );

    ~SseConnectionManager();

    /**
     * @brief Register a new SSE connection
     * @param from_seq Starting sequence number for this connection
     * @param key_prefix Optional key prefix filter
     * @return Unique connection ID
     */
    uint64_t registerConnection(
        uint64_t from_seq,
        const std::string& key_prefix = ""
    );

    /**
     * @brief Unregister connection (called on client disconnect)
     * @param conn_id Connection ID
     */
    void unregisterConnection(uint64_t conn_id);

    /**
     * @brief Get pending events for a connection
     * @param conn_id Connection ID
     * @param max_events Max events to retrieve
     * @return Vector of event JSON strings (formatted as SSE data lines)
     */
    std::vector<std::string> pollEvents(uint64_t conn_id, size_t max_events = 100);

    /**
     * @brief Check if heartbeat is needed for connection
     * @param conn_id Connection ID
     * @return true if heartbeat should be sent
     */
    bool needsHeartbeat(uint64_t conn_id) const;

    /**
     * @brief Mark heartbeat sent for connection
     * @param conn_id Connection ID
     */
    void recordHeartbeat(uint64_t conn_id);

    /**
     * @brief Get current stats
     */
    ConnectionStats getStats() const;

    /**
     * @brief Shutdown all connections gracefully
     */
    void shutdown();

private:
    struct Connection {
        uint64_t id;
        uint64_t current_sequence;
        std::string key_prefix;
        std::chrono::steady_clock::time_point last_activity;
        std::chrono::steady_clock::time_point last_heartbeat;
        std::vector<std::string> buffered_events;
        std::atomic<bool> active{true};
        // Backpressure accounting
        uint64_t dropped_events{0};
        // Simple rate window (optional)
        uint32_t sent_in_window{0};
        std::chrono::steady_clock::time_point window_start{std::chrono::steady_clock::now()};
    };

    void backgroundPollTask();

    std::shared_ptr<Changefeed> changefeed_;
    boost::asio::io_context& ioc_;
    ConnectionConfig config_;

    mutable std::mutex connections_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<Connection>> connections_;
    std::atomic<uint64_t> next_conn_id_{1};

    // Background polling
    std::unique_ptr<boost::asio::steady_timer> poll_timer_;
    std::atomic<bool> running_{false};

    // Stats
    std::atomic<uint64_t> total_events_sent_{0};
    std::atomic<uint64_t> total_heartbeats_sent_{0};
    std::atomic<uint64_t> total_disconnects_{0};
    std::atomic<uint64_t> total_dropped_events_{0};
};

} // namespace server
} // namespace themis
