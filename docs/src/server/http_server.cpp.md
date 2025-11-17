# http_server.cpp (server)

Path: `src/server/http_server.cpp`

Purpose: Server HTTP entrypoints and routing for the application; differs from `src/api/http_server.cpp` in that it organizes API handlers and middleware.

Public functions / symbols:
- `if (config_.feature_semantic_cache) {`
- `if (config_.feature_llm_store) {`
- `if (config_.feature_cdc) {`
- `if (config_.feature_timeseries) {`
- `for (const auto& policies_path : candidates) {`
- `if (!loaded_any) {`
- `if (running_) {`
- `for (size_t i = 0; i < config_.num_threads; ++i) {`
- `if (!running_) {`
- `if (semantic_cache_) {`
- ``
- `if (vector_index_) {`
- `if (storage_) {`
- `for (auto& thread : threads_) {`
- `if (ec) {`
- `if (!keys_api_) {`
- `if (qpos != std::string::npos) {`
- `if (eq != std::string::npos) {`
- `if (k == "key_id") { key_id = v; break; }`
- `if (!classification_api_) {`
- `if (!reports_api_) {`
- `if (fmt == "json") {`
- `if (timeout >= 1000 && timeout <= 300000) { // 1s - 5min range`
- `if (!config_.feature_cdc || !changefeed_) {`
- `if (hours < 1 || hours > 8760) { // 1 hour - 1 year`
- `if (policy_engine_) {`
- `if (sse_manager_) {`
- `if (!token) {`
- `if (!ar.authorized) {`
- `if (auth_enabled) {`
- `if (policy_enabled) {`
- `for (const auto& h : req) {`
- `if (start != std::string::npos) {`
- `if (!decision.allowed) {`
- `if (!value_opt) {`
- `if (pos != std::string::npos) {`
- `if (mode == "hard") {`
- `if (!config_.feature_semantic_cache) {`
- `if (!config_.feature_llm_store) {`
- `if (query_pos != std::string::npos) {`
- `if (limit_pos != std::string::npos) {`
- `if (start_pos != std::string::npos) {`
- `if (model_pos != std::string::npos) {`
- `for (const auto& interaction : interactions) {`
- `if (!config_.feature_cdc) {`
- `if (from_pos != std::string::npos) {`
- `if (poll_pos != std::string::npos) {`
- `if (key_pos != std::string::npos) {`
- `for (const auto& event : events) {`
- `if (ka_pos != std::string::npos) {`

