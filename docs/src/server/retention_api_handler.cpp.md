# retention_api_handler.cpp

Path: `src/server/retention_api_handler.cpp`

Purpose: Handlers for data retention APIs (delete/expire data according to policies).

Public functions / symbols:
- `if (!retention_manager_) {`
- ``
- `if (policy.classification_level != filter.classification_filter) {`
- `if (start < total) {`
- `for (int i = start; i < end; ++i) {`
- `catch (const std::exception& e) {`
- `for (const auto& action : actions) {`
- `localtime_s(&tm, &timestamp_t);`
- `localtime_r(&timestamp_t, &tm);`

