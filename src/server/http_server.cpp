// Windows macros undefine - MUST be before any includes
#ifdef ERROR
#undef ERROR
#endif

// Include full definitions BEFORE http_server.h to avoid incomplete types
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "utils/logger.h"
#include "utils/logger_impl.h"
#include "utils/cursor.h"
#include "content/content_manager.h"
#include "content/content_processor.h"
// Für Graph-Kanten-Entities
#include "storage/key_schema.h"

// Now include http_server.h which has forward declarations
#include "server/http_server.h"

#include "query/query_engine.h"
#include "query/query_optimizer.h"
#include "query/aql_parser.h"
#include "query/aql_translator.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <tuple>
#include <regex>
#include <queue>
#include <unordered_set>
#include <functional>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <limits>

using json = nlohmann::json;

namespace themis {
namespace server {

// ============================================================================
// HttpServer Implementation
// ============================================================================

HttpServer::HttpServer(
    const Config& config,
    std::shared_ptr<RocksDBWrapper> storage,
    std::shared_ptr<SecondaryIndexManager> secondary_index,
    std::shared_ptr<GraphIndexManager> graph_index,
    std::shared_ptr<VectorIndexManager> vector_index,
    std::shared_ptr<TransactionManager> tx_manager
)
    : config_(config)
    , storage_(std::move(storage))
    , secondary_index_(std::move(secondary_index))
    , graph_index_(std::move(graph_index))
    , vector_index_(std::move(vector_index))
    , tx_manager_(std::move(tx_manager))
    , ioc_(static_cast<int>(config_.num_threads))
    , acceptor_(ioc_)
    , start_time_(std::chrono::steady_clock::now())
{
    THEMIS_INFO("HTTP Server created with {} threads on {}:{}", 
        config_.num_threads, config_.host, config_.port);
    // Initialize ContentManager and register built-in processors
    content_manager_ = std::make_unique<themis::content::ContentManager>(
        storage_, vector_index_, graph_index_, secondary_index_);
    text_processor_ = std::make_unique<themis::content::TextProcessor>();
    content_manager_->registerProcessor(std::unique_ptr<themis::content::IContentProcessor>(text_processor_.release()));
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_) {
        THEMIS_WARN("Server already running");
        return;
    }

    // Setup acceptor
    tcp::endpoint endpoint{net::ip::make_address(config_.host), config_.port};
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(net::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(net::socket_base::max_listen_connections);

    THEMIS_INFO("HTTP Server listening on {}:{}", config_.host, config_.port);

    running_ = true;
    
    // Start accepting connections
    doAccept();

    // Start thread pool
    threads_.reserve(config_.num_threads);
    for (size_t i = 0; i < config_.num_threads; ++i) {
        threads_.emplace_back([this, i] {
            THEMIS_DEBUG("Worker thread {} started", i);
            ioc_.run();
            THEMIS_DEBUG("Worker thread {} stopped", i);
        });
    }

    THEMIS_INFO("HTTP Server started successfully");
}

void HttpServer::stop() {
    if (!running_) {
        return;
    }

    THEMIS_INFO("Stopping HTTP Server...");
    running_ = false;

    // Stop accepting new connections
    beast::error_code ec;
    acceptor_.close(ec);

    // Stop io_context
    ioc_.stop();

    // Wait for all threads
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();

    THEMIS_INFO("HTTP Server stopped");
}

void HttpServer::wait() {
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void HttpServer::doAccept() {
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&HttpServer::onAccept, this)
    );
}

void HttpServer::onAccept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        THEMIS_ERROR("Accept error: {}", ec.message());
    } else {
        // Create new session for this connection
        std::make_shared<Session>(std::move(socket), this)->start();
    }

    // Accept next connection
    if (running_) {
        doAccept();
    }
}

namespace {
    enum class Route {
        Health,
        Stats,
        Metrics,
        Config,
        AdminBackupPost,
        AdminRestorePost,
        EntitiesGet,
        EntitiesPut,
        EntitiesDelete,
        EntitiesPost,
        QueryPost,
        QueryAqlPost,
        IndexCreatePost,
        IndexDropPost,
        IndexStatsGet,
        IndexRebuildPost,
        IndexReindexPost,
        GraphTraversePost,
        VectorSearchPost,
        VectorIndexSavePost,
        VectorIndexLoadPost,
        VectorIndexConfigGet,
        VectorIndexConfigPut,
        VectorIndexStatsGet,
        TransactionPost,
        TransactionBeginPost,
        TransactionCommitPost,
        TransactionRollbackPost,
        TransactionStatsGet,
        ContentImportPost,
        ContentGet,
        ContentBlobGet,
        ContentChunksGet,
        HybridSearchPost,
        ContentFilterSchemaGet,
        ContentFilterSchemaPut,
        EdgeWeightConfigGet,
        EdgeWeightConfigPut,
        NotFound
    };

    Route classifyRoute(const http::request<http::string_body>& req) {
        const auto method = req.method();
        const std::string target = std::string(req.target());

        if (target == "/" || target == "/health") return Route::Health;
        if (target == "/stats" && method == http::verb::get) return Route::Stats;
        if (target == "/metrics" && method == http::verb::get) return Route::Metrics;
    if (target == "/config" && method == http::verb::get) return Route::Config;
    if (target == "/admin/backup" && method == http::verb::post) return Route::AdminBackupPost;
    if (target == "/admin/restore" && method == http::verb::post) return Route::AdminRestorePost;

        // Parametrized entity by key
        if (target.rfind("/entities/", 0) == 0) {
            if (method == http::verb::get) return Route::EntitiesGet;
            if (method == http::verb::put) return Route::EntitiesPut;
            if (method == http::verb::delete_) return Route::EntitiesDelete;
            return Route::NotFound;
        }

        if (target == "/entities" && method == http::verb::post) return Route::EntitiesPost;
        if (target == "/query" && method == http::verb::post) return Route::QueryPost;
        if (target == "/query/aql" && method == http::verb::post) return Route::QueryAqlPost;
        if (target == "/index/create" && method == http::verb::post) return Route::IndexCreatePost;
        if (target == "/index/drop" && method == http::verb::post) return Route::IndexDropPost;
        if (target == "/index/stats" && method == http::verb::get) return Route::IndexStatsGet;
        if (target == "/index/rebuild" && method == http::verb::post) return Route::IndexRebuildPost;
        if (target == "/index/reindex" && method == http::verb::post) return Route::IndexReindexPost;
        if (target == "/graph/traverse" && method == http::verb::post) return Route::GraphTraversePost;
        if (target == "/vector/search" && method == http::verb::post) return Route::VectorSearchPost;
        if (target == "/vector/index/save" && method == http::verb::post) return Route::VectorIndexSavePost;
        if (target == "/vector/index/load" && method == http::verb::post) return Route::VectorIndexLoadPost;
        if (target == "/vector/index/config" && method == http::verb::get) return Route::VectorIndexConfigGet;
        if (target == "/vector/index/config" && method == http::verb::put) return Route::VectorIndexConfigPut;
    if (target == "/vector/index/stats" && method == http::verb::get) return Route::VectorIndexStatsGet;
        if (target == "/transaction" && method == http::verb::post) return Route::TransactionPost;
        if (target == "/transaction/begin" && method == http::verb::post) return Route::TransactionBeginPost;
        if (target == "/transaction/commit" && method == http::verb::post) return Route::TransactionCommitPost;
        if (target == "/transaction/rollback" && method == http::verb::post) return Route::TransactionRollbackPost;
        if (target == "/transaction/stats" && method == http::verb::get) return Route::TransactionStatsGet;

        // Content API
        if (target == "/content/import" && method == http::verb::post) return Route::ContentImportPost;
        if (target.rfind("/content/", 0) == 0 && method == http::verb::get) {
            if (target.find("/blob") != std::string::npos) return Route::ContentBlobGet;
            if (target.find("/chunks") != std::string::npos) return Route::ContentChunksGet;
            return Route::ContentGet;
        }

    // Hybrid Search
        if (target == "/search/hybrid" && method == http::verb::post) return Route::HybridSearchPost;

    // Content filter schema config
    if (target == "/config/content-filters" && method == http::verb::get) return Route::ContentFilterSchemaGet;
    if (target == "/config/content-filters" && (method == http::verb::put || method == http::verb::post)) return Route::ContentFilterSchemaPut;
    if (target == "/config/edge-weights" && method == http::verb::get) return Route::EdgeWeightConfigGet;
    if (target == "/config/edge-weights" && (method == http::verb::put || method == http::verb::post)) return Route::EdgeWeightConfigPut;

        return Route::NotFound;
    }
}

http::response<http::string_body> HttpServer::routeRequest(
    const http::request<http::string_body>& req
) {
    auto start = std::chrono::steady_clock::now();
    
    auto target = std::string(req.target());
    auto method = req.method();

    THEMIS_DEBUG("Request: {} {}", http::to_string(method), target);

    // Increment request counter
    request_count_.fetch_add(1, std::memory_order_relaxed);

    http::response<http::string_body> response;

    switch (classifyRoute(req)) {
        case Route::Health:
            response = handleHealthCheck(req);
            break;
        case Route::Stats:
            response = handleStats(req);
            break;
        case Route::Metrics:
            response = handleMetrics(req);
            break;
        case Route::Config:
            response = handleConfig(req);
            break;
        case Route::AdminBackupPost:
            response = handleAdminBackup(req);
            break;
        case Route::AdminRestorePost:
            response = handleAdminRestore(req);
            break;
        case Route::EntitiesGet:
            response = handleGetEntity(req);
            break;
        case Route::EntitiesPut:
            response = handlePutEntity(req);
            break;
        case Route::EntitiesDelete:
            response = handleDeleteEntity(req);
            break;
        case Route::EntitiesPost:
            response = handlePutEntity(req);
            break;
        case Route::QueryPost:
            response = handleQuery(req);
            break;
        case Route::QueryAqlPost:
            response = handleQueryAql(req);
            break;
        case Route::IndexCreatePost:
            response = handleCreateIndex(req);
            break;
        case Route::IndexDropPost:
            response = handleDropIndex(req);
            break;
        case Route::IndexStatsGet:
            response = handleIndexStats(req);
            break;
        case Route::IndexRebuildPost:
            response = handleIndexRebuild(req);
            break;
        case Route::IndexReindexPost:
            response = handleIndexReindex(req);
            break;
        case Route::GraphTraversePost:
            response = handleGraphTraverse(req);
            break;
        case Route::VectorSearchPost:
            response = handleVectorSearch(req);
            break;
        case Route::VectorIndexSavePost:
            response = handleVectorIndexSave(req);
            break;
        case Route::VectorIndexLoadPost:
            response = handleVectorIndexLoad(req);
            break;
        case Route::VectorIndexConfigGet:
            response = handleVectorIndexConfigGet(req);
            break;
        case Route::VectorIndexConfigPut:
            response = handleVectorIndexConfigPut(req);
            break;
        case Route::VectorIndexStatsGet:
            response = handleVectorIndexStats(req);
            break;
        case Route::TransactionPost:
            response = handleTransaction(req);
            break;
        case Route::TransactionBeginPost:
            response = handleTransactionBegin(req);
            break;
        case Route::TransactionCommitPost:
            response = handleTransactionCommit(req);
            break;
        case Route::TransactionRollbackPost:
            response = handleTransactionRollback(req);
            break;
        case Route::TransactionStatsGet:
            response = handleTransactionStats(req);
            break;
        case Route::ContentImportPost:
            response = handleContentImport(req);
            break;
        case Route::ContentGet:
            response = handleGetContent(req);
            break;
        case Route::ContentBlobGet:
            response = handleGetContentBlob(req);
            break;
        case Route::ContentChunksGet:
            response = handleGetContentChunks(req);
            break;
        case Route::HybridSearchPost:
            response = handleHybridSearch(req);
            break;
        case Route::ContentFilterSchemaGet:
            response = handleContentFilterSchemaGet(req);
            break;
        case Route::ContentFilterSchemaPut:
            response = handleContentFilterSchemaPut(req);
            break;
        case Route::EdgeWeightConfigGet:
            response = handleEdgeWeightConfigGet(req);
            break;
        case Route::EdgeWeightConfigPut:
            response = handleEdgeWeightConfigPut(req);
            break;
        case Route::NotFound:
        default:
            response = makeErrorResponse(http::status::not_found, "Endpoint not found", req);
            break;
    }

    // Record latency before returning
    auto end = std::chrono::steady_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    recordLatency(dur);

    return response;
}

http::response<http::string_body> HttpServer::handleHealthCheck(
    const http::request<http::string_body>& req
) {
    auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time_
    ).count();
    
    json response = {
        {"status", "healthy"},
        {"version", "0.1.0"},
        {"database", "themis"},
        {"uptime_seconds", uptime_seconds}
    };
    return makeResponse(http::status::ok, response.dump(), req);
}

http::response<http::string_body> HttpServer::handleStats(
    const http::request<http::string_body>& req
) {
    try {
        auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_
        ).count();
        
        uint64_t total_requests = request_count_.load(std::memory_order_relaxed);
        uint64_t total_errors = error_count_.load(std::memory_order_relaxed);
        double qps = uptime_seconds > 0 ? static_cast<double>(total_requests) / uptime_seconds : 0.0;
        
        // Get RocksDB stats (returns JSON string)
        std::string rocksdb_stats = storage_->getStats();
        
        // Parse RocksDB JSON
        json rocksdb_json;
        try {
            rocksdb_json = json::parse(rocksdb_stats);
        } catch (...) {
            rocksdb_json = {{"error", "Failed to parse RocksDB stats"}};
        }
        
        // Build complete stats response
        json response = {
            {"server", {
                {"uptime_seconds", uptime_seconds},
                {"total_requests", total_requests},
                {"total_errors", total_errors},
                {"queries_per_second", qps},
                {"threads", config_.num_threads}
            }},
            {"storage", rocksdb_json}
        };
        
        return makeResponse(http::status::ok, response.dump(2), req); // Pretty print with indent 2
    } catch (const std::exception& e) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return makeErrorResponse(http::status::internal_server_error, 
                                 std::string("Failed to get stats: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleConfig(
    const http::request<http::string_body>& req
) {
    try {
        // Build comprehensive config response
        json response = {
            {"server", {
                {"port", config_.port},
                {"threads", config_.num_threads},
                {"request_timeout_ms", config_.request_timeout_ms}
            }},
            {"rocksdb", {
                {"db_path", storage_->getConfig().db_path},
                {"wal_dir", storage_->getConfig().wal_dir.empty() ? storage_->getConfig().db_path : storage_->getConfig().wal_dir},
                {"memtable_size_mb", storage_->getConfig().memtable_size_mb},
                {"block_cache_size_mb", storage_->getConfig().block_cache_size_mb},
                {"cache_index_and_filter_blocks", storage_->getConfig().cache_index_and_filter_blocks},
                {"pin_l0_filter_and_index_blocks_in_cache", storage_->getConfig().pin_l0_filter_and_index_blocks_in_cache},
                {"partition_filters", storage_->getConfig().partition_filters},
                {"high_pri_pool_ratio", storage_->getConfig().high_pri_pool_ratio},
                {"bloom_bits_per_key", storage_->getConfig().bloom_bits_per_key},
                {"enable_wal", storage_->getConfig().enable_wal},
                {"enable_blobdb", storage_->getConfig().enable_blobdb},
                {"blob_size_threshold", storage_->getConfig().blob_size_threshold},
                {"max_background_jobs", storage_->getConfig().max_background_jobs},
                {"use_universal_compaction", storage_->getConfig().use_universal_compaction},
                {"dynamic_level_bytes", storage_->getConfig().dynamic_level_bytes},
                {"target_file_size_base_mb", storage_->getConfig().target_file_size_base_mb},
                {"max_bytes_for_level_base_mb", storage_->getConfig().max_bytes_for_level_base_mb},
                {"max_write_buffer_number", storage_->getConfig().max_write_buffer_number},
                {"min_write_buffer_number_to_merge", storage_->getConfig().min_write_buffer_number_to_merge},
                {"use_direct_reads", storage_->getConfig().use_direct_reads},
                {"use_direct_io_for_flush_and_compaction", storage_->getConfig().use_direct_io_for_flush_and_compaction},
                {"compression_default", storage_->getConfig().compression_default},
                {"compression_bottommost", storage_->getConfig().compression_bottommost}
            }},
            {"runtime", {
                {"compression_active", storage_->getCompressionType()},
                {"db_size_bytes", storage_->getApproximateSize()}
            }},
            {"metrics", {
                {"total_requests", request_count_.load(std::memory_order_relaxed)},
                {"total_errors", error_count_.load(std::memory_order_relaxed)}
            }}
        };

        return makeResponse(http::status::ok, response.dump(2), req); // Pretty print
    } catch (const std::exception& e) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return makeErrorResponse(http::status::internal_server_error,
                                 std::string("Failed to get config: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleMetrics(
    const http::request<http::string_body>& req
) {
    try {
        auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_
        ).count();

        uint64_t total_requests = request_count_.load(std::memory_order_relaxed);
        uint64_t total_errors = error_count_.load(std::memory_order_relaxed);
        double qps = uptime_seconds > 0 ? static_cast<double>(total_requests) / uptime_seconds : 0.0;

        // Parse RocksDB stats JSON
        json rdb;
        try {
            rdb = json::parse(storage_->getStats());
        } catch (...) {
            rdb = json::object();
        }
        json r = rdb.contains("rocksdb") ? rdb["rocksdb"] : json::object();

        auto get_u64 = [&](const char* k) -> uint64_t {
            if (r.contains(k) && r[k].is_number_unsigned()) return r[k].get<uint64_t>();
            if (r.contains(k) && r[k].is_number_integer()) return static_cast<uint64_t>(r[k].get<int64_t>());
            return 0ull;
        };
        uint64_t block_cache_usage = get_u64("block_cache_usage_bytes");
        uint64_t block_cache_capacity = get_u64("block_cache_capacity_bytes");
        uint64_t estimate_keys = get_u64("estimate_num_keys");
        uint64_t pending_compaction = get_u64("estimate_pending_compaction_bytes");
        uint64_t memtable_bytes = get_u64("memtable_size_bytes");

        std::string out;
        out.reserve(2048);
        out += "# HELP process_uptime_seconds Process uptime in seconds\n";
        out += "# TYPE process_uptime_seconds gauge\n";
        out += "process_uptime_seconds " + std::to_string(uptime_seconds) + "\n";

        out += "# HELP vccdb_requests_total Total HTTP requests handled\n";
        out += "# TYPE vccdb_requests_total counter\n";
        out += "vccdb_requests_total " + std::to_string(total_requests) + "\n";

        out += "# HELP vccdb_errors_total Total HTTP errors returned\n";
        out += "# TYPE vccdb_errors_total counter\n";
        out += "vccdb_errors_total " + std::to_string(total_errors) + "\n";

        out += "# HELP vccdb_qps Queries per second (approx)\n";
        out += "# TYPE vccdb_qps gauge\n";
        out += "vccdb_qps " + std::to_string(qps) + "\n";

        out += "# HELP rocksdb_block_cache_usage_bytes RocksDB block cache usage in bytes\n";
        out += "# TYPE rocksdb_block_cache_usage_bytes gauge\n";
        out += "rocksdb_block_cache_usage_bytes " + std::to_string(block_cache_usage) + "\n";

        out += "# HELP rocksdb_block_cache_capacity_bytes RocksDB block cache capacity in bytes\n";
        out += "# TYPE rocksdb_block_cache_capacity_bytes gauge\n";
        out += "rocksdb_block_cache_capacity_bytes " + std::to_string(block_cache_capacity) + "\n";

        out += "# HELP rocksdb_estimate_num_keys Estimated number of keys in DB\n";
        out += "# TYPE rocksdb_estimate_num_keys gauge\n";
        out += "rocksdb_estimate_num_keys " + std::to_string(estimate_keys) + "\n";

        out += "# HELP rocksdb_pending_compaction_bytes Estimated pending compaction bytes\n";
        out += "# TYPE rocksdb_pending_compaction_bytes gauge\n";
        out += "rocksdb_pending_compaction_bytes " + std::to_string(pending_compaction) + "\n";

        out += "# HELP rocksdb_memtable_size_bytes Current memtable size in bytes\n";
        out += "# TYPE rocksdb_memtable_size_bytes gauge\n";
        out += "rocksdb_memtable_size_bytes " + std::to_string(memtable_bytes) + "\n";

        if (r.contains("files_per_level") && r["files_per_level"].is_object()) {
            for (auto it = r["files_per_level"].begin(); it != r["files_per_level"].end(); ++it) {
                std::string level = it.key();
                uint64_t val = 0;
                if (it.value().is_number_integer()) val = static_cast<uint64_t>(it.value().get<int64_t>());
                else if (it.value().is_number_unsigned()) val = it.value().get<uint64_t>();
                out += "rocksdb_files_level{level=\"" + level + "\"} " + std::to_string(val) + "\n";
            }
        }

        // Export latency histogram buckets
        auto export_bucket = [&](const char* name, uint64_t val){
            out += std::string(name) + " " + std::to_string(val) + "\n";
        };
        export_bucket("vccdb_latency_bucket_microseconds{le=\"100\"}", latency_bucket_100us_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"500\"}", latency_bucket_500us_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"1000\"}", latency_bucket_1ms_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"5000\"}", latency_bucket_5ms_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"10000\"}", latency_bucket_10ms_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"50000\"}", latency_bucket_50ms_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"100000\"}", latency_bucket_100ms_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"500000\"}", latency_bucket_500ms_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"1000000\"}", latency_bucket_1s_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"5000000\"}", latency_bucket_5s_.load(std::memory_order_relaxed));
        export_bucket("vccdb_latency_bucket_microseconds{le=\"+Inf\"}", latency_bucket_inf_.load(std::memory_order_relaxed));

        // Sum + count for histogram
        uint64_t total_latency_us = latency_sum_us_.load(std::memory_order_relaxed);
        uint64_t total_count = latency_bucket_inf_.load(std::memory_order_relaxed);
        out += "# HELP vccdb_latency_sum_microseconds Total request latency in microseconds\n";
        out += "# TYPE vccdb_latency_sum_microseconds counter\n";
        out += "vccdb_latency_sum_microseconds " + std::to_string(total_latency_us) + "\n";
        out += "# HELP vccdb_latency_count Total recorded requests for latency histogram\n";
        out += "# TYPE vccdb_latency_count counter\n";
        out += "vccdb_latency_count " + std::to_string(total_count) + "\n";

    // Index rebuild metrics
        auto& rebuild_metrics = secondary_index_->getRebuildMetrics();
        uint64_t rebuild_count = rebuild_metrics.rebuild_count.load(std::memory_order_relaxed);
        uint64_t rebuild_duration_ms = rebuild_metrics.rebuild_duration_ms.load(std::memory_order_relaxed);
        uint64_t rebuild_entities = rebuild_metrics.rebuild_entities_processed.load(std::memory_order_relaxed);
        
        out += "# HELP vccdb_index_rebuilds_total Total number of index rebuilds performed\n";
        out += "# TYPE vccdb_index_rebuilds_total counter\n";
        out += "vccdb_index_rebuilds_total " + std::to_string(rebuild_count) + "\n";
        
        out += "# HELP vccdb_index_rebuild_duration_milliseconds_total Total duration of all index rebuilds in milliseconds\n";
        out += "# TYPE vccdb_index_rebuild_duration_milliseconds_total counter\n";
        out += "vccdb_index_rebuild_duration_milliseconds_total " + std::to_string(rebuild_duration_ms) + "\n";
        
        out += "# HELP vccdb_index_rebuild_entities_total Total number of entities processed during index rebuilds\n";
        out += "# TYPE vccdb_index_rebuild_entities_total counter\n";
        out += "vccdb_index_rebuild_entities_total " + std::to_string(rebuild_entities) + "\n";

    // Query metrics from SecondaryIndexManager
    auto& qmetrics = secondary_index_->getQueryMetrics();
    uint64_t cursor_anchor_hits = qmetrics.cursor_anchor_hits_total.load(std::memory_order_relaxed);
    uint64_t range_scan_steps = qmetrics.range_scan_steps_total.load(std::memory_order_relaxed);
    out += "# HELP vccdb_cursor_anchor_hits_total Total number of cursor anchor usages in ORDER BY pagination\n";
    out += "# TYPE vccdb_cursor_anchor_hits_total counter\n";
    out += "vccdb_cursor_anchor_hits_total " + std::to_string(cursor_anchor_hits) + "\n";
    out += "# HELP vccdb_range_scan_steps_total Total index scan steps performed during range scans\n";
    out += "# TYPE vccdb_range_scan_steps_total counter\n";
    out += "vccdb_range_scan_steps_total " + std::to_string(range_scan_steps) + "\n";

    // Page fetch time histogram (ms) for cursor pagination
    auto export_page_bucket = [&](const char* name, uint64_t val){ out += std::string(name) + " " + std::to_string(val) + "\n"; };
    out += "# HELP vccdb_page_fetch_time_ms_bucket Cursor page fetch time histogram buckets (ms)\n";
    out += "# TYPE vccdb_page_fetch_time_ms_bucket histogram\n";
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"1\"}", page_bucket_1ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"5\"}", page_bucket_5ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"10\"}", page_bucket_10ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"25\"}", page_bucket_25ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"50\"}", page_bucket_50ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"100\"}", page_bucket_100ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"250\"}", page_bucket_250ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"500\"}", page_bucket_500ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"1000\"}", page_bucket_1000ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"5000\"}", page_bucket_5000ms_.load(std::memory_order_relaxed));
    export_page_bucket("vccdb_page_fetch_time_ms_bucket{le=\"+Inf\"}", page_bucket_inf_.load(std::memory_order_relaxed));
    out += "# HELP vccdb_page_fetch_time_ms_sum Total cursor page fetch time in milliseconds\n";
    out += "# TYPE vccdb_page_fetch_time_ms_sum counter\n";
    out += "vccdb_page_fetch_time_ms_sum " + std::to_string(page_sum_ms_.load(std::memory_order_relaxed)) + "\n";
    out += "# HELP vccdb_page_fetch_time_ms_count Total number of cursor pages fetched\n";
    out += "# TYPE vccdb_page_fetch_time_ms_count counter\n";
    out += "vccdb_page_fetch_time_ms_count " + std::to_string(page_count_.load(std::memory_order_relaxed)) + "\n";

        // Build plain text response with proper content-type
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "THEMIS/0.1.0");
        res.set(http::field::content_type, "text/plain; version=0.0.4");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(out);
        res.prepare_payload();
        return res;
    } catch (const std::exception& e) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return makeErrorResponse(http::status::internal_server_error, std::string("metrics error: ") + e.what(), req);
    }
}

void HttpServer::recordPageFetch(std::chrono::milliseconds duration_ms) {
    using namespace std::chrono;
    uint64_t ms = static_cast<uint64_t>(duration_ms.count());
    // Cumulative buckets: each bucket counts all values <= its upper bound
    // Buckets: 1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1000ms, 5000ms, +Inf
    if (ms <= 1) page_bucket_1ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 5) page_bucket_5ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 10) page_bucket_10ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 25) page_bucket_25ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 50) page_bucket_50ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 100) page_bucket_100ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 250) page_bucket_250ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 500) page_bucket_500ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 1000) page_bucket_1000ms_.fetch_add(1, std::memory_order_relaxed);
    if (ms <= 5000) page_bucket_5000ms_.fetch_add(1, std::memory_order_relaxed);
    // +Inf bucket always increments (cumulative count of all observations)
    page_bucket_inf_.fetch_add(1, std::memory_order_relaxed);
    page_sum_ms_.fetch_add(ms, std::memory_order_relaxed);
    page_count_.fetch_add(1, std::memory_order_relaxed);
}

http::response<http::string_body> HttpServer::handleGetEntity(
    const http::request<http::string_body>& req
) {
    try {
        // Extract entity key from path: /entities/{key}
        auto key = extractPathParam(std::string(req.target()), "/entities/");
        if (key.empty()) {
            return makeErrorResponse(http::status::bad_request, "Missing entity key", req);
        }

        // Retrieve entity blob
        auto blob_opt = storage_->get(key);
        
        if (!blob_opt.has_value()) {
            return makeErrorResponse(http::status::not_found, 
                "Entity not found", req);
        }

        // Convert blob to string for JSON
        const auto& blob_vec = blob_opt.value();
        std::string blob_str(blob_vec.begin(), blob_vec.end());

        // Return blob as JSON value
        json response = {
            {"key", key},
            {"blob", blob_str}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const std::exception& e) {
        THEMIS_ERROR("GET entity error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePutEntity(
    const http::request<http::string_body>& req
) {
    try {
        // Parse request body
        auto body_json = json::parse(req.body());
        
        std::string key;
        if (body_json.contains("key")) {
            key = body_json["key"].get<std::string>();
        } else {
            // Extract from path if PUT /entities/{key}
            key = extractPathParam(std::string(req.target()), "/entities/");
        }

        if (key.empty()) {
            return makeErrorResponse(http::status::bad_request, "Missing entity key", req);
        }

        if (!body_json.contains("blob")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'blob' field", req);
        }

        // Split key into table:pk
        auto pos = key.find(':');
        if (pos == std::string::npos || pos == 0 || pos == key.size()-1) {
            return makeErrorResponse(http::status::bad_request, "Key must be in format 'table:pk'", req);
        }
        std::string table = key.substr(0, pos);
        std::string pk = key.substr(pos+1);

        // Build BaseEntity from blob (assume JSON payload string)
        std::string blob_str = body_json["blob"].get<std::string>();
        BaseEntity entity = BaseEntity::fromJson(pk, blob_str);

        // Upsert via SecondaryIndexManager to keep indexes consistent
        auto st = secondary_index_->put(table, entity);
        if (!st.ok) {
            // Check for unique constraint violation
            if (st.message.find("Unique constraint violation") != std::string::npos) {
                return makeErrorResponse(http::status::conflict,
                    st.message, req);
            }
            return makeErrorResponse(http::status::internal_server_error,
                "Index/Storage update failed: " + st.message, req);
        }

        json response = {
            {"success", true},
            {"key", key},
            {"blob_size", blob_str.size()}
        };
        return makeResponse(http::status::created, response.dump(), req);

    } catch (const json::exception& e) {
        THEMIS_ERROR("PUT entity JSON error: {}", e.what());
        return makeErrorResponse(http::status::bad_request, 
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        THEMIS_ERROR("PUT entity error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleDeleteEntity(
    const http::request<http::string_body>& req
) {
    try {
        auto key = extractPathParam(std::string(req.target()), "/entities/");
        if (key.empty()) {
            return makeErrorResponse(http::status::bad_request, "Missing entity key", req);
        }

        // Split key into table:pk
        auto pos = key.find(':');
        if (pos == std::string::npos || pos == 0 || pos == key.size()-1) {
            return makeErrorResponse(http::status::bad_request, "Key must be in format 'table:pk'", req);
        }
        std::string table = key.substr(0, pos);
        std::string pk = key.substr(pos+1);

        auto st = secondary_index_->erase(table, pk);
        if (!st.ok) {
            return makeErrorResponse(http::status::internal_server_error,
                "Index/Storage delete failed: " + st.message, req);
        }

        json response = {
            {"success", true},
            {"key", key}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const std::exception& e) {
        THEMIS_ERROR("DELETE entity error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleQuery(
    const http::request<http::string_body>& req
) {
    try {
        auto body = json::parse(req.body());
        if (!body.contains("table")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'table'", req);
        }

        std::string table = body["table"].get<std::string>();
        std::vector<themis::PredicateEq> preds;
        if (body.contains("predicates")) {
            for (const auto& p : body["predicates"]) {
                if (!p.contains("column") || !p.contains("value")) {
                    return makeErrorResponse(http::status::bad_request, "Each predicate needs 'column' and 'value'", req);
                }
                preds.push_back({p["column"].get<std::string>(), p["value"].get<std::string>()});
            }
        }

        // Range predicates (optional)
        std::vector<themis::PredicateRange> rpreds;
        if (body.contains("range")) {
            for (const auto& r : body["range"]) {
                if (!r.contains("column")) {
                    return makeErrorResponse(http::status::bad_request, "Each range needs 'column'", req);
                }
                themis::PredicateRange pr;
                pr.column = r["column"].get<std::string>();
                if (r.contains("gte")) pr.lower = r["gte"].get<std::string>();
                if (r.contains("lte")) pr.upper = r["lte"].get<std::string>();
                pr.includeLower = true; pr.includeUpper = true;
                if (r.contains("includeLower")) pr.includeLower = r["includeLower"].get<bool>();
                if (r.contains("includeUpper")) pr.includeUpper = r["includeUpper"].get<bool>();
                rpreds.push_back(std::move(pr));
            }
        }

        // Optional ORDER BY
        std::optional<themis::OrderBy> orderBy;
        if (body.contains("order_by")) {
            const auto& ob = body["order_by"];
            if (!ob.contains("column")) return makeErrorResponse(http::status::bad_request, "order_by requires 'column'", req);
            themis::OrderBy o; o.column = ob["column"].get<std::string>();
            o.desc = ob.contains("desc") ? ob["desc"].get<bool>() : false;
            o.limit = ob.contains("limit") ? ob["limit"].get<size_t>() : 1000;
            orderBy = o;
        }

    bool optimize = body.contains("optimize") ? body["optimize"].get<bool>() : true;
    bool allow_full_scan = body.contains("allow_full_scan") ? body["allow_full_scan"].get<bool>() : false;
    bool explain = body.contains("explain") ? body["explain"].get<bool>() : false;
        std::string ret = body.contains("return") ? body["return"].get<std::string>() : std::string("entities");

    themis::ConjunctiveQuery q{table, preds};
    q.rangePredicates = std::move(rpreds);
    q.orderBy = orderBy;
        themis::QueryEngine engine(*storage_, *secondary_index_);

        // Optional plan/explain info
        std::string exec_mode;
        nlohmann::json plan_json;

        if (ret == "keys") {
            std::pair<themis::QueryEngine::Status, std::vector<std::string>> res;
            if (allow_full_scan) {
                exec_mode = "full_scan_fallback";
                res = engine.executeAndKeysWithFallback(q, optimize);
            } else {
                if (optimize) {
                    themis::QueryOptimizer opt(*secondary_index_);
                    auto plan = opt.chooseOrderForAndQuery(q);
                    res = opt.executeOptimizedKeys(engine, q, plan);
                    exec_mode = "index_optimized";
                    if (explain) {
                        plan_json["mode"] = exec_mode;
                        plan_json["order"] = nlohmann::json::array();
                        for (const auto& p : plan.orderedPredicates) {
                            plan_json["order"].push_back({{"column", p.column}, {"value", p.value}});
                        }
                        plan_json["estimates"] = nlohmann::json::array();
                        for (const auto& d : plan.details) {
                            plan_json["estimates"].push_back({
                                {"column", d.pred.column}, {"value", d.pred.value},
                                {"estimatedCount", d.estimatedCount}, {"capped", d.capped}
                            });
                        }
                    }
                } else {
                    exec_mode = "index_parallel";
                    res = engine.executeAndKeys(q);
                    if (explain) {
                        plan_json = {
                            {"mode", exec_mode},
                            {"order", nlohmann::json::array()}
                        };
                        for (const auto& p : q.predicates) {
                            plan_json["order"].push_back({{"column", p.column}, {"value", p.value}});
                        }
                    }
                }
            }
            if (!res.first.ok) {
                return makeErrorResponse(http::status::bad_request, res.first.message, req);
            }
            json j = {{"table", table}, {"count", res.second.size()}, {"keys", res.second}};
            if (explain && !plan_json.is_null()) j["plan"] = plan_json;
            return makeResponse(http::status::ok, j.dump(), req);
        } else {
            std::pair<themis::QueryEngine::Status, std::vector<themis::BaseEntity>> res;
            if (allow_full_scan) {
                exec_mode = "full_scan_fallback";
                res = engine.executeAndEntitiesWithFallback(q, optimize);
            } else {
                if (optimize) {
                    themis::QueryOptimizer opt(*secondary_index_);
                    auto plan = opt.chooseOrderForAndQuery(q);
                    res = opt.executeOptimizedEntities(engine, q, plan);
                    exec_mode = "index_optimized";
                    if (explain) {
                        plan_json["mode"] = exec_mode;
                        plan_json["order"] = nlohmann::json::array();
                        for (const auto& p : plan.orderedPredicates) {
                            plan_json["order"].push_back({{"column", p.column}, {"value", p.value}});
                        }
                        plan_json["estimates"] = nlohmann::json::array();
                        for (const auto& d : plan.details) {
                            plan_json["estimates"].push_back({
                                {"column", d.pred.column}, {"value", d.pred.value},
                                {"estimatedCount", d.estimatedCount}, {"capped", d.capped}
                            });
                        }
                    }
                } else {
                    exec_mode = "index_parallel";
                    res = engine.executeAndEntities(q);
                    if (explain) {
                        plan_json = {
                            {"mode", exec_mode},
                            {"order", nlohmann::json::array()}
                        };
                        for (const auto& p : q.predicates) {
                            plan_json["order"].push_back({{"column", p.column}, {"value", p.value}});
                        }
                    }
                }
            }
            if (!res.first.ok) {
                return makeErrorResponse(http::status::bad_request, res.first.message, req);
            }
            // Serialize entities as JSON strings (default path)
            json entities = json::array();
            for (const auto& e : res.second) {
                entities.push_back(e.toJson());
            }
            json j = {{"table", table}, {"count", res.second.size()}, {"entities", entities}};
            if (explain && !plan_json.is_null()) j["plan"] = plan_json;
            return makeResponse(http::status::ok, j.dump(), req);
        }

    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleQueryAql(
    const http::request<http::string_body>& req
) {
    try {
        auto body = json::parse(req.body());
        
        // Validate request
        if (!body.contains("query")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'query' field", req);
        }

        std::string aql_query = body["query"].get<std::string>();
        bool explain = body.contains("explain") ? body["explain"].get<bool>() : false;
        bool optimize = body.contains("optimize") ? body["optimize"].get<bool>() : true;
        bool allow_full_scan = body.contains("allow_full_scan") ? body["allow_full_scan"].get<bool>() : false;
        
    // Cursor-based pagination parameters
        std::string cursor_token = body.contains("cursor") ? body["cursor"].get<std::string>() : "";
        bool use_cursor = body.contains("use_cursor") ? body["use_cursor"].get<bool>() : false;
    auto page_fetch_start = std::chrono::steady_clock::now();
        
    // Cursor-Pagination: Wir verlagern Cursor-Handling in die Engine (Anker-basiert)
        
        // Optional: Frontier-Limits für Traversal (Soft-Limit)
        size_t max_frontier_size = body.contains("max_frontier_size") ? body["max_frontier_size"].get<size_t>() : 100000;
        size_t max_results = body.contains("max_results") ? body["max_results"].get<size_t>() : 10000;
        
        // Parse AQL query
        themis::query::AQLParser parser;
        auto parse_result = parser.parse(aql_query);
        
        if (!parse_result.success) {
            std::string error_msg = "AQL parse error: " + parse_result.error.message + 
                " at line " + std::to_string(parse_result.error.line) + 
                ", column " + std::to_string(parse_result.error.column);
            
            if (!parse_result.error.context.empty()) {
                error_msg += " (context: " + parse_result.error.context + ")";
            }
            
            return makeErrorResponse(http::status::bad_request, error_msg, req);
        }
        
    // Translate AST to Query (relational or traversal)
    auto translate_result = themis::AQLTranslator::translate(parse_result.query);
        
        if (!translate_result.success) {
            return makeErrorResponse(http::status::bad_request,
                "AQL translation error: " + translate_result.error_message, req);
        }
        
    // If traversal present, execute via GraphIndexManager
        if (translate_result.traversal.has_value()) {
            if (!graph_index_) {
                return makeErrorResponse(http::status::bad_request, "Graph traversal requested but graph index manager is not available", req);
            }
            const auto& t = translate_result.traversal.value();
            if (t.minDepth < 0 || t.maxDepth < 0 || t.maxDepth < t.minDepth) {
                return makeErrorResponse(http::status::bad_request, "Invalid depth range in traversal", req);
            }

            // Bestimme Return-Modus anhand RETURN-Ausdruck: v (default), e, p
            enum class RetMode { Vertex, Edge, Path };
            RetMode retMode = RetMode::Vertex;
            if (parse_result.query->return_node && parse_result.query->return_node->expression) {
                using namespace themis::query;
                auto* var = dynamic_cast<VariableExpr*>(parse_result.query->return_node->expression.get());
                if (var) {
                    if (var->name == "e") retMode = RetMode::Edge;
                    else if (var->name == "p") retMode = RetMode::Path;
                }
            }

            // Extrahiere einfache FILTER-Prädikate auf v/e im Format: FILTER v.<field> == <literal|funktion> oder FILTER e.<field> == <literal|funktion>
            struct SimplePred {
                enum class Op { Eq, Neq, Lt, Lte, Gt, Gte };
                char var; // 'v' oder 'e'
                std::string field;
                nlohmann::json literal; // als JSON-Literal
                Op op;
            };
            // Unterstützte Funktionsauswertung zur Reduktion auf Literale
            std::function<bool(std::shared_ptr<themis::query::Expression>, nlohmann::json&)> evalExprToLiteral;
            evalExprToLiteral = [&](std::shared_ptr<themis::query::Expression> expr, nlohmann::json& out)->bool {
                using namespace themis::query;
                if (auto* l = dynamic_cast<LiteralExpr*>(expr.get())) {
                    out = l->toJSON()["value"]; return true;
                }
                auto* fc = dynamic_cast<FunctionCallExpr*>(expr.get());
                if (!fc) return false;
                // Funktionsnamen case-insensitiv vergleichen
                std::string name = fc->name; std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                auto getArgLit = [&](size_t idx, nlohmann::json& argOut)->bool{
                    if (idx >= fc->arguments.size()) return false;
                    return evalExprToLiteral(fc->arguments[idx], argOut);
                };
                if (name == "abs") {
                    nlohmann::json a; if (!getArgLit(0, a)) return false;
                    if (a.is_number_integer()) { long long v = a.get<long long>(); if (v < 0) v = -v; out = v; return true; }
                    if (a.is_number_float()) { double v = a.get<double>(); if (v < 0) v = -v; out = v; return true; }
                    return false;
                }
                if (name == "ceil") {
                    nlohmann::json a; if (!getArgLit(0, a)) return false;
                    if (a.is_number()) { out = std::ceil(a.get<double>()); return true; }
                    return false;
                }
                if (name == "floor") {
                    nlohmann::json a; if (!getArgLit(0, a)) return false;
                    if (a.is_number()) { out = std::floor(a.get<double>()); return true; }
                    return false;
                }
                if (name == "round") {
                    nlohmann::json a; if (!getArgLit(0, a)) return false;
                    if (a.is_number()) { out = std::llround(a.get<double>()); return true; }
                    return false;
                }
                if (name == "pow") {
                    nlohmann::json a,b; if (!getArgLit(0, a) || !getArgLit(1, b)) return false;
                    if (a.is_number() && b.is_number()) { out = std::pow(a.get<double>(), b.get<double>()); return true; }
                    return false;
                }
                auto parseIso = [&](const std::string& s, std::tm& tm)->bool{
                    memset(&tm, 0, sizeof tm);
                    int Y=0,M=0,D=0,h=0,m=0,sec=0; char T='\0', Z='\0';
                    if (s.size() == 10 && std::sscanf(s.c_str(), "%d-%d-%d", &Y,&M,&D) == 3) {
                        tm.tm_year = Y-1900; tm.tm_mon = M-1; tm.tm_mday = D; tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; return true;
                    }
                    if (std::sscanf(s.c_str(), "%d-%d-%d%c%d:%d:%d%c", &Y,&M,&D,&T,&h,&m,&sec,&Z) >= 7) {
                        tm.tm_year = Y-1900; tm.tm_mon = M-1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec; return true;
                    }
                    return false;
                };
                auto tmToDateStr = [&](const std::tm& tm)->std::string{
                    char buf[32]; std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday); return std::string(buf);
                };
                if (name == "date_trunc") {
                    // DATE_TRUNC(unit, dateStr)
                    nlohmann::json unitJ, dateJ; if (!getArgLit(0, unitJ) || !getArgLit(1, dateJ)) return false;
                    if (!unitJ.is_string() || !dateJ.is_string()) return false;
                    std::string unit = unitJ.get<std::string>(); std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
                    std::tm tm{}; if (!parseIso(dateJ.get<std::string>(), tm)) return false;
                    if (unit == "day") { /* already normalized by tmToDateStr */ }
                    else if (unit == "month") { tm.tm_mday = 1; }
                    else if (unit == "year") { tm.tm_mon = 0; tm.tm_mday = 1; }
                    else return false;
                    out = tmToDateStr(tm); return true;
                }
                if (name == "date_add" || name == "date_sub") {
                    // DATE_ADD(dateStr, amount, unit) – unterstützt 'day','month','year'
                    nlohmann::json dateJ, amountJ, unitJ; if (!getArgLit(0, dateJ) || !getArgLit(1, amountJ) || !getArgLit(2, unitJ)) return false;
                    if (!dateJ.is_string() || !amountJ.is_number_integer() || !unitJ.is_string()) return false;
                    std::string unit = unitJ.get<std::string>(); std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
                    std::tm tm{}; if (!parseIso(dateJ.get<std::string>(), tm)) return false;
                    long long amt = amountJ.get<long long>(); if (name == "date_sub") amt = -amt;
                    if (unit == "day") {
                        time_t t = _mkgmtime(&tm); if (t == -1) return false;
                        t += static_cast<time_t>(amt * 86400);
                        std::tm outTm{}; gmtime_s(&outTm, &t);
                        out = tmToDateStr(outTm); return true;
                    } else if (unit == "month") {
                        tm.tm_mon += static_cast<int>(amt);
                        time_t t = _mkgmtime(&tm); if (t == -1) return false;
                        std::tm outTm{}; gmtime_s(&outTm, &t);
                        out = tmToDateStr(outTm); return true;
                    } else if (unit == "year") {
                        tm.tm_year += static_cast<int>(amt);
                        time_t t = _mkgmtime(&tm); if (t == -1) return false;
                        std::tm outTm{}; gmtime_s(&outTm, &t);
                        out = tmToDateStr(outTm); return true;
                    } else {
                        return false;
                    }
                }
                if (name == "now") {
                    // Gibt YYYY-MM-DD zurück (UTC)
                    std::time_t t = std::time(nullptr); std::tm tm{}; gmtime_s(&tm, &t);
                    out = tmToDateStr(tm); return true;
                }
                return false; // unbekannte Funktion
            };
            std::vector<SimplePred> preds;
            // XOR-Unterstützung: Paare einfacher Prädikate (links XOR rechts)
            std::vector<std::pair<SimplePred, SimplePred>> xorPreds;
            if (!parse_result.query->filters.empty()) {
                using namespace themis::query;
                for (const auto& f : parse_result.query->filters) {
                    auto* be = dynamic_cast<BinaryOpExpr*>(f->condition.get());
                    if (!be) continue;

                    // Falls es ein XOR ist, versuchen wir links und rechts je als SimplePred zu parsen
                    if (be->op == BinaryOperator::Xor) {
                        auto mapOpInner = [&](BinaryOperator bop)->std::optional<SimplePred::Op> {
                            switch (bop) {
                                case BinaryOperator::Eq:  return SimplePred::Op::Eq;
                                case BinaryOperator::Neq: return SimplePred::Op::Neq;
                                case BinaryOperator::Lt:  return SimplePred::Op::Lt;
                                case BinaryOperator::Lte: return SimplePred::Op::Lte;
                                case BinaryOperator::Gt:  return SimplePred::Op::Gt;
                                case BinaryOperator::Gte: return SimplePred::Op::Gte;
                                default: return std::nullopt;
                            }
                        };
                        auto parseSide = [&](std::shared_ptr<Expression> e, char& var, std::string& field) -> bool {
                            auto* fa = dynamic_cast<FieldAccessExpr*>(e.get());
                            if (!fa) return false;
                            auto* v = dynamic_cast<VariableExpr*>(fa->object.get());
                            if (!v) return false;
                            if (v->name != "v" && v->name != "e") return false;
                            var = v->name[0];
                            field = fa->field;
                            return true;
                        };
                        auto parseSimpleFromExpr = [&](std::shared_ptr<Expression> expr, SimplePred& out)->bool {
                            auto* be2 = dynamic_cast<BinaryOpExpr*>(expr.get());
                            if (!be2) return false;
                            auto op_m2 = mapOpInner(be2->op);
                            if (!op_m2.has_value()) return false;
                            char var=0; std::string field; nlohmann::json lit;
                            if (parseSide(be2->left, var, field)) {
                                if (evalExprToLiteral(be2->right, lit)) { out = {var, field, lit, *op_m2}; return true; }
                                return false;
                            }
                            {
                                nlohmann::json leftLit;
                                bool hasLeft = evalExprToLiteral(be2->left, leftLit);
                                if (hasLeft && parseSide(be2->right, var, field)) {
                                    auto invert = [&](SimplePred::Op o){
                                        switch (o) {
                                            case SimplePred::Op::Lt:  return SimplePred::Op::Gt;
                                            case SimplePred::Op::Lte: return SimplePred::Op::Gte;
                                            case SimplePred::Op::Gt:  return SimplePred::Op::Lt;
                                            case SimplePred::Op::Gte: return SimplePred::Op::Lte;
                                            default: return o;
                                        }
                                    };
                                    lit = leftLit; out = {var, field, lit, invert(*op_m2)}; return true;
                                }
                            }
                            return false;
                        };

                        SimplePred left{}, right{};
                        if (parseSimpleFromExpr(be->left, left) && parseSimpleFromExpr(be->right, right)) {
                            xorPreds.emplace_back(left, right);
                        }
                        // XOR-Filter als Ganzes sind verarbeitet; nicht in einfache Prädikate aufnehmen
                        continue;
                    }
                    auto mapOp = [&](BinaryOperator bop)->std::optional<SimplePred::Op> {
                        switch (bop) {
                            case BinaryOperator::Eq:  return SimplePred::Op::Eq;
                            case BinaryOperator::Neq: return SimplePred::Op::Neq;
                            case BinaryOperator::Lt:  return SimplePred::Op::Lt;
                            case BinaryOperator::Lte: return SimplePred::Op::Lte;
                            case BinaryOperator::Gt:  return SimplePred::Op::Gt;
                            case BinaryOperator::Gte: return SimplePred::Op::Gte;
                            default: return std::nullopt;
                        }
                    };
                    auto op_m = mapOp(be->op);
                    if (!op_m.has_value()) continue; // nur Vergleichsoperatoren

                    auto parseSide = [&](std::shared_ptr<Expression> e, char& var, std::string& field) -> bool {
                        // Erwartet FieldAccess(Variable('v'|'e'), field)
                        auto* fa = dynamic_cast<FieldAccessExpr*>(e.get());
                        if (!fa) return false;
                        auto* v = dynamic_cast<VariableExpr*>(fa->object.get());
                        if (!v) return false;
                        if (v->name != "v" && v->name != "e") return false;
                        var = v->name[0];
                        field = fa->field;
                        return true;
                    };

                    char var = 0; std::string field; nlohmann::json lit;
                    // v.field == <literal|funktion>
                    if (parseSide(be->left, var, field)) {
                        if (evalExprToLiteral(be->right, lit)) {
                            preds.push_back({var, field, lit, *op_m});
                        }
                        continue;
                    }
                    // <literal|funktion> == v.field
                    {
                        nlohmann::json leftLit;
                        bool hasLeft = evalExprToLiteral(be->left, leftLit);
                        if (hasLeft) {
                            if (parseSide(be->right, var, field)) {
                                lit = leftLit; 
                                // Operator invertieren (literal OP field) -> (field OP' literal)
                                auto invert = [&](SimplePred::Op o){
                                    switch (o) {
                                        case SimplePred::Op::Lt:  return SimplePred::Op::Gt;
                                        case SimplePred::Op::Lte: return SimplePred::Op::Gte;
                                        case SimplePred::Op::Gt:  return SimplePred::Op::Lt;
                                        case SimplePred::Op::Gte: return SimplePred::Op::Lte;
                                        default: return o; // Eq/Neq symmetrisch
                                    }
                                };
                                preds.push_back({var, field, lit, invert(*op_m)});
                            }
                        }
                    }
                }
            }

            auto cmp = [&](const std::string& a, const nlohmann::json& b, SimplePred::Op op)->bool{
                // Versuch: Zahl-Vergleich
                auto toDouble = [](const std::string& s, double& out)->bool{
                    char* end=nullptr; out = strtod(s.c_str(), &end); return end && *end=='\0';
                };
                auto toBool = [](const std::string& s, bool& out)->bool{
                    if (s == "true" || s == "1") { out = true; return true; }
                    if (s == "false" || s == "0") { out = false; return true; }
                    return false;
                };
                auto parseDate = [](const std::string& s, time_t& t)->bool{
                    // Unterstützt YYYY-MM-DD oder YYYY-MM-DDTHH:MM:SSZ
                    std::tm tm{}; memset(&tm, 0, sizeof tm);
                    if (s.size() == 10 && std::sscanf(s.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
                        tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
                        t = _mkgmtime(&tm); return t != -1;
                    }
                    int Y,M,D,h=0,m=0,sec=0; char Z='\0', T='\0';
                    if (std::sscanf(s.c_str(), "%d-%d-%d%c%d:%d:%d%c", &Y,&M,&D,&T,&h,&m,&sec,&Z) >= 7) {
                        tm.tm_year = Y-1900; tm.tm_mon = M-1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;
                        t = _mkgmtime(&tm); return t != -1;
                    }
                    return false;
                };

                // Literaltypen prüfen
                if (b.is_number()) {
                    double lit = b.get<double>();
                    double aval; if (!toDouble(a, aval)) return false;
                    switch (op) {
                        case SimplePred::Op::Eq:  return aval == lit;
                        case SimplePred::Op::Neq: return aval != lit;
                        case SimplePred::Op::Lt:  return aval <  lit;
                        case SimplePred::Op::Lte: return aval <= lit;
                        case SimplePred::Op::Gt:  return aval >  lit;
                        case SimplePred::Op::Gte: return aval >= lit;
                    }
                } else if (b.is_boolean()) {
                    bool lit = b.get<bool>(); bool av;
                    if (!toBool(a, av)) return false;
                    switch (op) {
                        case SimplePred::Op::Eq:  return av == lit;
                        case SimplePred::Op::Neq: return av != lit;
                        default: return false; // <,> semantisch nicht definiert
                    }
                } else if (b.is_string()) {
                    const std::string lit = b.get<std::string>();
                    // Datumsvergleich falls beide ISO-ähnlich
                    time_t ta, tb; bool da = parseDate(a, ta), db = parseDate(lit, tb);
                    if (da && db) {
                        switch (op) {
                            case SimplePred::Op::Eq:  return ta == tb;
                            case SimplePred::Op::Neq: return ta != tb;
                            case SimplePred::Op::Lt:  return ta <  tb;
                            case SimplePred::Op::Lte: return ta <= tb;
                            case SimplePred::Op::Gt:  return ta >  tb;
                            case SimplePred::Op::Gte: return ta >= tb;
                        }
                    }
                    // Default: lexikographisch
                    int c = a.compare(lit);
                    switch (op) {
                        case SimplePred::Op::Eq:  return c == 0;
                        case SimplePred::Op::Neq: return c != 0;
                        case SimplePred::Op::Lt:  return c <  0;
                        case SimplePred::Op::Lte: return c <= 0;
                        case SimplePred::Op::Gt:  return c >  0;
                        case SimplePred::Op::Gte: return c >= 0;
                    }
                }
                return false;
            };

            // Allgemeine Filterauswertung (AND/OR/NOT/XOR) auf AST-Basis
            auto getVFieldString = [&](const std::string& pk, const std::string& field)->std::optional<std::string>{
                if (field == "_key") return pk;
                auto blob = storage_->get(pk);
                if (!blob) return std::nullopt;
                try {
                    auto ent = themis::BaseEntity::deserialize(pk, *blob);
                    return ent.getFieldAsString(field);
                } catch (...) { return std::nullopt; }
            };
            auto getEFieldString = [&](const std::string& edgeId, const std::string& field)->std::optional<std::string>{
                if (field == "id") return edgeId;
                auto eblob = storage_->get(themis::KeySchema::makeGraphEdgeKey(edgeId));
                if (!eblob) return std::nullopt;
                try {
                    auto ent = themis::BaseEntity::deserialize(edgeId, *eblob);
                    return ent.getFieldAsString(field);
                } catch (...) { return std::nullopt; }
            };

            using namespace themis::query;
            size_t filterShortCircuits = 0;  // Zählt Short-Circuit-Evaluationen in Filtern
            std::function<bool(const Expression*, const std::string&, const std::optional<std::string>&)> evalBoolExpr;
            evalBoolExpr = [&](const Expression* e, const std::string& vpk, const std::optional<std::string>& eid)->bool{
                if (!e) return true;
                if (auto* ue = dynamic_cast<const UnaryOpExpr*>(e)) {
                    if (ue->op == UnaryOperator::Not) return !evalBoolExpr(ue->operand.get(), vpk, eid);
                    return false;
                }
                if (auto* be = dynamic_cast<const BinaryOpExpr*>(e)) {
                    auto evalCmp = [&](const Expression* left, BinaryOperator op, const Expression* right)->bool{
                        // Unterstütze: FieldAccess(v|e).field <op> (Literal|Funktion) und umgekehrt
                        auto parseFA = [&](const Expression* ex, char& var, std::string& field)->bool{
                            auto* fa = dynamic_cast<const FieldAccessExpr*>(ex);
                            if (!fa) return false;
                            auto* v = dynamic_cast<const VariableExpr*>(fa->object.get());
                            if (!v) return false;
                            if (v->name != "v" && v->name != "e") return false;
                            var = v->name[0]; field = fa->field; return true;
                        };
                        auto mapOp = [&](BinaryOperator bop)->std::optional<SimplePred::Op>{
                            switch (bop) {
                                case BinaryOperator::Eq: return SimplePred::Op::Eq;
                                case BinaryOperator::Neq: return SimplePred::Op::Neq;
                                case BinaryOperator::Lt: return SimplePred::Op::Lt;
                                case BinaryOperator::Lte: return SimplePred::Op::Lte;
                                case BinaryOperator::Gt: return SimplePred::Op::Gt;
                                case BinaryOperator::Gte: return SimplePred::Op::Gte;
                                default: return std::nullopt;
                            }
                        };
                        auto op_m = mapOp(op); if (!op_m) return false;

                        char var=0; std::string field; nlohmann::json lit;
                        if (parseFA(left, var, field)) {
                            // rechts Literal/Funktion
                            if (!evalExprToLiteral(std::shared_ptr<Expression>(const_cast<Expression*>(right), [](Expression*){}), lit)) return false;
                            std::optional<std::string> val;
                            if (var=='v') val = getVFieldString(vpk, field);
                            else { if (!eid) return false; val = getEFieldString(*eid, field); }
                            if (!val) return false;
                            return cmp(*val, lit, *op_m);
                        }
                        if (parseFA(right, var, field)) {
                            // links Literal/Funktion
                            if (!evalExprToLiteral(std::shared_ptr<Expression>(const_cast<Expression*>(left), [](Expression*){}), lit)) return false;
                            // invertiere Operator
                            auto invert = [&](SimplePred::Op o){
                                switch (o) {
                                    case SimplePred::Op::Lt: return SimplePred::Op::Gt;
                                    case SimplePred::Op::Lte: return SimplePred::Op::Gte;
                                    case SimplePred::Op::Gt: return SimplePred::Op::Lt;
                                    case SimplePred::Op::Gte: return SimplePred::Op::Lte;
                                    default: return o;
                                }
                            };
                            auto op2 = invert(*op_m);
                            std::optional<std::string> val;
                            if (var=='v') val = getVFieldString(vpk, field);
                            else { if (!eid) return false; val = getEFieldString(*eid, field); }
                            if (!val) return false;
                            return cmp(*val, lit, op2);
                        }
                        return false; // nicht unterstütztes Muster
                    };

                    switch (be->op) {
                        case BinaryOperator::And: {
                            bool l = evalBoolExpr(be->left.get(), vpk, eid);
                            if (!l) { filterShortCircuits++; return false; }
                            return evalBoolExpr(be->right.get(), vpk, eid);
                        }
                        case BinaryOperator::Or: {
                            bool l = evalBoolExpr(be->left.get(), vpk, eid);
                            if (l) { filterShortCircuits++; return true; }
                            return evalBoolExpr(be->right.get(), vpk, eid);
                        }
                        case BinaryOperator::Xor: { bool l = evalBoolExpr(be->left.get(), vpk, eid); bool r = evalBoolExpr(be->right.get(), vpk, eid); return l ^ r; }
                        case BinaryOperator::Eq:
                        case BinaryOperator::Neq:
                        case BinaryOperator::Lt:
                        case BinaryOperator::Lte:
                        case BinaryOperator::Gt:
                        case BinaryOperator::Gte:
                            return evalCmp(be->left.get(), be->op, be->right.get());
                        default:
                            return false;
                    }
                }
                // Literale/Variablen alleine als bool nicht unterstützt → false
                return false;
            };

            // Helper: prüfe, ob ein Ausdruck v/e-Referenzen enthält (für konstante Vorabprüfung)
            std::function<bool(const Expression*)> usesVE;
            usesVE = [&](const Expression* e)->bool{
                if (!e) return false;
                if (auto* le = dynamic_cast<const LiteralExpr*>(e)) {
                    (void)le; return false;
                }
                if (auto* ve = dynamic_cast<const VariableExpr*>(e)) {
                    return (ve->name == "v" || ve->name == "e");
                }
                if (auto* fa = dynamic_cast<const FieldAccessExpr*>(e)) {
                    // Prüfe, ob das Objekt eine Variable v/e ist
                    if (auto* objVar = dynamic_cast<VariableExpr*>(fa->object.get())) {
                        if (objVar->name == "v" || objVar->name == "e") return true;
                    }
                    // Auch verschachtelte Zugriffe können v/e enthalten
                    return usesVE(fa->object.get());
                }
                if (auto* ue = dynamic_cast<const UnaryOpExpr*>(e)) {
                    return usesVE(ue->operand.get());
                }
                if (auto* be = dynamic_cast<const BinaryOpExpr*>(e)) {
                    return usesVE(be->left.get()) || usesVE(be->right.get());
                }
                if (auto* fe = dynamic_cast<const FunctionCallExpr*>(e)) {
                    for (const auto& a : fe->arguments) {
                        if (usesVE(a.get())) return true;
                    }
                    return false;
                }
                return false;
            };

            // Vorab: Wenn alle FILTER-Ausdrücke keine v/e-Referenz enthalten, einmal bewerten und ggf. früh abbrechen
            if (!parse_result.query->filters.empty()) {
                bool anyUsesVE = false;
                for (const auto& f : parse_result.query->filters) {
                    if (usesVE(f->condition.get())) { anyUsesVE = true; break; }
                }
                if (!anyUsesVE) {
                    bool allPass = true;
                    for (const auto& f : parse_result.query->filters) {
                        if (!evalBoolExpr(f->condition.get(), std::string{}, std::nullopt)) { allPass = false; break; }
                    }
                    if (!allPass) {
                        nlohmann::json res;
                        res["table"] = "graph";
                        res["count"] = 0;
                        res["entities"] = nlohmann::json::array();
                        if (explain) {
                            nlohmann::json metrics;
                            metrics["constant_filter_precheck"] = true;
                            metrics["constant_filter_result"] = false;
                            metrics["edges_expanded"] = 0;
                            metrics["pruned_last_level"] = 0;
                            metrics["filter_evaluations_total"] = 1;
                            metrics["filter_short_circuits"] = 0;
                            metrics["frontier_processed_per_depth"] = nlohmann::json::object();
                            metrics["enqueued_per_depth"] = nlohmann::json::object();
                            res["metrics"] = metrics;
                        }
                        return makeResponse(http::status::ok, res.dump(), req);
                    }
                }
            }

            auto evalV = [&](const std::string& pk)->bool{
                for (const auto& p : preds) {
                    if (p.var != 'v') continue;
                    if (p.field == "_key") { // direkte PK-Vergleiche
                        if (!cmp(pk, p.literal, p.op)) return false;
                    } else {
                        // Lade Entity und vergleiche typbewusst
                        auto blob = storage_->get(pk);
                        if (!blob) return false;
                        try {
                            auto ent = themis::BaseEntity::deserialize(pk, *blob);
                            // bevorzugt String, für Zahlen könnte man auch getFieldAsDouble versuchen
                            auto valOpt = ent.getFieldAsString(p.field);
                            if (!valOpt) return false;
                            if (!cmp(*valOpt, p.literal, p.op)) return false;
                        } catch (...) { return false; }
                    }
                }
                return true;
            };

            auto evalSingleV = [&](const std::string& pk, const SimplePred& p)->bool{
                if (p.var != 'v') return true; // nicht zuständig
                if (p.field == "_key") {
                    return cmp(pk, p.literal, p.op);
                } else {
                    auto blob = storage_->get(pk);
                    if (!blob) return false;
                    try {
                        auto ent = themis::BaseEntity::deserialize(pk, *blob);
                        auto valOpt = ent.getFieldAsString(p.field);
                        if (!valOpt) return false;
                        return cmp(*valOpt, p.literal, p.op);
                    } catch (...) { return false; }
                }
            };

            auto evalE = [&](const std::string& edgeId)->bool{
                bool needLoad = false;
                for (const auto& p : preds) {
                    if (p.var != 'e') continue;
                    if (p.field == "id") {
                        if (!cmp(edgeId, p.literal, p.op)) return false;
                    } else {
                        needLoad = true; // _from/_to oder andere Felder
                    }
                }
                if (!needLoad) return true;
                auto blob = storage_->get(themis::KeySchema::makeGraphEdgeKey(edgeId));
                if (!blob) return false;
                try {
                    auto ent = themis::BaseEntity::deserialize(edgeId, *blob);
                    for (const auto& p : preds) {
                        if (p.var != 'e' || p.field == "id") continue;
                        auto valOpt = ent.getFieldAsString(p.field);
                        if (!valOpt) return false;
                        if (!cmp(*valOpt, p.literal, p.op)) return false;
                    }
                } catch (...) { return false; }
                return true;
            };

            auto evalSingleE = [&](const std::string& edgeId, const SimplePred& p)->bool{
                if (p.var != 'e') return true; // nicht zuständig
                if (p.field == "id") {
                    return cmp(edgeId, p.literal, p.op);
                }
                auto blob = storage_->get(themis::KeySchema::makeGraphEdgeKey(edgeId));
                if (!blob) return false;
                try {
                    auto ent = themis::BaseEntity::deserialize(edgeId, *blob);
                    auto valOpt = ent.getFieldAsString(p.field);
                    if (!valOpt) return false;
                    return cmp(*valOpt, p.literal, p.op);
                } catch (...) { return false; }
            };

            // BFS mit Eltern-/Kanten-Tracking für e/p
            struct ParentInfo { std::string parent; std::string edgeId; };
            std::unordered_set<std::string> visited;
            std::unordered_map<std::string, ParentInfo> parent;
            std::queue<std::pair<std::string,int>> qnodes;
            qnodes.push({t.startVertex, 0});
            visited.insert(t.startVertex);

            // Metriken
            bool constantFilterPrechecked = false; // bleibt false, wenn oben nicht gegriffen (true wäre nur beim Early-Return)
            size_t edgesExpanded = 0;
            size_t prunedLastLevel = 0;
            std::unordered_map<int, size_t> frontierProcessedPerDepth;
            std::unordered_map<int, size_t> enqueuedPerDepth;
            size_t filterEvaluationsTotal = 0;
            // filterShortCircuits bereits weiter oben definiert (Zeile ~1265)
            size_t frontierLimitHits = 0;
            size_t maxFrontierSizeReached = 0;
            bool resultLimitReached = false;

            // Ergebnis-Sammlungen
            std::vector<std::string> resultVertices;
            std::vector<std::string> resultEdgeIds;
            std::vector<std::string> resultTerminalVertices; // für Pfadrekonstruktion

            auto withinDepth = [&](int depth){ return depth >= t.minDepth && depth <= t.maxDepth; };

            while (!qnodes.empty()) {
                // Frontier-Size Limit Check (Soft Limit)
                if (qnodes.size() > max_frontier_size) {
                    frontierLimitHits++;
                    // Optional: Abbruch oder nur Warnung
                    // break;  // hart abbrechen (später konfigurierbar)
                }
                if (qnodes.size() > maxFrontierSizeReached) {
                    maxFrontierSizeReached = qnodes.size();
                }
                
                auto [node, depth] = qnodes.front();
                qnodes.pop();
                frontierProcessedPerDepth[depth]++;

                if (withinDepth(depth)) {
                    if (!(depth == 0 && t.minDepth > 0)) {
                        // Filterauswertung pro "Zeile" (Knoten + eingehende Kante) mit voller Bool-Logik
                        bool pass = true;
                        if (!parse_result.query->filters.empty()) {
                            filterEvaluationsTotal++;
                            std::optional<std::string> edgeIdOpt;
                            if (depth > 0) {
                                auto itp = parent.find(node);
                                if (itp != parent.end()) edgeIdOpt = itp->second.edgeId;
                            }
                            for (const auto& f : parse_result.query->filters) {
                                if (!evalBoolExpr(f->condition.get(), node, edgeIdOpt)) { pass = false; break; }
                            }
                        }

                        if (pass) {
                            // Result-Limit Check
                            size_t currentResultCount = (retMode == RetMode::Vertex) ? resultVertices.size() :
                                                        (retMode == RetMode::Edge) ? resultEdgeIds.size() :
                                                        resultTerminalVertices.size();
                            if (currentResultCount >= max_results) {
                                resultLimitReached = true;
                                // Optional: BFS abbrechen (später konfigurierbar)
                                // goto traversal_finished;
                            }
                            
                            if (retMode == RetMode::Vertex) {
                                resultVertices.push_back(node);
                            } else if (retMode == RetMode::Edge) {
                                auto it = parent.find(node);
                                if (it != parent.end()) {
                                    resultEdgeIds.push_back(it->second.edgeId);
                                }
                            } else { // Path
                                if (node != t.startVertex) {
                                    resultTerminalVertices.push_back(node);
                                } else if (t.minDepth == 0) {
                                    resultTerminalVertices.push_back(node);
                                }
                            }
                        }
                    }
                }
                if (depth == t.maxDepth) continue;

                auto enqueueOut = [&](const std::vector<themis::GraphIndexManager::AdjacencyInfo>& adj){
                    for (const auto& a : adj) {
                        const std::string& nb = a.targetPk;
                        edgesExpanded++;
                        // Konservative Pruning-Strategie am letzten Level: wende einfache v/e-Prädikate an
                        if (depth + 1 == t.maxDepth && !preds.empty()) {
                            bool drop = false;
                            for (const auto& p : preds) {
                                if (p.var == 'e' && !evalSingleE(a.edgeId, p)) { drop = true; break; }
                                if (p.var == 'v' && !evalSingleV(nb, p)) { drop = true; break; }
                            }
                            if (drop) { prunedLastLevel++; continue; }
                        }
                        if (visited.insert(nb).second) {
                            parent[nb] = {node, a.edgeId};
                            qnodes.push({nb, depth + 1});
                            enqueuedPerDepth[depth + 1]++;
                        }
                    }
                };
                auto enqueueIn = [&](const std::vector<themis::GraphIndexManager::AdjacencyInfo>& adj){
                    for (const auto& a : adj) {
                        const std::string& nb = a.targetPk; // bei inAdjacency ist targetPk = fromPk
                        edgesExpanded++;
                        // Konservative Pruning-Strategie am letzten Level
                        if (depth + 1 == t.maxDepth && !preds.empty()) {
                            bool drop = false;
                            for (const auto& p : preds) {
                                if (p.var == 'e' && !evalSingleE(a.edgeId, p)) { drop = true; break; }
                                if (p.var == 'v' && !evalSingleV(nb, p)) { drop = true; break; }
                            }
                            if (drop) { prunedLastLevel++; continue; }
                        }
                        if (visited.insert(nb).second) {
                            parent[nb] = {node, a.edgeId};
                            qnodes.push({nb, depth + 1});
                            enqueuedPerDepth[depth + 1]++;
                        }
                    }
                };

                if (t.direction == themis::AQLTranslator::TranslationResult::TraversalQuery::Direction::Outbound ||
                    t.direction == themis::AQLTranslator::TranslationResult::TraversalQuery::Direction::Any) {
                    auto [stAdj, adj] = graph_index_->outAdjacency(node);
                    if (!stAdj.ok) {
                        return makeErrorResponse(http::status::internal_server_error, std::string("Graph outAdjacency failed: ") + stAdj.message, req);
                    }
                    enqueueOut(adj);
                }
                if (t.direction == themis::AQLTranslator::TranslationResult::TraversalQuery::Direction::Inbound ||
                    t.direction == themis::AQLTranslator::TranslationResult::TraversalQuery::Direction::Any) {
                    auto [stAdjIn, adjIn] = graph_index_->inAdjacency(node);
                    if (!stAdjIn.ok) {
                        return makeErrorResponse(http::status::internal_server_error, std::string("Graph inAdjacency failed: ") + stAdjIn.message, req);
                    }
                    enqueueIn(adjIn);
                }
            }

            // Serialisierung je nach Return-Modus
            nlohmann::json res;
            res["table"] = "graph";
            res["entities"] = nlohmann::json::array();

            if (retMode == RetMode::Vertex) {
                res["count"] = static_cast<int>(resultVertices.size());
                for (const auto& pk : resultVertices) {
                    std::optional<std::vector<uint8_t>> blob = storage_->get(pk);
                    if (!blob && pk.find(':') == std::string::npos) {
                        blob = storage_->get(std::string("users:") + pk);
                    }
                    if (blob) {
                        try {
                            auto entity = themis::BaseEntity::deserialize(pk, *blob);
                            res["entities"].push_back(entity.toJson());
                        } catch (...) {
                            res["entities"].push_back(nlohmann::json({{"_key", pk}}));
                        }
                    } else {
                        res["entities"].push_back(nlohmann::json({{"_key", pk}}));
                    }
                }
            } else if (retMode == RetMode::Edge) {
                res["count"] = static_cast<int>(resultEdgeIds.size());
                for (const auto& eid : resultEdgeIds) {
                    auto eblob = storage_->get(themis::KeySchema::makeGraphEdgeKey(eid));
                    if (eblob) {
                        try {
                            auto edgeEnt = themis::BaseEntity::deserialize(eid, *eblob);
                            res["entities"].push_back(edgeEnt.toJson());
                        } catch (...) {
                            res["entities"].push_back(nlohmann::json({{"_edge", eid}}));
                        }
                    } else {
                        res["entities"].push_back(nlohmann::json({{"_edge", eid}}));
                    }
                }
            } else { // Path
                // Für jeden Terminalknoten Pfad rekonstruieren
                res["count"] = static_cast<int>(resultTerminalVertices.size());
                for (const auto& terminal : resultTerminalVertices) {
                    // Rekonstruiere Knotenfolge und Kanten entlang Elternzeiger
                    std::vector<std::string> vertices;
                    std::vector<std::string> edges;
                    std::string cur = terminal;
                    vertices.push_back(cur);
                    while (cur != t.startVertex) {
                        auto it = parent.find(cur);
                        if (it == parent.end()) break; // sollte bei Start aufhören
                        edges.push_back(it->second.edgeId);
                        cur = it->second.parent;
                        vertices.push_back(cur);
                    }
                    std::reverse(vertices.begin(), vertices.end());
                    std::reverse(edges.begin(), edges.end());

                    nlohmann::json jpath;
                    jpath["length"] = static_cast<int>(edges.size());
                    jpath["vertices"] = nlohmann::json::array();
                    jpath["edges"] = nlohmann::json::array();

                    // Lade Vertex-Entities
                    for (const auto& pk : vertices) {
                        std::optional<std::vector<uint8_t>> blob = storage_->get(pk);
                        if (!blob && pk.find(':') == std::string::npos) {
                            blob = storage_->get(std::string("users:") + pk);
                        }
                        if (blob) {
                            try {
                                auto ent = themis::BaseEntity::deserialize(pk, *blob);
                                jpath["vertices"].push_back(ent.toJson());
                            } catch (...) {
                                jpath["vertices"].push_back(nlohmann::json({{"_key", pk}}));
                            }
                        } else {
                            jpath["vertices"].push_back(nlohmann::json({{"_key", pk}}));
                        }
                    }

                    // Lade Edge-Entities
                    for (const auto& eid : edges) {
                        auto eblob = storage_->get(themis::KeySchema::makeGraphEdgeKey(eid));
                        if (eblob) {
                            try {
                                auto eent = themis::BaseEntity::deserialize(eid, *eblob);
                                jpath["edges"].push_back(eent.toJson());
                            } catch (...) {
                                jpath["edges"].push_back(nlohmann::json({{"_edge", eid}}));
                            }
                        } else {
                            jpath["edges"].push_back(nlohmann::json({{"_edge", eid}}));
                        }
                    }

                    res["entities"].push_back(std::move(jpath));
                }
            }
            // EXPLAIN/PROFILE: Traversal-Metriken anhängen
            if (explain) {
                nlohmann::json metrics;
                metrics["constant_filter_precheck"] = constantFilterPrechecked;
                metrics["edges_expanded"] = static_cast<int>(edgesExpanded);
                metrics["pruned_last_level"] = static_cast<int>(prunedLastLevel);
                metrics["filter_evaluations_total"] = static_cast<int>(filterEvaluationsTotal);
                metrics["filter_short_circuits"] = static_cast<int>(filterShortCircuits);
                metrics["max_frontier_size_reached"] = static_cast<int>(maxFrontierSizeReached);
                metrics["frontier_limit_hits"] = static_cast<int>(frontierLimitHits);
                metrics["result_limit_reached"] = resultLimitReached;
                nlohmann::json fp = nlohmann::json::object();
                for (const auto& kv : frontierProcessedPerDepth) fp[std::to_string(kv.first)] = kv.second;
                nlohmann::json eq = nlohmann::json::object();
                for (const auto& kv : enqueuedPerDepth) eq[std::to_string(kv.first)] = kv.second;
                metrics["frontier_processed_per_depth"] = std::move(fp);
                metrics["enqueued_per_depth"] = std::move(eq);
                res["metrics"] = std::move(metrics);
            }
            return makeResponse(http::status::ok, res.dump(), req);
        }

        // Relationale Query (mutable Kopie für Cursor-Anker/Limit-Anpassungen)
        auto q = translate_result.query;
        std::string table = q.table;

        // Cursor-Integration in die QueryEngine: falls ORDER BY vorhanden
        bool early_empty_due_to_cursor = false;
        size_t requested_count_for_cursor = 0;
        if (use_cursor && q.orderBy.has_value()) {
            // Bestimme angeforderte Seitengröße aus LIMIT
            if (parse_result.query && parse_result.query->limit) {
                requested_count_for_cursor = static_cast<size_t>(std::max<int64_t>(1, parse_result.query->limit->count));
            } else {
                requested_count_for_cursor = 1000;
            }
            // Engine soll count+1 liefern (extra Element für has_more)
            q.orderBy->limit = requested_count_for_cursor + 1;

            // Wenn ein Cursor-Token übergeben wurde, ermittle Anker (value, pk)
            if (!cursor_token.empty()) {
                auto decoded = themis::utils::Cursor::decode(cursor_token);
                if (!decoded.has_value()) {
                    // Ungültiger Cursor: leere Seite zurück
                    early_empty_due_to_cursor = true;
                } else {
                    auto [pk, collection] = *decoded;
                    if (collection != table) {
                        // Falsche Collection im Cursor
                        early_empty_due_to_cursor = true;
                    } else {
                        // Entität laden, um Sortierspaltenwert zu extrahieren
                        auto blob = storage_->get(table + ":" + pk);
                        if (!blob.has_value()) {
                            early_empty_due_to_cursor = true;
                        } else {
                            try {
                                auto entity = themis::BaseEntity::deserialize(pk, *blob);
                                // Sortierspalte extrahieren
                                const std::string sortCol = q.orderBy->column;
                                auto maybeVal = entity.extractField(sortCol);
                                if (maybeVal.has_value()) {
                                    q.orderBy->cursor_value = *maybeVal;
                                    q.orderBy->cursor_pk = pk;
                                } else {
                                    // Ohne Sortwert kein sicherer Anker
                                    early_empty_due_to_cursor = true;
                                }
                            } catch (...) {
                                early_empty_due_to_cursor = true;
                            }
                        }
                    }
                }
            }
        }
        
        // Execute query
        themis::QueryEngine engine(*storage_, *secondary_index_);
        
        std::string exec_mode;
        nlohmann::json plan_json;
        
        std::pair<themis::QueryEngine::Status, std::vector<themis::BaseEntity>> res;
        if (early_empty_due_to_cursor && use_cursor) {
            // Liefere leere Seite sofort zurück
            res = { themis::QueryEngine::Status::OK(), {} };
        } else {
        
        if (allow_full_scan) {
            exec_mode = "full_scan_fallback";
            res = engine.executeAndEntitiesWithFallback(q, optimize);
        } else {
            // Range-aware: Wenn Range-Prädikate oder ORDER BY vorhanden sind,
            // nutze direkt die range-fähige Engine-Logik (Optimizer unterstützt nur Gleichheit).
            if (!q.rangePredicates.empty() || q.orderBy.has_value()) {
                exec_mode = "index_rangeaware";
                res = engine.executeAndEntities(q);
            } else if (optimize) {
                themis::QueryOptimizer opt(*secondary_index_);
                auto plan = opt.chooseOrderForAndQuery(q);
                res = opt.executeOptimizedEntities(engine, q, plan);
                exec_mode = "index_optimized";
                
                if (explain) {
                    plan_json["mode"] = exec_mode;
                    plan_json["order"] = nlohmann::json::array();
                    for (const auto& p : plan.orderedPredicates) {
                        plan_json["order"].push_back({{"column", p.column}, {"value", p.value}});
                    }
                    plan_json["estimates"] = nlohmann::json::array();
                    for (const auto& d : plan.details) {
                        plan_json["estimates"].push_back({
                            {"column", d.pred.column}, {"value", d.pred.value},
                            {"estimatedCount", d.estimatedCount}, {"capped", d.capped}
                        });
                    }
                }
            } else {
                exec_mode = "index_parallel";
                res = engine.executeAndEntities(q);
                
                if (explain) {
                    plan_json = {
                        {"mode", exec_mode},
                        {"order", nlohmann::json::array()}
                    };
                    for (const auto& p : q.predicates) {
                        plan_json["order"].push_back({{"column", p.column}, {"value", p.value}});
                    }
                }
            }
        }
        }
        
        if (!res.first.ok) {
            return makeErrorResponse(http::status::bad_request, res.first.message, req);
        }
        
        // Apply LIMIT offset,count if provided in the AQL (post-fetch slicing)
        std::vector<themis::BaseEntity> sliced;
        sliced.reserve(res.second.size());
        sliced = std::move(res.second);

        if (!use_cursor && parse_result.query && parse_result.query->limit) {
            // Klassisches LIMIT offset,count Verhalten
            auto off = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->offset));
            auto cnt = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->count));
            if (off < sliced.size()) {
                size_t last = std::min(sliced.size(), off + cnt);
                std::vector<themis::BaseEntity> tmp;
                tmp.reserve(last - off);
                for (size_t i = off; i < last; ++i) tmp.emplace_back(std::move(sliced[i]));
                sliced.swap(tmp);
            } else {
                sliced.clear();
            }
        }

        // Enrich plan (for explain) with execution mode and cursor metadata
        if (explain) {
            if (plan_json.is_null()) plan_json = nlohmann::json::object();
            if (!exec_mode.empty()) plan_json["mode"] = exec_mode;
            if (use_cursor) {
                nlohmann::json cursor_meta = nlohmann::json::object();
                cursor_meta["used"] = true;
                cursor_meta["cursor_present"] = !cursor_token.empty();
                if (q.orderBy.has_value()) {
                    cursor_meta["sort_column"] = q.orderBy->column;
                    cursor_meta["effective_limit"] = static_cast<int>(q.orderBy->limit);
                    cursor_meta["anchor_set"] = q.orderBy->cursor_pk.has_value();
                }
                cursor_meta["requested_count"] = static_cast<int>(requested_count_for_cursor);
                plan_json["cursor"] = std::move(cursor_meta);
            }
        }

        // COLLECT/GROUP BY (MVP): falls vorhanden, führe In-Memory-Gruppierung/Aggregation über die Ergebnisse aus
        if (parse_result.query && parse_result.query->collect && !use_cursor) {
            const auto& collect = *parse_result.query->collect;
            using namespace themis::query;

            // Extrahiere aus einem FieldAccess-Ausdruck die Feld-Pfad-Notation (z.B. doc.city -> "city", doc.addr.city -> "addr.city")
            auto extractColumn = [&](const std::shared_ptr<themis::query::Expression>& expr)->std::string {
                auto* fa = dynamic_cast<FieldAccessExpr*>(expr.get());
                if (!fa) return std::string();
                std::vector<std::string> parts;
                parts.push_back(fa->field);
                auto* cur = fa->object.get();
                while (auto* fa2 = dynamic_cast<FieldAccessExpr*>(cur)) {
                    parts.push_back(fa2->field);
                    cur = fa2->object.get();
                }
                // Root erwartet Variable; deren Name wird ignoriert
                std::string col;
                for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
                    if (!col.empty()) col += ".";
                    col += *it;
                }
                return col;
            };

            // MVP: Unterstütze 0..1 Group-Variablen
            std::string groupVarName;
            std::string groupColumn;
            if (!collect.groups.empty()) {
                groupVarName = collect.groups[0].first;
                if (collect.groups[0].second) {
                    groupColumn = extractColumn(collect.groups[0].second);
                }
            }

            struct AggSpec { std::string var; std::string func; std::string col; };
            std::vector<AggSpec> aggs;
            aggs.reserve(collect.aggregations.size());
            for (const auto& a : collect.aggregations) {
                std::string func = a.funcName; std::transform(func.begin(), func.end(), func.begin(), ::tolower);
                std::string col;
                if (a.argument) col = extractColumn(a.argument);
                aggs.push_back({a.varName, func, col});
            }

            struct AggState { uint64_t cnt=0; double sum=0.0; double min=std::numeric_limits<double>::infinity(); double max=-std::numeric_limits<double>::infinity(); };
            std::unordered_map<std::string, std::unordered_map<std::string, AggState>> acc;

            auto toGroupKey = [&](const themis::BaseEntity& e)->std::string{
                if (groupColumn.empty()) return std::string("__all__");
                auto v = e.getFieldAsString(groupColumn);
                return v.value_or(std::string(""));
            };
            auto toNumber = [&](const themis::BaseEntity& e, const std::string& col, std::optional<double>& out)->bool{
                if (col.empty()) { out = 1.0; return true; }
                auto dv = e.getFieldAsDouble(col);
                if (dv.has_value()) { out = *dv; return true; }
                auto sv = e.getFieldAsString(col);
                if (sv.has_value()) {
                    try { out = std::stod(*sv); return true; } catch (...) { /* ignore */ }
                }
                return false;
            };

            for (const auto& e : sliced) {
                std::string key = toGroupKey(e);
                auto& bucket = acc[key];
                // Update je Aggregation
                if (aggs.empty()) {
                    bucket[std::string("count")].cnt += 1;
                } else {
                    for (const auto& a : aggs) {
                        auto& st = bucket[a.var];
                        if (a.func == "count") {
                            st.cnt += 1;
                        } else if (a.func == "sum" || a.func == "avg" || a.func == "min" || a.func == "max") {
                            std::optional<double> num;
                            if (toNumber(e, a.col, num) && num.has_value()) {
                                st.cnt += 1;
                                st.sum += *num;
                                if (*num < st.min) st.min = *num;
                                if (*num > st.max) st.max = *num;
                            }
                        }
                    }
                }
            }

            // Baue Ausgabe
            nlohmann::json groups = nlohmann::json::array();
            for (const auto& [k, mp] : acc) {
                nlohmann::json row = nlohmann::json::object();
                if (!groupVarName.empty()) row[groupVarName] = k;
                if (aggs.empty()) {
                    auto it = mp.find("count");
                    uint64_t c = (it != mp.end()) ? it->second.cnt : 0;
                    row["count"] = c;
                } else {
                    for (const auto& a : aggs) {
                        const auto it = mp.find(a.var);
                        if (it == mp.end()) continue;
                        const auto& st = it->second;
                        if (a.func == "count") row[a.var] = static_cast<uint64_t>(st.cnt);
                        else if (a.func == "sum") row[a.var] = st.sum;
                        else if (a.func == "avg") row[a.var] = (st.cnt ? (st.sum / static_cast<double>(st.cnt)) : 0.0);
                        else if (a.func == "min") row[a.var] = (st.cnt ? st.min : 0.0);
                        else if (a.func == "max") row[a.var] = (st.cnt ? st.max : 0.0);
                    }
                }
                groups.push_back(std::move(row));
            }

            nlohmann::json response_body = {
                {"table", table},
                {"count", groups.size()},
                {"groups", groups}
            };
            if (explain) {
                response_body["query"] = aql_query;
                response_body["ast"] = parse_result.query->toJSON();
                if (!plan_json.is_null()) response_body["plan"] = plan_json;
            }
            return makeResponse(http::status::ok, response_body.dump(), req);
        }

        // Serialize entities
        json entities = json::array();
        for (const auto& e : sliced) {
            entities.push_back(e.toJson());
        }
        
        json response_body;
        
        if (use_cursor) {
            // Cursor-basierte Antwort wurde nach Engine-Paginierung erstellt
            themis::utils::PaginatedResponse paged;
            // Bestimme angeforderte Seitengröße
            size_t requested_count = parse_result.query && parse_result.query->limit 
                ? static_cast<size_t>(std::max<int64_t>(1, parse_result.query->limit->count))
                : 1000;

            bool has_more = false;
            if (sliced.size() > requested_count) {
                has_more = true;
                // Trenne das +1 Element ab (nur für has_more Erkennung)
                sliced.resize(requested_count);
            }

            // Serialize final page
            json page_items = json::array();
            for (const auto& e : sliced) page_items.push_back(e.toJson());

            paged.items = std::move(page_items);
            paged.batch_size = sliced.size();
            paged.has_more = has_more;
            if (has_more && !sliced.empty()) {
                paged.next_cursor = themis::utils::Cursor::encode(sliced.back().getPrimaryKey(), table);
            }
            response_body = paged.toJSON();
        } else {
            // Traditional response format
            response_body = {
                {"table", table},
                {"count", sliced.size()},
                {"entities", entities}
            };
        }
        
        if (explain) {
            response_body["query"] = aql_query;
            response_body["ast"] = parse_result.query->toJSON();
            if (!plan_json.is_null()) {
                response_body["plan"] = plan_json;
            }
        }
        
        auto final_res = makeResponse(http::status::ok, response_body.dump(), req);
        // Record page fetch time histogram for cursor-based pagination
        if (use_cursor) {
            auto end = std::chrono::steady_clock::now();
            auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - page_fetch_start);
            recordPageFetch(dur_ms);
        }
        return final_res;
        
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleGraphTraverse(
    const http::request<http::string_body>& req
) {
    try {
        auto body_json = json::parse(req.body());
        
        if (!body_json.contains("start_vertex") || !body_json.contains("max_depth")) {
            return makeErrorResponse(http::status::bad_request,
                "Missing 'start_vertex' or 'max_depth'", req);
        }

        std::string start_vertex = body_json["start_vertex"];
        size_t max_depth = body_json["max_depth"];

        // Perform BFS traversal
        auto [status, visited] = graph_index_->bfs(start_vertex, static_cast<int>(max_depth));

        if (!status.ok) {
            return makeErrorResponse(http::status::internal_server_error,
                "Traversal failed", req);
        }

        json response = {
            {"start_vertex", start_vertex},
            {"max_depth", max_depth},
            {"visited_count", visited.size()},
            {"visited", visited}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Graph traverse error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorSearch(
    const http::request<http::string_body>& req
) {
    try {
        auto body_json = json::parse(req.body());
        
        // Validate required fields
        if (!body_json.contains("vector")) {
            return makeErrorResponse(http::status::bad_request,
                "Missing required field: vector", req);
        }
        
        // Parse query vector
        std::vector<float> queryVector;
        if (body_json["vector"].is_array()) {
            for (const auto& val : body_json["vector"]) {
                if (val.is_number()) {
                    queryVector.push_back(val.get<float>());
                } else {
                    return makeErrorResponse(http::status::bad_request,
                        "Vector elements must be numbers", req);
                }
            }
        } else {
            return makeErrorResponse(http::status::bad_request,
                "Field 'vector' must be an array", req);
        }
        
        // Parse k (default: 10)
        size_t k = body_json.value("k", 10);
        if (k == 0) {
            return makeErrorResponse(http::status::bad_request,
                "Field 'k' must be greater than 0", req);
        }
        
        // Validate dimension
        int expectedDim = vector_index_->getDimension();
        if (expectedDim > 0 && static_cast<int>(queryVector.size()) != expectedDim) {
            return makeErrorResponse(http::status::bad_request,
                "Vector dimension mismatch: expected " + std::to_string(expectedDim) +
                ", got " + std::to_string(queryVector.size()), req);
        }
        
        // Perform k-NN search
        auto [status, results] = vector_index_->searchKnn(queryVector, k);
        
        if (!status.ok) {
            return makeErrorResponse(http::status::internal_server_error,
                "Vector search failed: " + status.message, req);
        }
        
        // Format response
        json resultJson = json::array();
        for (const auto& result : results) {
            resultJson.push_back({
                {"pk", result.pk},
                {"distance", result.distance}
            });
        }
        
        json response = {
            {"results", resultJson},
            {"k", k},
            {"count", results.size()}
        };
        
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Vector search error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorIndexSave(
    const http::request<http::string_body>& req
) {
    try {
        auto body_json = json::parse(req.body());
        
        // Optional: directory parameter, default to "./data/vector_index"
        std::string directory = body_json.value("directory", "./data/vector_index");
        
        auto status = vector_index_->saveIndex(directory);
        
        if (!status.ok) {
            return makeErrorResponse(http::status::internal_server_error,
                "Failed to save index: " + status.message, req);
        }
        
        json response = {
            {"message", "Vector index saved successfully"},
            {"directory", directory}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Vector index save error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorIndexLoad(
    const http::request<http::string_body>& req
) {
    try {
        auto body_json = json::parse(req.body());
        
        // Required: directory parameter
        if (!body_json.contains("directory")) {
            return makeErrorResponse(http::status::bad_request,
                "Missing required field: directory", req);
        }
        
        std::string directory = body_json["directory"];
        
        auto status = vector_index_->loadIndex(directory);
        
        if (!status.ok) {
            return makeErrorResponse(http::status::internal_server_error,
                "Failed to load index: " + status.message, req);
        }
        
        json response = {
            {"message", "Vector index loaded successfully"},
            {"directory", directory}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Vector index load error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorIndexConfigGet(
    const http::request<http::string_body>& req
) {
    try {
        std::string metricStr = (vector_index_->getMetric() == themis::VectorIndexManager::Metric::L2) ? "L2" : "COSINE";
        
        json response = {
            {"objectName", vector_index_->getObjectName()},
            {"dimension", vector_index_->getDimension()},
            {"metric", metricStr},
            {"efSearch", vector_index_->getEfSearch()},
            {"M", vector_index_->getM()},
            {"efConstruction", vector_index_->getEfConstruction()},
            {"hnswEnabled", vector_index_->isHnswEnabled()}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const std::exception& e) {
        THEMIS_ERROR("Vector config get error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorIndexConfigPut(
    const http::request<http::string_body>& req
) {
    try {
        auto body_json = json::parse(req.body());
        
        // Hot-update efSearch
        if (body_json.contains("efSearch")) {
            int efSearch = body_json["efSearch"];
            if (efSearch < 1 || efSearch > 10000) {
                return makeErrorResponse(http::status::bad_request,
                    "efSearch must be between 1 and 10000", req);
            }
            
            auto status = vector_index_->setEfSearch(efSearch);
            if (!status.ok) {
                return makeErrorResponse(http::status::internal_server_error,
                    "Failed to set efSearch: " + status.message, req);
            }
        }
        
        json response = {
            {"message", "Vector index configuration updated"},
            {"updated_fields", body_json}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Vector config update error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorIndexStats(
    const http::request<http::string_body>& req
) {
    try {
        std::string metricStr = (vector_index_->getMetric() == themis::VectorIndexManager::Metric::L2) ? "L2" : "COSINE";
        
        json response = {
            {"objectName", vector_index_->getObjectName()},
            {"dimension", vector_index_->getDimension()},
            {"metric", metricStr},
            {"vectorCount", vector_index_->getVectorCount()},
            {"efSearch", vector_index_->getEfSearch()},
            {"M", vector_index_->getM()},
            {"efConstruction", vector_index_->getEfConstruction()},
            {"hnswEnabled", vector_index_->isHnswEnabled()}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const std::exception& e) {
        THEMIS_ERROR("Vector stats error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

// ===================== Admin: Backup & Restore =====================

http::response<http::string_body> HttpServer::handleAdminBackup(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        std::string dir = body.value("directory", std::string("./data/backup_") + std::to_string(std::time(nullptr)));
        bool ok = storage_->createCheckpoint(dir);
        if (!ok) {
            return makeErrorResponse(http::status::internal_server_error, std::string("Failed to create checkpoint at ") + dir, req);
        }
        json resp = {{"status", "ok"}, {"directory", dir}};
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("Invalid JSON: ") + e.what(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleAdminRestore(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        if (!body.contains("directory") || !body["directory"].is_string()) {
            return makeErrorResponse(http::status::bad_request, "Missing required field: directory", req);
        }
        std::string dir = body["directory"].get<std::string>();
        bool ok = storage_->restoreFromCheckpoint(dir);
        if (!ok) {
            return makeErrorResponse(http::status::internal_server_error, std::string("Failed to restore from checkpoint ") + dir, req);
        }
        json resp = {{"status", "ok"}, {"restored_from", dir}};
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("Invalid JSON: ") + e.what(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleTransaction(
    const http::request<http::string_body>& req
) {
    try {
        auto body_json = json::parse(req.body());
        
        // TODO: Implement transaction endpoint
        json response = {
            {"message", "Transaction endpoint not yet fully implemented"},
            {"request", body_json}
        };
        return makeResponse(http::status::not_implemented, response.dump(), req);

    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    }
}

// ===================== Content API =====================

http::response<http::string_body> HttpServer::handleContentImport(
    const http::request<http::string_body>& req
) {
    try {
        auto body = json::parse(req.body());
        
        // Extract optional blob (can be base64 or raw string)
        std::optional<std::string> blob;
        if (body.contains("blob")) {
            blob = body["blob"].get<std::string>();
        } else if (body.contains("blob_base64")) {
            // Simple base64 decode (minimal implementation - consider using proper library)
            blob = body["blob_base64"].get<std::string>(); // TODO: actual base64 decode
        }
        
        // Call ContentManager::importContent with structured JSON spec
        auto status = content_manager_->importContent(body, blob);
        
        if (!status.ok) {
            return makeErrorResponse(http::status::internal_server_error, status.message, req);
        }
        
        // Return success response with content_id from the spec
        json response_json = {{"status", "success"}};
        if (body.contains("content") && body["content"].contains("id")) {
            response_json["content_id"] = body["content"]["id"].get<std::string>();
        }
        
        return makeResponse(http::status::ok, response_json.dump(), req);
        
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("Invalid JSON: ") + e.what(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleGetContent(
    const http::request<http::string_body>& req
) {
    try {
        auto id = extractPathParam(std::string(req.target()), "/content/");
        if (id.empty()) return makeErrorResponse(http::status::bad_request, "Missing content id", req);
        auto meta = content_manager_->getContentMeta(id);
        if (!meta) return makeErrorResponse(http::status::not_found, "Content not found", req);
        return makeResponse(http::status::ok, meta->toJson().dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleGetContentBlob(
    const http::request<http::string_body>& req
) {
    try {
        auto path = std::string(req.target());
        // path format: /content/{id}/blob
        auto prefix = std::string("/content/");
        auto pos = path.find("/blob");
        if (pos == std::string::npos) return makeErrorResponse(http::status::bad_request, "Invalid path", req);
        auto id = path.substr(prefix.size(), pos - prefix.size());
        auto blob = content_manager_->getContentBlob(id);
        if (!blob) return makeErrorResponse(http::status::not_found, "Blob not found", req);
        auto meta = content_manager_->getContentMeta(id);
        std::string mime = (meta ? meta->mime_type : std::string("application/octet-stream"));

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "THEMIS/0.1.0");
        res.set(http::field::content_type, mime);
        res.keep_alive(req.keep_alive());
        res.body() = *blob; // may contain binary data
        res.prepare_payload();
        return res;
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleGetContentChunks(
    const http::request<http::string_body>& req
) {
    try {
        auto path = std::string(req.target());
        // path format: /content/{id}/chunks
        auto prefix = std::string("/content/");
        auto pos = path.find("/chunks");
        if (pos == std::string::npos) return makeErrorResponse(http::status::bad_request, "Invalid path", req);
        auto id = path.substr(prefix.size(), pos - prefix.size());
        auto chunks = content_manager_->getContentChunks(id);
        json arr = json::array();
        for (const auto& c : chunks) {
            json j = c.toJson();
            // For response size, omit full embedding by default
            if (j.contains("embedding")) j["embedding"] = json::array();
            arr.push_back(std::move(j));
        }
        json resp = { {"count", chunks.size()}, {"chunks", std::move(arr)} };
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleHybridSearch(
    const http::request<http::string_body>& req
) {
    try {
        if (!content_manager_) return makeErrorResponse(http::status::service_unavailable, "ContentManager not initialized", req);
        json body = json::parse(req.body());
        std::string query = body.value("query", "");
        int k = body.value("k", 10);
        int hops = 1;
        if (body.contains("expand") && body["expand"].is_object()) {
            hops = body["expand"].value("hops", 1);
        }
        json filters = json::object();
        if (body.contains("filters")) filters = body["filters"];
        if (body.contains("scoring")) filters["scoring"] = body["scoring"];

        auto results = content_manager_->searchWithExpansion(query, k, hops, filters);
        json resp = json::array();
        for (const auto& [pk, score] : results) {
            resp.push_back({{"pk", pk}, {"score", score}});
        }
        json out = {
            {"count", resp.size()},
            {"results", resp}
        };
        return makeResponse(http::status::ok, out.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("Hybrid search error: ") + e.what(), req);
    } catch (...) {
        return makeErrorResponse(http::status::bad_request, "Hybrid search error", req);
    }
}

http::response<http::string_body> HttpServer::handleContentFilterSchemaGet(
    const http::request<http::string_body>& req
) {
    try {
        auto v = storage_->get("config:content_filter_schema");
        json resp;
        if (v) {
            std::string s(v->begin(), v->end());
            resp = json::parse(s);
        } else {
            resp = json{{"field_map", json::object()}};
        }
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, std::string("config read error: ") + e.what(), req);
    } catch (...) {
        return makeErrorResponse(http::status::internal_server_error, "config read error", req);
    }
}

http::response<http::string_body> HttpServer::handleContentFilterSchemaPut(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        if (!body.is_object() || !body.contains("field_map") || !body["field_map"].is_object()) {
            return makeErrorResponse(http::status::bad_request, "Body must be { field_map: { key: path } }", req);
        }
        std::string s = body.dump();
        std::vector<uint8_t> bytes(s.begin(), s.end());
        bool ok = storage_->put("config:content_filter_schema", bytes);
        if (!ok) return makeErrorResponse(http::status::internal_server_error, "Failed to store filter schema", req);
        return makeResponse(http::status::ok, json{{"status","ok"}}.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("config write error: ") + e.what(), req);
    } catch (...) {
        return makeErrorResponse(http::status::bad_request, "config write error", req);
    }
}

http::response<http::string_body> HttpServer::handleEdgeWeightConfigGet(
    const http::request<http::string_body>& req
) {
    try {
        auto v = storage_->get("config:edge_weights");
        json resp;
        if (v) {
            std::string s(v->begin(), v->end());
            resp = json::parse(s);
        } else {
            resp = json{{"weights", json{{"parent", 1.0}, {"next", 1.0}, {"prev", 1.0}}}};
        }
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, std::string("config read error: ") + e.what(), req);
    } catch (...) {
        return makeErrorResponse(http::status::internal_server_error, "config read error", req);
    }
}

http::response<http::string_body> HttpServer::handleEdgeWeightConfigPut(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        if (!body.is_object() || !body.contains("weights") || !body["weights"].is_object()) {
            return makeErrorResponse(http::status::bad_request, "Body must be { weights: { parent: number, next: number, prev: number } }", req);
        }
        // Validate all values numeric
        for (auto it = body["weights"].begin(); it != body["weights"].end(); ++it) {
            if (!it.value().is_number()) {
                return makeErrorResponse(http::status::bad_request, "All weights must be numeric", req);
            }
        }
        std::string s = body.dump();
        std::vector<uint8_t> bytes(s.begin(), s.end());
        bool ok = storage_->put("config:edge_weights", bytes);
        if (!ok) return makeErrorResponse(http::status::internal_server_error, "Failed to store edge weights", req);
        return makeResponse(http::status::ok, json{{"status","ok"}}.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("config write error: ") + e.what(), req);
    } catch (...) {
        return makeErrorResponse(http::status::bad_request, "config write error", req);
    }
}

http::response<http::string_body> HttpServer::handleCreateIndex(
    const http::request<http::string_body>& req
) {
    try {
        auto body = json::parse(req.body());
        if (!body.contains("table")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'table'", req);
        }
        std::string table = body["table"].get<std::string>();
        bool unique = false;
        if (body.contains("unique")) {
            unique = body["unique"].get<bool>();
        }

        // Support range index creation via type = "range"
        if (body.contains("type")) {
            std::string type = body["type"].get<std::string>();
            if (type == "range") {
                if (!body.contains("column")) {
                    return makeErrorResponse(http::status::bad_request, "Missing 'column' for range index", req);
                }
                std::string column = body["column"].get<std::string>();
                auto st = secondary_index_->createRangeIndex(table, column);
                if (!st.ok) {
                    return makeErrorResponse(http::status::bad_request, st.message, req);
                }
                json resp = {{"success", true}, {"table", table}, {"column", column}, {"type", "range"}};
                return makeResponse(http::status::ok, resp.dump(), req);
            }
        }

        // Support single-column (column) and composite (columns)
        if (body.contains("columns")) {
            if (!body["columns"].is_array() || body["columns"].empty()) {
                return makeErrorResponse(http::status::bad_request, "'columns' must be a non-empty array of strings", req);
            }
            std::vector<std::string> columns;
            for (const auto& c : body["columns"]) {
                columns.push_back(c.get<std::string>());
            }
            auto st = secondary_index_->createCompositeIndex(table, columns, unique);
            if (!st.ok) {
                return makeErrorResponse(http::status::bad_request, st.message, req);
            }
            json resp = {{"success", true}, {"table", table}, {"columns", columns}, {"unique", unique}};
            return makeResponse(http::status::ok, resp.dump(), req);
        }
        
        if (!body.contains("column")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'column' or 'columns'", req);
        }
        std::string column = body["column"].get<std::string>();
        auto st = secondary_index_->createIndex(table, column, unique);
        if (!st.ok) {
            return makeErrorResponse(http::status::bad_request, st.message, req);
        }
        json resp = {{"success", true}, {"table", table}, {"column", column}, {"unique", unique}};
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleDropIndex(
    const http::request<http::string_body>& req
) {
    try {
        auto body = json::parse(req.body());
        if (!body.contains("table") || !body.contains("column")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'table' or 'column'", req);
        }
        std::string table = body["table"].get<std::string>();
        std::string column = body["column"].get<std::string>();

        // Optional type for dropping range indexes
        if (body.contains("type") && body["type"].is_string() && body["type"].get<std::string>() == "range") {
            auto st = secondary_index_->dropRangeIndex(table, column);
            if (!st.ok) {
                return makeErrorResponse(http::status::bad_request, st.message, req);
            }
            json resp = {{"success", true}, {"table", table}, {"column", column}, {"type", "range"}};
            return makeResponse(http::status::ok, resp.dump(), req);
        }

        auto st = secondary_index_->dropIndex(table, column);
        if (!st.ok) {
            return makeErrorResponse(http::status::bad_request, st.message, req);
        }
        json resp = {{"success", true}, {"table", table}, {"column", column}};
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::makeResponse(
    http::status status,
    const std::string& body,
    const http::request<http::string_body>& req
) {
    http::response<http::string_body> res{status, req.version()};
    res.set(http::field::server, "THEMIS/0.1.0");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
}

http::response<http::string_body> HttpServer::makeErrorResponse(
    http::status status,
    const std::string& message,
    const http::request<http::string_body>& req
) {
    // Increment error counter
    error_count_.fetch_add(1, std::memory_order_relaxed);
    
    json error_body = {
        {"error", true},
        {"message", message},
        {"status_code", static_cast<int>(status)}
    };
    return makeResponse(status, error_body.dump(), req);
}

void HttpServer::recordLatency(std::chrono::microseconds duration) {
    uint64_t us = static_cast<uint64_t>(duration.count());
    latency_sum_us_.fetch_add(us, std::memory_order_relaxed);
    // Cumulative buckets: each bucket counts all values <= its upper bound
    // Buckets: 100us, 500us, 1ms, 5ms, 10ms, 50ms, 100ms, 500ms, 1s, 5s, +Inf
    if (us <= 100) latency_bucket_100us_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 500) latency_bucket_500us_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 1000) latency_bucket_1ms_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 5000) latency_bucket_5ms_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 10000) latency_bucket_10ms_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 50000) latency_bucket_50ms_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 100000) latency_bucket_100ms_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 500000) latency_bucket_500ms_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 1000000) latency_bucket_1s_.fetch_add(1, std::memory_order_relaxed);
    if (us <= 5000000) latency_bucket_5s_.fetch_add(1, std::memory_order_relaxed);
    // +Inf bucket always increments (cumulative count of all observations)
    latency_bucket_inf_.fetch_add(1, std::memory_order_relaxed);
}

std::string HttpServer::extractPathParam(
    const std::string& path,
    const std::string& prefix
) {
    if (!(path.rfind(prefix, 0) == 0)) {
        return "";
    }
    auto param = path.substr(prefix.length());
    // Remove query string if present
    auto query_pos = param.find('?');
    if (query_pos != std::string::npos) {
        param = param.substr(0, query_pos);
    }
    return param;
}

// ============================================================================
// Session Implementation
// ============================================================================

HttpServer::Session::Session(tcp::socket socket, HttpServer* server)
    : socket_(std::move(socket))
    , server_(server)
{
}

void HttpServer::Session::start() {
    doRead();
}

void HttpServer::Session::doRead() {
    request_ = {};

    http::async_read(
        socket_,
        buffer_,
        request_,
        beast::bind_front_handler(&Session::onRead, shared_from_this())
    );
}

void HttpServer::Session::onRead(
    beast::error_code ec,
    std::size_t bytes_transferred
) {
    boost::ignore_unused(bytes_transferred);

    if (ec == http::error::end_of_stream) {
        // Client closed connection
        socket_.shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    if (ec) {
        THEMIS_ERROR("Read error: {}", ec.message());
        return;
    }

    // Process the request
    processRequest();
}

void HttpServer::Session::processRequest() {
    // Route request to appropriate handler
    response_ = server_->routeRequest(request_);
    
    // Send response
    doWrite();
}

void HttpServer::Session::doWrite() {
    bool close = response_.need_eof();
    http::async_write(
        socket_,
        response_,
        beast::bind_front_handler(
            &Session::onWrite,
            shared_from_this(),
            close
        )
    );
}

void HttpServer::Session::onWrite(
    bool close,
    beast::error_code ec,
    std::size_t bytes_transferred
) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        THEMIS_ERROR("Write error: {}", ec.message());
        return;
    }

    if (close) {
        // Close connection
        socket_.shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    // Read next request
    doRead();
}

http::response<http::string_body> HttpServer::handleIndexStats(
    const http::request<http::string_body>& req
) {
    try {
        std::string table;
        std::string column;

        // Try parsing JSON body first
        if (!req.body().empty()) {
            json body = json::parse(req.body());
            if (body.contains("table")) {
                table = body["table"];
            }
            if (body.contains("column")) {
                column = body["column"];
            }
        }

        // If no JSON, try query parameters from target
        if (table.empty()) {
            std::string target = std::string(req.target());
            size_t query_start = target.find('?');
            if (query_start != std::string::npos) {
                std::string query = target.substr(query_start + 1);
                // Simple query parser: table=X&column=Y
                size_t pos = 0;
                while (pos < query.size()) {
                    size_t eq = query.find('=', pos);
                    if (eq == std::string::npos) break;
                    size_t amp = query.find('&', eq);
                    if (amp == std::string::npos) amp = query.size();
                    
                    std::string key = query.substr(pos, eq - pos);
                    std::string value = query.substr(eq + 1, amp - eq - 1);
                    
                    if (key == "table") table = value;
                    else if (key == "column") column = value;
                    
                    pos = amp + 1;
                }
            }
        }

        if (table.empty()) {
            return makeErrorResponse(http::status::bad_request, "Missing 'table' parameter", req);
        }

        // If column specified, get single index stats
        if (!column.empty()) {
            auto stats = secondary_index_->getIndexStats(table, column);
            json resp = {
                {"type", stats.type},
                {"table", stats.table},
                {"column", stats.column},
                {"entry_count", stats.entry_count},
                {"estimated_size_bytes", stats.estimated_size_bytes},
                {"unique", stats.unique}
            };
            if (!stats.additional_info.empty()) {
                resp["additional_info"] = stats.additional_info;
            }
            return makeResponse(http::status::ok, resp.dump(), req);
        } else {
            // Get all index stats for table
            auto all_stats = secondary_index_->getAllIndexStats(table);
            json resp = json::array();
            for (const auto& stats : all_stats) {
                json stat_obj = {
                    {"type", stats.type},
                    {"table", stats.table},
                    {"column", stats.column},
                    {"entry_count", stats.entry_count},
                    {"estimated_size_bytes", stats.estimated_size_bytes},
                    {"unique", stats.unique}
                };
                if (!stats.additional_info.empty()) {
                    stat_obj["additional_info"] = stats.additional_info;
                }
                resp.push_back(stat_obj);
            }
            return makeResponse(http::status::ok, resp.dump(), req);
        }
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, "Error: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleIndexRebuild(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        
        if (!body.contains("table") || !body.contains("column")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'table' or 'column'", req);
        }

        std::string table = body["table"];
        std::string column = body["column"];

        // Rebuild the index
        secondary_index_->rebuildIndex(table, column);

        // Get updated stats
        auto stats = secondary_index_->getIndexStats(table, column);
        
        json resp = {
            {"success", true},
            {"table", table},
            {"column", column},
            {"entry_count", stats.entry_count},
            {"estimated_size_bytes", stats.estimated_size_bytes}
        };
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, "Error: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleIndexReindex(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        
        if (!body.contains("table")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'table'", req);
        }

        std::string table = body["table"];

        // Reindex the entire table
        secondary_index_->reindexTable(table);

        // Get all index stats
        auto all_stats = secondary_index_->getAllIndexStats(table);
        
        json resp = {
            {"success", true},
            {"table", table},
            {"indexes_rebuilt", all_stats.size()}
        };
        
        // Include stats for each index
        json stats_array = json::array();
        for (const auto& stats : all_stats) {
            stats_array.push_back({
                {"column", stats.column},
                {"type", stats.type},
                {"entry_count", stats.entry_count}
            });
        }
        resp["indexes"] = stats_array;
        
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, "Error: " + std::string(e.what()), req);
    }
}

// ===== Transaction Endpoints =====

http::response<http::string_body> HttpServer::handleTransactionBegin(
    const http::request<http::string_body>& req
) {
    try {
        // Parse optional isolation level from request body
        IsolationLevel isolation = IsolationLevel::ReadCommitted;
        
        if (!req.body().empty()) {
            json body = json::parse(req.body());
            if (body.contains("isolation")) {
                std::string isolation_str = body["isolation"];
                if (isolation_str == "snapshot") {
                    isolation = IsolationLevel::Snapshot;
                } else if (isolation_str != "read_committed") {
                    return makeErrorResponse(http::status::bad_request, 
                        "Invalid isolation level. Use 'read_committed' or 'snapshot'", req);
                }
            }
        }
        
        auto txn_id = tx_manager_->beginTransaction(isolation);
        
        json response = {
            {"transaction_id", txn_id},
            {"isolation", isolation == IsolationLevel::ReadCommitted ? "read_committed" : "snapshot"},
            {"status", "active"}
        };
        
        return makeResponse(http::status::ok, response.dump(2), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, "Error: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleTransactionCommit(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        
        if (!body.contains("transaction_id")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'transaction_id'", req);
        }
        
        TransactionManager::TransactionId txn_id = body["transaction_id"];
        
        auto status = tx_manager_->commitTransaction(txn_id);
        
        if (status.ok) {
            json response = {
                {"transaction_id", txn_id},
                {"status", "committed"},
                {"message", "Transaction committed successfully"}
            };
            return makeResponse(http::status::ok, response.dump(2), req);
        } else {
            json response = {
                {"transaction_id", txn_id},
                {"status", "failed"},
                {"error", status.message}
            };
            return makeResponse(http::status::internal_server_error, response.dump(2), req);
        }
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, "Error: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleTransactionRollback(
    const http::request<http::string_body>& req
) {
    try {
        json body = json::parse(req.body());
        
        if (!body.contains("transaction_id")) {
            return makeErrorResponse(http::status::bad_request, "Missing 'transaction_id'", req);
        }
        
        TransactionManager::TransactionId txn_id = body["transaction_id"];
        
        tx_manager_->rollbackTransaction(txn_id);
        
        json response = {
            {"transaction_id", txn_id},
            {"status", "rolled_back"},
            {"message", "Transaction rolled back successfully"}
        };
        
        return makeResponse(http::status::ok, response.dump(2), req);
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, "Error: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleTransactionStats(
    const http::request<http::string_body>& req
) {
    try {
        auto stats = tx_manager_->getStats();
        
        json response = {
            {"total_begun", stats.total_begun},
            {"total_committed", stats.total_committed},
            {"total_aborted", stats.total_aborted},
            {"active_count", stats.active_count},
            {"avg_duration_ms", stats.avg_duration_ms},
            {"max_duration_ms", stats.max_duration_ms},
            {"success_rate", stats.total_begun > 0 
                ? static_cast<double>(stats.total_committed) / stats.total_begun 
                : 0.0}
        };
        
        return makeResponse(http::status::ok, response.dump(2), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, "Error: " + std::string(e.what()), req);
    }
}

} // namespace server
} // namespace themis
