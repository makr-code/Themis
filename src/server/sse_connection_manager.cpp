#include "server/sse_connection_manager.h"
#include "cdc/changefeed.h"
#include "utils/logger.h"
#include <algorithm>

namespace themis {
namespace server {

SseConnectionManager::SseConnectionManager(
    std::shared_ptr<Changefeed> changefeed,
    boost::asio::io_context& ioc,
    const ConnectionConfig& config
)
    : changefeed_(std::move(changefeed))
    , ioc_(ioc)
    , config_(config)
    , poll_timer_(std::make_unique<boost::asio::steady_timer>(ioc_))
{
    THEMIS_INFO("SSE Connection Manager initialized (heartbeat: {}ms, poll: {}ms)",
        config_.heartbeat_interval_ms, config_.event_poll_interval_ms);
}

SseConnectionManager::~SseConnectionManager() {
    shutdown();
}

uint64_t SseConnectionManager::registerConnection(
    uint64_t from_seq,
    const std::string& key_prefix
) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto conn = std::make_shared<Connection>();
    conn->id = next_conn_id_++;
    conn->current_sequence = from_seq;
    conn->key_prefix = key_prefix;
    conn->last_activity = std::chrono::steady_clock::now();
    conn->last_heartbeat = std::chrono::steady_clock::now();
    
    connections_[conn->id] = conn;
    
    THEMIS_INFO("SSE connection registered: id={}, from_seq={}, prefix='{}'",
        conn->id, from_seq, key_prefix);
    
    // Start background polling if first connection
    if (connections_.size() == 1 && !running_) {
        running_ = true;
        backgroundPollTask();
    }
    
    return conn->id;
}

void SseConnectionManager::unregisterConnection(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(conn_id);
    if (it != connections_.end()) {
        it->second->active = false;
        connections_.erase(it);
        total_disconnects_++;
        
        THEMIS_INFO("SSE connection unregistered: id={}", conn_id);
        
        // Stop polling if no more connections
        if (connections_.empty()) {
            running_ = false;
            if (poll_timer_) {
                poll_timer_->cancel();
            }
        }
    }
}

std::vector<std::string> SseConnectionManager::pollEvents(
    uint64_t conn_id,
    size_t max_events
) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(conn_id);
    if (it == connections_.end() || !it->second->active) {
        return {};
    }
    
    auto& conn = it->second;
    
    // Take events from buffer (up to max_events)
    size_t count = std::min(max_events, conn->buffered_events.size());
    std::vector<std::string> events(
        conn->buffered_events.begin(),
        conn->buffered_events.begin() + count
    );
    
    // Remove consumed events from buffer
    conn->buffered_events.erase(
        conn->buffered_events.begin(),
        conn->buffered_events.begin() + count
    );
    
    if (!events.empty()) {
        conn->last_activity = std::chrono::steady_clock::now();
        total_events_sent_ += events.size();
    }
    
    return events;
}

bool SseConnectionManager::needsHeartbeat(uint64_t conn_id) const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(conn_id);
    if (it == connections_.end()) {
        return false;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - it->second->last_heartbeat
    ).count();
    
    return elapsed >= config_.heartbeat_interval_ms;
}

void SseConnectionManager::recordHeartbeat(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(conn_id);
    if (it != connections_.end()) {
        it->second->last_heartbeat = std::chrono::steady_clock::now();
        total_heartbeats_sent_++;
    }
}

SseConnectionManager::ConnectionStats SseConnectionManager::getStats() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    return ConnectionStats{
        .active_connections = connections_.size(),
        .total_events_sent = total_events_sent_.load(),
        .total_heartbeats_sent = total_heartbeats_sent_.load(),
        .total_disconnects = total_disconnects_.load()
    };
}

void SseConnectionManager::shutdown() {
    THEMIS_INFO("SSE Connection Manager shutting down...");
    
    running_ = false;
    
    if (poll_timer_) {
        poll_timer_->cancel();
    }
    
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& [id, conn] : connections_) {
        conn->active = false;
    }
    connections_.clear();
    
    THEMIS_INFO("SSE Connection Manager shutdown complete");
}

void SseConnectionManager::backgroundPollTask() {
    if (!running_) {
        return;
    }
    
    try {
        // Poll changefeed for new events
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        for (auto& [id, conn] : connections_) {
            if (!conn->active) {
                continue;
            }
            
            // Skip if buffer is full
            if (conn->buffered_events.size() >= config_.max_buffered_events) {
                THEMIS_WARN("SSE connection {} buffer full, skipping poll", id);
                continue;
            }
            
            // Query new events since last sequence
            Changefeed::ListOptions options;
            options.from_sequence = conn->current_sequence;
            options.limit = 100;
            
            if (!conn->key_prefix.empty()) {
                options.key_prefix = conn->key_prefix;
            }
            
            auto events = changefeed_->listEvents(options);
            
            // Format events as SSE data lines and buffer
            for (const auto& event : events) {
                std::string sse_line = "data: " + event.toJson().dump() + "\n\n";
                conn->buffered_events.push_back(std::move(sse_line));
                conn->current_sequence = std::max(conn->current_sequence, event.sequence);
            }
        }
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("SSE background poll error: {}", e.what());
    }
    
    // Schedule next poll
    if (running_) {
        poll_timer_->expires_after(
            std::chrono::milliseconds(config_.event_poll_interval_ms)
        );
        poll_timer_->async_wait([this](const boost::system::error_code& ec) {
            if (!ec && running_) {
                backgroundPollTask();
            }
        });
    }
}

} // namespace server
} // namespace themis
