// Windows headers must come before Boost.Asio on Windows
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

#include "utils/logger.h"
#include "utils/tracing.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "server/http_server.h"
#include "utils/retention_manager.h"
#include "utils/audit_logger.h"
#include "utils/pki_client.h"
#include "security/encryption.h"
#include "security/mock_key_provider.h"

#include <iostream>
#include <fstream>
#include <csignal>
#include <memory>
#include <optional>
#include <thread>
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

using namespace themis;
using json = nlohmann::json;

// Global server instance for signal handling
std::shared_ptr<server::HttpServer> g_server;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        THEMIS_INFO("Received shutdown signal...");
        if (g_server) {
            g_server->stop();
        }
    }
}

int main(int argc, char* argv[]) {
    // Initialize logger
    utils::Logger::init("themis_server.log", utils::Logger::Level::INFO);
    
    THEMIS_INFO("=== Themis Multi-Model Database API Server ===");
    THEMIS_INFO("Version: 0.1.0");
    
    try {
        // Parse command line arguments
        std::string db_path = "./data/themis_server";
        std::string host = "0.0.0.0";
        uint16_t port = 8765;
        size_t num_threads = 0; // Auto-detect
        std::optional<std::string> config_path;
        
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--db" && i + 1 < argc) {
                db_path = argv[++i];
            } else if (arg == "--host" && i + 1 < argc) {
                host = argv[++i];
            } else if (arg == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            } else if (arg == "--threads" && i + 1 < argc) {
                num_threads = std::stoul(argv[++i]);
            } else if (arg == "--config" && i + 1 < argc) {
                config_path = argv[++i];
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: " << argv[0] << " [options]\n"
                          << "Options:\n"
                          << "  --db PATH       Database path (default: ./data/themis_server)\n"
                          << "  --host HOST     Server host (default: 0.0.0.0)\n"
                          << "  --port PORT     Server port (default: 8765)\n"
                          << "  --threads N     Number of worker threads (default: auto)\n"
                          << "  --config FILE   Load server/storage config from JSON or YAML file\n"
                          << "  --help, -h      Show this help message\n";
                return 0;
            }
        }
        
        // Load config (JSON or YAML) if provided or in default locations
        auto load_config = [&](const std::string& path) -> std::optional<json> {
            try {
                auto ends_with = [](const std::string& s, const std::string& suffix){
                    return s.size() >= suffix.size() && s.compare(s.size()-suffix.size(), suffix.size(), suffix) == 0;
                };

                if (ends_with(path, ".yaml") || ends_with(path, ".yml")) {
                    YAML::Node root = YAML::LoadFile(path);
                    // recursive conversion YAML -> JSON
                    std::function<json(const YAML::Node&)> to_json = [&](const YAML::Node& n) -> json {
                        if (!n) return nullptr;
                        if (n.IsScalar()) {
                            // Try boolean
                            try { return n.as<bool>(); } catch (...) {}
                            // Try integer
                            try { return n.as<long long>(); } catch (...) {}
                            // Try double
                            try { return n.as<double>(); } catch (...) {}
                            // Fallback string
                            return n.as<std::string>("");
                        } else if (n.IsSequence()) {
                            json arr = json::array();
                            for (const auto& it : n) arr.push_back(to_json(it));
                            return arr;
                        } else if (n.IsMap()) {
                            json obj = json::object();
                            for (auto it = n.begin(); it != n.end(); ++it) {
                                const auto& k = it->first.as<std::string>();
                                obj[k] = to_json(it->second);
                            }
                            return obj;
                        }
                        return nullptr;
                    };
                    return to_json(root);
                } else {
                    std::ifstream f(path);
                    if (!f.is_open()) return std::nullopt;
                    json j; f >> j; return j;
                }
            } catch (...) { return std::nullopt; }
        };

        std::optional<json> cfg;
        if (config_path) {
            cfg = load_config(*config_path);
            if (!cfg) {
                THEMIS_ERROR("Failed to read config file: {}", *config_path);
                return 1;
            }
        } else {
            // default search paths (prefer YAML)
            for (const auto& p : {
                    std::string("./config.yaml"), std::string("./config.yml"), std::string("./config.json"),
                    std::string("./config/config.yaml"), std::string("./config/config.yml"), std::string("./config/config.json"),
                    std::string("/etc/vccdb/config.yaml"), std::string("/etc/vccdb/config.yml"), std::string("/etc/vccdb/config.json")}) {
                cfg = load_config(p);
                if (cfg) { THEMIS_INFO("Loaded config from {}", p); break; }
            }
        }

        THEMIS_INFO("Database path: {}", db_path);
        THEMIS_INFO("Server: {}:{}", host, port);
        
        // Configure RocksDB
        RocksDBWrapper::Config db_config;
        db_config.db_path = db_path;
        db_config.memtable_size_mb = 128;
        db_config.block_cache_size_mb = 512;
        db_config.enable_wal = true;
        db_config.enable_blobdb = false;

        // Apply JSON config if present
        if (cfg) {
            // storage
            if (cfg->contains("storage")) {
                const auto& s = (*cfg)["storage"];
                if (s.contains("rocksdb_path")) db_config.db_path = s["rocksdb_path"].get<std::string>();
                if (s.contains("memtable_size_mb")) db_config.memtable_size_mb = s["memtable_size_mb"].get<size_t>();
                if (s.contains("block_cache_size_mb")) db_config.block_cache_size_mb = s["block_cache_size_mb"].get<size_t>();
                if (s.contains("enable_blobdb")) db_config.enable_blobdb = s["enable_blobdb"].get<bool>();
                if (s.contains("compression")) {
                    const auto& c = s["compression"];
                    if (c.contains("default")) db_config.compression_default = c["default"].get<std::string>();
                    if (c.contains("bottommost")) db_config.compression_bottommost = c["bottommost"].get<std::string>();
                }
            }
            // server
            if (cfg->contains("server")) {
                const auto& sv = (*cfg)["server"];
                if (sv.contains("host")) host = sv["host"].get<std::string>();
                if (sv.contains("port")) port = static_cast<uint16_t>(sv["port"].get<int>());
                if (sv.contains("worker_threads")) num_threads = sv["worker_threads"].get<size_t>();
            }
            // features (beta)
            if (cfg->contains("features")) {
                const auto& f = (*cfg)["features"];
                // values read later into server_config
                (void)f; // placeholder to avoid unused warnings
            }
        }
        
        // Create database wrapper
        THEMIS_INFO("Opening RocksDB database...");
        auto db = std::make_shared<RocksDBWrapper>(db_config);
        
        if (!db->open()) {
            THEMIS_ERROR("Failed to open database!");
            return 1;
        }
        
        THEMIS_INFO("Database opened successfully");
        
        // Create index managers
        THEMIS_INFO("Initializing index managers...");
        auto secondary_index = std::make_shared<SecondaryIndexManager>(*db);
        auto graph_index = std::make_shared<GraphIndexManager>(*db);
        auto vector_index = std::make_shared<VectorIndexManager>(*db);

        // Parse vector_index config and initialize (optional auto-load)
        std::string vector_save_path;
        if (cfg && cfg->contains("vector_index")) {
            const auto& vi = (*cfg)["vector_index"];
            std::string object_name = vi.value("object_name", std::string());
            int dimension = vi.value("dimension", 0);
            std::string metric_str = vi.value("metric", std::string("COSINE"));
            int hnsw_m = vi.value("hnsw_m", 16);
            int hnsw_ef_construction = vi.value("hnsw_ef_construction", 200);
            int ef_search = vi.value("ef_search", 64);
            bool load_on_startup = vi.value("load_on_startup", true);
            bool save_on_shutdown = vi.value("save_on_shutdown", true);
            if (vi.contains("save_path")) {
                vector_save_path = vi["save_path"].get<std::string>();
                vector_index->setAutoSavePath(vector_save_path, save_on_shutdown);
                THEMIS_INFO("Vector index auto-save path: {} (save_on_shutdown={})", vector_save_path, save_on_shutdown);
            }

            // Initialize index if object_name & dimension provided
            if (!object_name.empty() && dimension > 0) {
                VectorIndexManager::Metric metric = (metric_str == "L2")
                    ? VectorIndexManager::Metric::L2
                    : VectorIndexManager::Metric::COSINE;
                THEMIS_INFO("Initializing vector index: object='{}', dim={}, metric={}, M={}, efC={}, efS={}",
                    object_name, dimension, metric_str, hnsw_m, hnsw_ef_construction, ef_search);
                auto st = vector_index->init(object_name, dimension, metric, hnsw_m, hnsw_ef_construction, ef_search,
                    load_on_startup ? vector_save_path : std::string());
                if (!st.ok) {
                    THEMIS_WARN("Vector index init failed: {}", st.message);
                }
            } else {
                THEMIS_INFO("Vector index not initialized (object_name/dimension missing). You can init via API or config.");
            }
        }
        
        // Create transaction manager
        auto tx_manager = std::make_shared<TransactionManager>(
            *db, *secondary_index, *graph_index, *vector_index
        );
        
        THEMIS_INFO("All managers initialized");
        
        // Initialize tracing if enabled
        if (cfg && cfg->contains("tracing")) {
            const auto& tracing_cfg = (*cfg)["tracing"];
            bool tracing_enabled = tracing_cfg.value("enabled", false);
            if (tracing_enabled) {
                std::string service_name = tracing_cfg.value("service_name", std::string("themis-server"));
                std::string otlp_endpoint = tracing_cfg.value("otlp_endpoint", std::string("http://localhost:4318"));
                
                if (Tracer::initialize(service_name, otlp_endpoint)) {
                    THEMIS_INFO("Distributed tracing enabled: service='{}', endpoint='{}'", service_name, otlp_endpoint);
                } else {
                    THEMIS_WARN("Failed to initialize distributed tracing");
                }
            }
        }
        
    // Create and start HTTP server
        server::HttpServer::Config server_config(host, port, num_threads);
        // Apply feature flags
        if (cfg && cfg->contains("features")) {
            const auto& f = (*cfg)["features"];
            server_config.feature_semantic_cache = f.value("semantic_cache", false);
            server_config.feature_llm_store = f.value("llm_store", false);
            server_config.feature_cdc = f.value("cdc", false);
            server_config.feature_timeseries = f.value("timeseries", false);
        }
        // SSE/CDC streaming config
        if (cfg && cfg->contains("sse")) {
            const auto& sse = (*cfg)["sse"];
            server_config.sse_max_events_per_second = sse.value("max_events_per_second", uint32_t(0));
            if (server_config.sse_max_events_per_second > 0) {
                THEMIS_INFO("SSE rate limit: {} events/second per connection", server_config.sse_max_events_per_second);
            }
        }
        g_server = std::make_shared<server::HttpServer>(
            server_config,
            db,
            secondary_index,
            graph_index,
            vector_index,
            tx_manager
        );
        
        // Setup signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        
        // Retention worker (optional, runs in background if enabled in config)
        std::atomic<bool> retention_stop{false};
        std::thread retention_thread;
        bool retention_enabled = false;
        int retention_interval_hours = 24;
        std::string retention_policies_path = "./config/retention_policies.yaml";
        
        if (cfg && cfg->contains("features") && (*cfg)["features"].contains("retention")) {
            const auto& ret_cfg = (*cfg)["features"]["retention"];
            retention_enabled = ret_cfg.value("enabled", false);
            retention_interval_hours = ret_cfg.value("interval_hours", 24);
            retention_policies_path = ret_cfg.value("policies_path", std::string("./config/retention_policies.yaml"));
        }
        
        if (retention_enabled) {
            try {
                // Instantiate retention manager with configured YAML path
                auto retention_mgr = std::make_shared<vcc::RetentionManager>(retention_policies_path);
                
                // Setup audit logging for retention actions
                auto key_provider = std::make_shared<themis::MockKeyProvider>();
                key_provider->createKey("retention_audit_key", 32);
                auto field_enc = std::make_shared<themis::FieldEncryption>(key_provider);
                
                themis::utils::PKIConfig pki_cfg;
                pki_cfg.service_id = "themis-retention";
                pki_cfg.endpoint = "https://pki.example.com";
                pki_cfg.signature_algorithm = "RSA-SHA256";
                auto pki_client = std::make_shared<themis::utils::VCCPKIClient>(pki_cfg);
                
                themis::utils::AuditLoggerConfig audit_cfg;
                audit_cfg.enabled = true;
                audit_cfg.encrypt_then_sign = true;
                audit_cfg.log_path = "data/logs/retention_audit.jsonl";
                audit_cfg.key_id = "retention_audit_key";
                auto audit_logger = std::make_shared<themis::utils::AuditLogger>(field_enc, pki_client, audit_cfg);
                
                // Capture db and secondary_index for entity enumeration
                auto db_ptr = db;
                auto sec_idx_ptr = secondary_index;
                
                retention_thread = std::thread([retention_mgr, &retention_stop, retention_interval_hours, db_ptr, sec_idx_ptr, audit_logger]() {
                    using namespace std::chrono;
                    auto interval = hours(retention_interval_hours);
                    auto next_run = system_clock::now();
                    
                    while (!retention_stop.load(std::memory_order_relaxed)) {
                        auto now_tp = system_clock::now();
                        if (now_tp >= next_run) {
                            // Entity provider: enumerate entities per policy from DB
                            auto entity_provider = [db_ptr, sec_idx_ptr](const std::string& policy_name) 
                                -> std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> {
                                std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> entities;
                                
                                // Map policy names to collections (example heuristic)
                                // In production: use policy metadata or a mapping table
                                std::string collection;
                                if (policy_name.find("user") != std::string::npos || policy_name.find("personal") != std::string::npos) {
                                    collection = "users";
                                } else if (policy_name.find("transaction") != std::string::npos) {
                                    collection = "transactions";
                                } else if (policy_name.find("audit") != std::string::npos) {
                                    collection = "audit_logs";
                                } else if (policy_name.find("session") != std::string::npos) {
                                    collection = "sessions";
                                } else if (policy_name.find("analytics") != std::string::npos) {
                                    collection = "analytics";
                                } else if (policy_name.find("backup") != std::string::npos) {
                                    collection = "backups";
                                } else {
                                    // Skip unknown policies
                                    return entities;
                                }
                                
                                // Scan collection for entities with created_at timestamps
                                // Use range scan if created_at index exists, otherwise skip
                                try {
                                    if (sec_idx_ptr->hasRangeIndex(collection, "created_at")) {
                                        // Scan all entries in created_at index (unbounded range)
                                        auto [status, pks] = sec_idx_ptr->scanKeysRange(
                                            collection, "created_at",
                                            std::nullopt, std::nullopt,
                                            false, false,
                                            10000, false
                                        );
                                        
                                        if (status.ok) {
                                            for (const auto& pk : pks) {
                                                // Fetch entity to get created_at
                                                auto entity_opt = db_ptr->get(pk);
                                                if (entity_opt) {
                                                    try {
                                                        std::string entity_str(entity_opt->begin(), entity_opt->end());
                                                        auto j = nlohmann::json::parse(entity_str);
                                                        if (j.contains("created_at")) {
                                                            // Parse ISO8601 or epoch timestamp
                                                            int64_t ts_epoch = 0;
                                                            if (j["created_at"].is_number()) {
                                                                ts_epoch = j["created_at"].get<int64_t>();
                                                            } else if (j["created_at"].is_string()) {
                                                                // Simple epoch string parse
                                                                ts_epoch = std::stoll(j["created_at"].get<std::string>());
                                                            }
                                                            auto tp = std::chrono::system_clock::time_point(std::chrono::seconds(ts_epoch));
                                                            entities.emplace_back(pk, tp);
                                                        }
                                                    } catch (...) {
                                                        // Skip malformed entities
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } catch (...) {
                                    // Index may not exist; silent fallback
                                }
                                
                                return entities;
                            };
                            
                            auto archive_handler = [db_ptr, audit_logger](const std::string& entity_id) -> bool {
                                THEMIS_INFO("[Retention] Archive entity {}", entity_id);
                                
                                // Audit log the archival action
                                try {
                                    nlohmann::json audit_event;
                                    audit_event["action"] = "RETENTION_ARCHIVE";
                                    audit_event["entity_id"] = entity_id;
                                    audit_event["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch()
                                    ).count();
                                    audit_event["classification"] = "retention_lifecycle";
                                    audit_logger->logEvent(audit_event);
                                } catch (...) {
                                    THEMIS_WARN("[Retention] Failed to audit-log archive for {}", entity_id);
                                }
                                
                                // TODO: Move to cold storage (e.g., export to S3, tape, or separate DB)
                                // For now, mark as archived or export to file
                                return true;
                            };
                            
                            auto purge_handler = [db_ptr, audit_logger](const std::string& entity_id) -> bool {
                                THEMIS_INFO("[Retention] Purge entity {}", entity_id);
                                
                                // Audit log the purge action
                                try {
                                    nlohmann::json audit_event;
                                    audit_event["action"] = "RETENTION_PURGE";
                                    audit_event["entity_id"] = entity_id;
                                    audit_event["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch()
                                    ).count();
                                    audit_event["classification"] = "retention_lifecycle";
                                    audit_logger->logEvent(audit_event);
                                } catch (...) {
                                    THEMIS_WARN("[Retention] Failed to audit-log purge for {}", entity_id);
                                }
                                
                                // Delete from DB
                                return db_ptr->del(entity_id);
                            };
                            
                            auto stats = retention_mgr->runRetentionCheck(entity_provider, archive_handler, purge_handler);
                            THEMIS_INFO("[Retention] Completed: scanned={}, archived={}, purged={}, retained={}, errors={}",
                                stats.total_entities_scanned, stats.archived_count, stats.purged_count, stats.retained_count, stats.error_count);
                            next_run = now_tp + interval;
                        }
                        
                        // Sleep in small chunks to react quickly on shutdown
                        int sleep_minutes = std::max(1, retention_interval_hours * 60 / 60); // at least 1min chunks
                        for (int i = 0; i < sleep_minutes && !retention_stop.load(std::memory_order_relaxed); ++i) {
                            std::this_thread::sleep_for(std::chrono::minutes(1));
                        }
                    }
                });
                THEMIS_INFO("Retention worker started (interval: {}h)", retention_interval_hours);
            } catch (const std::exception& e) {
                THEMIS_WARN("Failed to start retention worker: {}", e.what());
            } catch (...) {
                THEMIS_WARN("Failed to start retention worker: unknown error");
            }
        } else {
            THEMIS_INFO("Retention worker disabled (enable via config.json features.retention.enabled)");
        }

        THEMIS_INFO("Starting HTTP server...");
        g_server->start();
        
    THEMIS_INFO("");
        THEMIS_INFO("=================================================");
        THEMIS_INFO("  Themis Database Server is running!");
        THEMIS_INFO("  API Endpoint: http://{}:{}", host, port);
        THEMIS_INFO("  Press Ctrl+C to stop");
        THEMIS_INFO("=================================================");
        THEMIS_INFO("");
        THEMIS_INFO("Available endpoints:");
        THEMIS_INFO("  GET  /health              - Health check");
        THEMIS_INFO("  GET  /entities/:key       - Retrieve entity");
        THEMIS_INFO("  POST /entities            - Create entity");
        THEMIS_INFO("  PUT  /entities/:key       - Update entity");
        THEMIS_INFO("  DELETE /entities/:key     - Delete entity");
        THEMIS_INFO("  POST /query               - Execute query");
        THEMIS_INFO("  POST /graph/traverse      - Graph traversal");
        THEMIS_INFO("  POST /vector/search       - Vector search");
        THEMIS_INFO("  POST /transaction         - Execute transaction");
        if (server_config.feature_semantic_cache) {
            THEMIS_INFO("  POST /cache/query         - Semantic cache lookup (beta)");
            THEMIS_INFO("  POST /cache/put           - Semantic cache put (beta)");
            THEMIS_INFO("  GET  /cache/stats         - Semantic cache stats (beta)");
        }
        if (server_config.feature_llm_store) {
            THEMIS_INFO("  POST /llm/interaction     - Create LLM interaction (beta)");
            THEMIS_INFO("  GET  /llm/interaction     - List LLM interactions (beta)");
            THEMIS_INFO("  GET  /llm/interaction/:id - Get LLM interaction (beta)");
        }
        if (server_config.feature_cdc) {
            THEMIS_INFO("  GET  /changefeed          - CDC feed (beta)");
        }
        if (server_config.feature_timeseries) {
            THEMIS_INFO("  POST /ts/put              - Store time-series data (beta)");
            THEMIS_INFO("  POST /ts/query            - Query time-series data (beta)");
            THEMIS_INFO("  POST /ts/aggregate        - Aggregate time-series (beta)");
        }
        THEMIS_INFO("");
        
        // Wait for server to finish
        g_server->wait();
        
        THEMIS_INFO("=================================================");
        THEMIS_INFO("Initiating graceful shutdown sequence...");
        THEMIS_INFO("=================================================");
        
        // Step 1: Stop retention worker
        THEMIS_INFO("[1/5] Stopping retention worker...");
        try {
            retention_stop.store(true, std::memory_order_relaxed);
            if (retention_thread.joinable()) retention_thread.join();
            THEMIS_INFO("Retention worker stopped");
        } catch (...) {
            THEMIS_WARN("Error stopping retention worker (continuing shutdown)");
        }

    // Step 2: Shutdown tracing (no more traces)
    THEMIS_INFO("[2/5] Shutting down distributed tracing...");
        Tracer::shutdown();
        
        // Step 3: Save vector index before closing DB
        if (vector_index && !vector_save_path.empty()) {
            THEMIS_INFO("[3/5] Saving vector index to disk...");
            vector_index->shutdown();
        } else {
            THEMIS_INFO("[3/5] Vector index save skipped (not configured)");
        }
        
        // Step 4: Database is already closed by server->stop()
        // but we ensure cleanup here
        THEMIS_INFO("[4/5] Database cleanup...");
        if (db && db->isOpen()) {
            db->close();
            THEMIS_INFO("Database closed cleanly");
        } else {
            THEMIS_INFO("Database already closed by server");
        }
        
        // Step 5: Clear shared pointers
        THEMIS_INFO("[5/5] Releasing resources...");
        g_server.reset();
        tx_manager.reset();
        vector_index.reset();
        graph_index.reset();
        secondary_index.reset();
        db.reset();
        
        THEMIS_INFO("=================================================");
        THEMIS_INFO("Shutdown complete. All data saved.");
        THEMIS_INFO("=================================================");
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Fatal error: {}", e.what());
        return 1;
    }
    
    utils::Logger::shutdown();
    return 0;
}
