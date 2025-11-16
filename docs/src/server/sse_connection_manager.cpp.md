# sse_connection_manager.cpp

Path: `src/server/sse_connection_manager.cpp`

Purpose: Manages Server‑Sent Events connections for streaming endpoints (changefeed SSE, etc.).

Public functions / symbols:
- `if (poll_timer_) {`
- `if (config_.max_events_per_second > 0) {`
- `if (elapsed_ms >= 1000) {`
- `if (conn->sent_in_window < config_.max_events_per_second) {`
- `if (budget == 0) {`
- `if (count == 0) {`
- ``
- `for (auto& [id, conn] : connections_) {`
- `if (!running_) {`
- `if (!conn->active) {`
- `for (const auto& event : events) {`
- `if (config_.drop_oldest_on_overflow) {`
- `if (running_) {`
- `if (!ec && running_) {`
- `shutdown();`
- `std::lock_guard<std::mutex> lock(connections_mutex_);`
- `backgroundPollTask();`
- `THEMIS_INFO("SSE Connection Manager shutting down...");`
- `THEMIS_WARN("SSE connection {} buffer full, skipping poll", id);`

