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

#include <iostream>
#include <fstream>
#include <csignal>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

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
                          << "  --config FILE   Load server/storage config from JSON file\n"
                          << "  --help, -h      Show this help message\n";
                return 0;
            }
        }
        
        // Load config.json if provided or in default locations
        auto load_config = [&](const std::string& path) -> std::optional<json> {
            try {
                std::ifstream f(path);
                if (!f.is_open()) return std::nullopt;
                json j; f >> j; return j;
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
            // default search paths
            for (const auto& p : {std::string("./config.json"), std::string("./config/config.json"), std::string("/etc/vccdb/config.json")}) {
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
        
        // Step 1: Shutdown tracing (no more traces)
        THEMIS_INFO("[1/4] Shutting down distributed tracing...");
        Tracer::shutdown();
        
        // Step 2: Save vector index before closing DB
        if (vector_index && !vector_save_path.empty()) {
            THEMIS_INFO("[2/4] Saving vector index to disk...");
            vector_index->shutdown();
        } else {
            THEMIS_INFO("[2/4] Vector index save skipped (not configured)");
        }
        
        // Step 3: Database is already closed by server->stop()
        // but we ensure cleanup here
        THEMIS_INFO("[3/4] Database cleanup...");
        if (db && db->isOpen()) {
            db->close();
            THEMIS_INFO("Database closed cleanly");
        } else {
            THEMIS_INFO("Database already closed by server");
        }
        
        // Step 4: Clear shared pointers
        THEMIS_INFO("[4/4] Releasing resources...");
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
