# main_server.cpp.md

Path: TODO

Purpose: TODO

Public functions / symbols:

- ``
- `if (signal == SIGINT || signal == SIGTERM) {`
- `if (g_server) {`
- `if (arg == "--db" && i + 1 < argc) {`
- `if (config_path) {`
- `if (!cfg) {`
- `if (cfg) { THEMIS_INFO("Loaded config from {}", p); break; }`
- `if (cfg) {`
- `if (!st.ok) {`
- `if (tracing_enabled) {`
- `if (server_config.sse_max_events_per_second > 0) {`
- `if (now_tp >= next_run) {`
- `for (const auto& pk : pks) {`
- `if (entity_opt) {`
- `if (server_config.feature_semantic_cache) {`
- `if (server_config.feature_llm_store) {`
- `if (server_config.feature_cdc) {`
- `if (server_config.feature_timeseries) {`
- `THEMIS_INFO("Received shutdown signal...");`
- `THEMIS_INFO("Version: 0.1.0");`
- `for (auto it = n.begin(); it != n.end(); ++it) {`
- `return to_json(root);`
- `std::ifstream f(path);`
- `THEMIS_ERROR("Failed to read config file: {}", *config_path);`
- `THEMIS_INFO("Server: {}:{}", host, port);`
- `THEMIS_INFO("Opening RocksDB database...");`
- `THEMIS_ERROR("Failed to open database!");`
- `THEMIS_INFO("Initializing index managers...");`
- `THEMIS_INFO("Initializing vector index: object='{}', dim={}, metric={}, M={}, efC={}, efS={}",`
- `THEMIS_WARN("Vector index init failed: {}", st.message);`
- `THEMIS_INFO("Distributed tracing enabled: service='{}', endpoint='{}'", service_name, otlp_endpoint);`
- `THEMIS_WARN("Failed to initialize distributed tracing");`
- `server::HttpServer::Config server_config(host, port, num_threads);`
- `THEMIS_INFO("SSE rate limit: {} events/second per connection", server_config.sse_max_events_per_second);`
- `THEMIS_INFO("[Retention] Archive entity {}", entity_id);`
- `THEMIS_WARN("[Retention] Failed to audit-log archive for {}", entity_id);`
- `THEMIS_INFO("[Retention] Purge entity {}", entity_id);`
- `THEMIS_WARN("[Retention] Failed to audit-log purge for {}", entity_id);`
- `THEMIS_INFO("[Retention] Completed: scanned={}, archived={}, purged={}, retained={}, errors={}",`
- `THEMIS_WARN("Failed to start retention worker: unknown error");`
- `THEMIS_INFO("=================================================");`
- `THEMIS_INFO("  Themis Database Server is running!");`
- `THEMIS_INFO("  API Endpoint: http://{}:{}", host, port);`
- `THEMIS_INFO("  Press Ctrl+C to stop");`
- `THEMIS_INFO("");`
- `THEMIS_INFO("Available endpoints:");`
- `THEMIS_INFO("  GET  /health              - Health check");`
- `THEMIS_INFO("  GET  /entities/:key       - Retrieve entity");`
- `THEMIS_INFO("  POST /entities            - Create entity");`
- `THEMIS_INFO("  PUT  /entities/:key       - Update entity");`

