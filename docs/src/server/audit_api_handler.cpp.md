# audit_api_handler.cpp

Path: `src/server/audit_api_handler.cpp`

Purpose: HTTP handlers for audit APIs; manage audit logs and retrieval.

Public functions / symbols:
- `static int64_t parseIso8601ToMs(const std::string& iso_str) {`
- `static bool containsCaseInsensitive(const std::string& haystack, const std::string& needle) {`
- `if (entry.timestamp_ms < filter.start_ts_ms || entry.timestamp_ms > filter.end_ts_ms) {`
- ``
- `for (int i = start_idx; i < end_idx; i++) {`
- `for (const auto& entry : entries) {`
- `for (char c : s) {`
- `std::istringstream ss(iso_str);`

