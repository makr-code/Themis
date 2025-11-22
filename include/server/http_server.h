#pragma once

// Windows compatibility
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <chrono>
#include <optional>

#include "content/content_manager.h"
#include "content/content_processor.h"
#include "cache/semantic_cache.h"
#include "server/sse_connection_manager.h"
#include "server/audit_api_handler.h"
#include "server/saga_api_handler.h"
#include "server/pii_api_handler.h"
#include "server/retention_api_handler.h"
#include "server/keys_api_handler.h"
#include "server/pki_api_handler.h"
#include "server/classification_api_handler.h"
#include "server/reports_api_handler.h"
#include "server/update_api_handler.h"
#include "server/rate_limiter.h"
#include "server/auth_middleware.h"
#include "server/policy_engine.h"
#include "server/ranger_adapter.h"
#include "utils/pii_pseudonymizer.h"
#include "utils/update_checker.h"
#include "security/encryption.h"
#include "utils/input_validator.h"

namespace themis {
// Forward declarations
class RocksDBWrapper;
class SecondaryIndexManager;
class GraphIndexManager;
class VectorIndexManager;
class TransactionManager;
class LLMInteractionStore;
class Changefeed;
class TSStore;
class ContinuousAggregateManager;
class AdaptiveIndexManager;
class PromptManager;

namespace index {
class SpatialIndexManager;
}

namespace server {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// HTTP request handler function type
using RequestHandler = std::function<http::response<http::string_body>(
    const http::request<http::string_body>&)>;

/**
 * @brief Async HTTP/REST API Server for THEMIS
 * 
 * Features:
 * - Thread pool for handling requests
 * - RESTful endpoints for CRUD, Query, Graph, Vector operations
 * - JSON request/response format
 * - Connection pooling and session management
 */
class HttpServer {
public:
    /**
     * @brief Server configuration
     */
    struct Config {
        std::string host = "0.0.0.0";
        uint16_t port = 8080;
        size_t num_threads = std::thread::hardware_concurrency();
        size_t max_request_size_mb = 10;
        uint32_t request_timeout_ms = 30000; // 30 seconds default
        // Feature flags
        bool feature_semantic_cache = false;
        bool feature_llm_store = false;
        bool feature_cdc = false;
        bool feature_timeseries = false;
    bool feature_pii_manager = false; // PII mappings persistence (RocksDB CF + API handler)
    bool feature_update_checker = false; // GitHub update checker subsystem
        // SSE/CDC streaming config
        uint32_t sse_max_events_per_second = 0; // 0 = unlimited; server-side rate limit per connection
        // API rate limits
        uint32_t audit_rate_limit_per_minute = 100; // 0 = unlimited
        
        // TLS/SSL Configuration
        bool enable_tls = false; // Enable HTTPS (TLS)
        std::string tls_cert_path; // Server certificate path (PEM format)
        std::string tls_key_path; // Private key path (PEM format)
        std::string tls_ca_cert_path; // CA certificate for mTLS client verification (optional)
        bool tls_require_client_cert = false; // Enforce mutual TLS (mTLS)
        std::string tls_min_version = "TLSv1.3"; // Minimum TLS version (TLSv1.2 or TLSv1.3)
        std::string tls_cipher_list; // OpenSSL cipher list (empty = secure defaults)
        
        Config() = default;
        Config(std::string h, uint16_t p, size_t threads = 0) 
            : host(std::move(h)), port(p) {
            if (threads > 0) num_threads = threads;
        }
    };

    /**
     * @brief Construct HTTP server with database access
     */
    HttpServer(
        const Config& config,
        std::shared_ptr<RocksDBWrapper> storage,
        std::shared_ptr<SecondaryIndexManager> secondary_index,
        std::shared_ptr<GraphIndexManager> graph_index,
        std::shared_ptr<VectorIndexManager> vector_index,
        std::shared_ptr<TransactionManager> tx_manager
    );

    ~HttpServer();

    /**
     * @brief Start the server (non-blocking)
     */
    void start();

    /**
     * @brief Stop the server and wait for all connections to close
     */
    void stop();

    /**
     * @brief Wait for server to finish (blocking)
     */
    void wait();

    /**
     * @brief Check if server is running
     */
    bool isRunning() const { return running_; }

    // Test helper: expose content manager metrics (nullable)
    const themis::content::ContentManager::Metrics* contentMetrics() const {
        return content_manager_ ? &content_manager_->getMetrics() : nullptr;
    }

private:
    // Session class for handling individual connections
    class Session : public std::enable_shared_from_this<Session> {
    public:
        Session(tcp::socket socket, HttpServer* server);
        void start();

    private:
        void doRead();
        void onRead(beast::error_code ec, std::size_t bytes_transferred);
        void processRequest();
        void doWrite();
        void onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred);

        tcp::socket socket_;
        HttpServer* server_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> request_;
        http::response<http::string_body> response_;
    };

    // SSL Session class for handling TLS connections
    class SslSession : public std::enable_shared_from_this<SslSession> {
    public:
        SslSession(tcp::socket socket, boost::asio::ssl::context& ssl_ctx, HttpServer* server);
        void start();

    private:
        void doHandshake();
        void onHandshake(beast::error_code ec);
        void doRead();
        void onRead(beast::error_code ec, std::size_t bytes_transferred);
        void processRequest();
        void doWrite();
        void onWrite(bool close, beast::error_code ec, std::size_t bytes_transferred);
        void doShutdown();

        beast::ssl_stream<tcp::socket> stream_;
        HttpServer* server_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> request_;
        http::response<http::string_body> response_;
    };

    // Request routing
    void setupRoutes();
    http::response<http::string_body> routeRequest(const http::request<http::string_body>& req);

    // Endpoint handlers
    http::response<http::string_body> handleHealthCheck(const http::request<http::string_body>& req);
    http::response<http::string_body> handleMetrics(const http::request<http::string_body>& req);
    http::response<http::string_body> handleMetricsJson(const http::request<http::string_body>& req);
    http::response<http::string_body> handleStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleConfig(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetEntity(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePutEntity(const http::request<http::string_body>& req);
    http::response<http::string_body> handleDeleteEntity(const http::request<http::string_body>& req);
    http::response<http::string_body> handleQuery(const http::request<http::string_body>& req);
    http::response<http::string_body> handleQueryAql(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGraphTraverse(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGraphEdgeCreate(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGraphEdgeDelete(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorSearch(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexSave(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexLoad(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexConfigGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexConfigPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorBatchInsert(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorDeleteByFilter(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransaction(const http::request<http::string_body>& req);
    http::response<http::string_body> handleCreateIndex(const http::request<http::string_body>& req);
    http::response<http::string_body> handleDropIndex(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexRebuild(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexReindex(const http::request<http::string_body>& req);
    
    // G5: Spatial Index Management
    http::response<http::string_body> handleSpatialIndexCreate(const http::request<http::string_body>& req);
    http::response<http::string_body> handleSpatialIndexRebuild(const http::request<http::string_body>& req);
    http::response<http::string_body> handleSpatialIndexStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleSpatialMetrics(const http::request<http::string_body>& req);

    // Admin: Backup & Restore
    http::response<http::string_body> handleAdminBackup(const http::request<http::string_body>& req);
    http::response<http::string_body> handleAdminRestore(const http::request<http::string_body>& req);

    // Content API endpoints
    http::response<http::string_body> handleContentImport(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetContent(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetContentBlob(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetContentChunks(const http::request<http::string_body>& req);
    http::response<http::string_body> handleHybridSearch(const http::request<http::string_body>& req);
    http::response<http::string_body> handleFusionSearch(const http::request<http::string_body>& req);
    http::response<http::string_body> handleFulltextSearch(const http::request<http::string_body>& req);
    http::response<http::string_body> handleContentFilterSchemaGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleContentFilterSchemaPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleContentConfigGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleContentConfigPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleEdgeWeightConfigGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleEdgeWeightConfigPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleEncryptionSchemaGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleEncryptionSchemaPut(const http::request<http::string_body>& req);
    // Capabilities (Core/Enterprise) endpoint
    http::response<http::string_body> handleCapabilities(const http::request<http::string_body>& req);

    // Sprint A beta endpoints (feature-flagged)
    http::response<http::string_body> handleCacheQuery(const http::request<http::string_body>& req);
    http::response<http::string_body> handleCachePut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleCacheStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleLlmInteractionPost(const http::request<http::string_body>& req);
    http::response<http::string_body> handleLlmInteractionList(const http::request<http::string_body>& req);
    http::response<http::string_body> handleLlmInteractionGet(const http::request<http::string_body>& req);
    // Prompt Template management
    http::response<http::string_body> handlePromptTemplatePost(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePromptTemplateList(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePromptTemplateGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePromptTemplatePut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleChangefeedGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleChangefeedStreamSse(const http::request<http::string_body>& req);
    // CDC admin endpoints
    http::response<http::string_body> handleChangefeedStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleChangefeedRetention(const http::request<http::string_body>& req);

    // Policies: Ranger import/export
    http::response<http::string_body> handlePoliciesImportRanger(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePoliciesExportRanger(const http::request<http::string_body>& req);
    
    // Sprint B: Time-Series endpoints
    http::response<http::string_body> handleTimeSeriesPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesQuery(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesAggregate(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesAggregatesPost(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesAggregatesGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesAggregatesDelete(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesConfigGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesConfigPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesRetentionPost(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesRetentionGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTimeSeriesRetentionDelete(const http::request<http::string_body>& req);
    
    // Sprint C: Adaptive Indexing endpoints
    http::response<http::string_body> handleIndexSuggestions(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexPatterns(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexRecordPattern(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexClearPatterns(const http::request<http::string_body>& req);
    
    // Transaction endpoints
    http::response<http::string_body> handleTransactionBegin(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransactionCommit(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransactionRollback(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransactionStats(const http::request<http::string_body>& req);
    
    // Audit API endpoints
    http::response<http::string_body> handleAuditQuery(const http::request<http::string_body>& req);
    http::response<http::string_body> handleAuditExportCsv(const http::request<http::string_body>& req);
    
    // SAGA API endpoints
    http::response<http::string_body> handleSagaListBatches(const http::request<http::string_body>& req);
    http::response<http::string_body> handleSagaBatchDetail(const http::request<http::string_body>& req);
    http::response<http::string_body> handleSagaVerifyBatch(const http::request<http::string_body>& req);
    http::response<http::string_body> handleSagaFlush(const http::request<http::string_body>& req);

    // PII API endpoints
    http::response<http::string_body> handlePiiListMappings(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePiiCreateMapping(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePiiGetByUuid(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePiiExportCsv(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePiiDeleteByUuid(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePiiRevealByUuid(const http::request<http::string_body>& req);

    // Retention API endpoints
    http::response<http::string_body> handleRetentionListPolicies(const http::request<http::string_body>& req);
    http::response<http::string_body> handleRetentionCreatePolicy(const http::request<http::string_body>& req);
    http::response<http::string_body> handleRetentionDeletePolicy(const http::request<http::string_body>& req);
    http::response<http::string_body> handleRetentionGetHistory(const http::request<http::string_body>& req);
    http::response<http::string_body> handleRetentionGetPolicyStats(const http::request<http::string_body>& req);

    // Keys API endpoints (Skeleton)
    http::response<http::string_body> handleKeysListKeys(const http::request<http::string_body>& req);
    http::response<http::string_body> handleKeysRotateKey(const http::request<http::string_body>& req);

    // PKI endpoints (sign/verify)
    http::response<http::string_body> handlePkiSign(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiVerify(const http::request<http::string_body>& req);
    
    // PKI HSM, TSA, eIDAS endpoints
    http::response<http::string_body> handlePkiHsmSign(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiHsmKeys(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiTimestamp(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiTimestampVerify(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiEidasSign(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiEidasVerify(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiCertificates(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiCertificate(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePkiStatus(const http::request<http::string_body>& req);

    // Classification API endpoints (Skeleton)
    http::response<http::string_body> handleClassificationListRules(const http::request<http::string_body>& req);
    http::response<http::string_body> handleClassificationTest(const http::request<http::string_body>& req);

    // Reports API endpoints (Skeleton)
    http::response<http::string_body> handleReportsCompliance(const http::request<http::string_body>& req);

    // Utility methods
    http::response<http::string_body> makeResponse(
        http::status status,
        const std::string& body,
        const http::request<http::string_body>& req
    );
    
    http::response<http::string_body> makeErrorResponse(
        http::status status,
        const std::string& message,
        const http::request<http::string_body>& req
    );

    // Apply governance-related headers (X-Themis-*) to all responses
    void applyGovernanceHeaders(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res
    );

    // Authorization helper: returns optional error response if unauthorized
    std::optional<http::response<http::string_body>> requireScope(
        const http::request<http::string_body>& req,
        std::string_view scope
    );

    // Combined scope + policy authorization; resource_path is e.g. request target path
    std::optional<http::response<http::string_body>> requireAccess(
        const http::request<http::string_body>& req,
        std::string_view required_scope,
        std::string_view action,
        std::string_view resource_path
    );

    // Extract authentication context (user_id and groups) from JWT token
    // Returns empty user_id and groups if auth is disabled or token is invalid
    struct AuthContext {
        std::string user_id;
        std::vector<std::string> groups;
    };
    AuthContext extractAuthContext(const http::request<http::string_body>& req) const;

    std::string extractPathParam(const std::string& path, const std::string& prefix);

    // Lazy initialization for PIIPseudonymizer
    void ensurePIIPseudonymizer();

    // Rate limiting helper for Audit endpoints
    std::optional<http::response<http::string_body>> enforceAuditRateLimit(
        const http::request<http::string_body>& req,
        std::string_view route_key);

    // Accept new connections
    void doAccept();
    void onAccept(beast::error_code ec, tcp::socket socket);

    Config config_;
    
    // Database components
    std::shared_ptr<RocksDBWrapper> storage_;
    std::shared_ptr<SecondaryIndexManager> secondary_index_;
    std::shared_ptr<GraphIndexManager> graph_index_;
    std::shared_ptr<VectorIndexManager> vector_index_;
    std::shared_ptr<TransactionManager> tx_manager_;
    
    // Spatial Index Manager (geo MVP)
    std::unique_ptr<index::SpatialIndexManager> spatial_index_;

    // Content Manager
    std::unique_ptr<themis::content::ContentManager> content_manager_;
    // Built-in processors
    std::unique_ptr<themis::content::TextProcessor> text_processor_;
    
    // Semantic Cache (Sprint A)
    std::unique_ptr<SemanticCache> semantic_cache_;
    rocksdb::ColumnFamilyHandle* cache_cf_handle_ = nullptr;
    
    // LLM Interaction Store (Sprint A)
    std::unique_ptr<LLMInteractionStore> llm_store_;
    rocksdb::ColumnFamilyHandle* llm_cf_handle_ = nullptr;
    // Prompt Manager for managing prompt templates (in-memory or RocksDB-backed)
    std::unique_ptr<themis::PromptManager> prompt_manager_;
    rocksdb::ColumnFamilyHandle* prompt_cf_handle_ = nullptr;
    
    // Changefeed (Sprint A CDC)
    std::shared_ptr<Changefeed> changefeed_; // shared_ptr for SSE manager
    rocksdb::ColumnFamilyHandle* cdc_cf_handle_ = nullptr;
    
    // SSE Connection Manager for Changefeed streaming
    std::unique_ptr<SseConnectionManager> sse_manager_;
    
    // Time-Series Store (Sprint B)
    std::unique_ptr<TSStore> timeseries_;
    rocksdb::ColumnFamilyHandle* ts_cf_handle_ = nullptr;
    std::unique_ptr<ContinuousAggregateManager> ts_agg_manager_;
    // Governance Policy Engine
    std::unique_ptr<themis::PolicyEngine> policy_engine_;
    std::unique_ptr<themis::server::RangerClient> ranger_client_;
    
    // Audit Logger
    std::shared_ptr<themis::utils::AuditLogger> audit_logger_;
    // Field encryption for PII mappings
    std::shared_ptr<themis::FieldEncryption> field_encryption_;
    // Key provider für hierarchische Schluesselverwaltung (DEK, Group-DEKs, Field-Keys)
    std::shared_ptr<themis::KeyProvider> key_provider_;
    // PII Pseudonymizer (for reveal/erase operations)
    std::shared_ptr<themis::utils::PIIPseudonymizer> pii_pseudonymizer_;
    std::mutex pii_init_mutex_; // For lazy initialization thread-safety
    
    // SAGA Logger
    std::shared_ptr<themis::utils::SAGALogger> saga_logger_;
    
    // Audit API Handler
    std::unique_ptr<themis::server::AuditApiHandler> audit_api_;
    
    // SAGA API Handler
    std::unique_ptr<themis::server::SAGAApiHandler> saga_api_;

    // PII API Handler
    std::unique_ptr<themis::server::PIIApiHandler> pii_api_;
    rocksdb::ColumnFamilyHandle* pii_cf_handle_ = nullptr;
    
    // Retention API Handler
    std::unique_ptr<themis::server::RetentionApiHandler> retention_api_;
    
    // Keys API Handler (Skeleton)
    std::unique_ptr<themis::server::KeysApiHandler> keys_api_;
    // PKI API Handler
    std::unique_ptr<themis::server::PkiApiHandler> pki_api_;
    
    // Classification API Handler (Skeleton)
    std::unique_ptr<themis::server::ClassificationApiHandler> classification_api_;
    
    // Reports API Handler (Skeleton)
    std::unique_ptr<themis::server::ReportsApiHandler> reports_api_;
    
    // Update API Handler
    std::unique_ptr<themis::server::UpdateApiHandler> update_api_;
    std::shared_ptr<themis::utils::UpdateChecker> update_checker_;
    
    // Adaptive Index Manager (Sprint C)
    std::unique_ptr<AdaptiveIndexManager> adaptive_index_;

    // Authorization middleware
    std::unique_ptr<themis::AuthMiddleware> auth_;
    
    // Rate Limiter for DoS protection
    std::unique_ptr<RateLimiter> rate_limiter_;

    // Input validation & sanitization
    std::unique_ptr<themis::utils::InputValidator> validator_;

    // Networking
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    std::unique_ptr<boost::asio::ssl::context> ssl_ctx_; // SSL context for TLS connections
    
    // Thread pool
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
    
    // Metrics
    std::atomic<uint64_t> request_count_{0};
    std::atomic<uint64_t> error_count_{0};
    std::chrono::steady_clock::time_point start_time_;

    // Audit rate limiting state
    struct RateState { uint64_t window_start_ms{0}; uint32_t count{0}; };
    std::mutex audit_rate_mutex_;
    std::unordered_map<std::string, RateState> audit_rate_buckets_;
    uint32_t audit_rate_limit_per_minute_{100};
    
    // Latency histogram buckets (in microseconds): 100us, 500us, 1ms, 5ms, 10ms, 50ms, 100ms, 500ms, 1s, 5s, 10s+
    std::atomic<uint64_t> latency_bucket_100us_{0};
    std::atomic<uint64_t> latency_bucket_500us_{0};
    std::atomic<uint64_t> latency_bucket_1ms_{0};
    std::atomic<uint64_t> latency_bucket_5ms_{0};
    std::atomic<uint64_t> latency_bucket_10ms_{0};
    std::atomic<uint64_t> latency_bucket_50ms_{0};
    std::atomic<uint64_t> latency_bucket_100ms_{0};
    std::atomic<uint64_t> latency_bucket_500ms_{0};
    std::atomic<uint64_t> latency_bucket_1s_{0};
    std::atomic<uint64_t> latency_bucket_5s_{0};
    std::atomic<uint64_t> latency_bucket_inf_{0};
    std::atomic<uint64_t> latency_sum_us_{0}; // Total latency in microseconds
    
    // Helper to record latency
    void recordLatency(std::chrono::microseconds duration);
    
    // Extract client IP from request
    std::string extractClientIP(const http::request<http::string_body>& req) const;
    
    // Check rate limit and return error response if exceeded
    std::optional<http::response<http::string_body>> checkRateLimit(
        const http::request<http::string_body>& req
    );

    // Preflight response for CORS (OPTIONS)
    http::response<http::string_body> makePreflightResponse(
        const http::request<http::string_body>& req
    );

    // CORS configuration (loaded from environment at startup)
    bool cors_allow_all_{false};
    bool cors_allow_credentials_{false};
    std::vector<std::string> cors_allowed_origins_{}; // exact match list
    std::string cors_allowed_methods_{"GET,POST,PUT,DELETE,OPTIONS"};
    std::string cors_allowed_headers_{"Authorization,Content-Type,X-Requested-With"};

    // Input validation: hard limit for request body
    size_t max_body_bytes_{10 * 1024 * 1024}; // 10 MB default

    // Page Fetch (Cursor) Histogram in Millisekunden: 1,5,10,25,50,100,250,500,1000,5000,+Inf
    std::atomic<uint64_t> page_bucket_1ms_{0};
    std::atomic<uint64_t> page_bucket_5ms_{0};
    std::atomic<uint64_t> page_bucket_10ms_{0};
    std::atomic<uint64_t> page_bucket_25ms_{0};
    std::atomic<uint64_t> page_bucket_50ms_{0};
    std::atomic<uint64_t> page_bucket_100ms_{0};
    std::atomic<uint64_t> page_bucket_250ms_{0};
    std::atomic<uint64_t> page_bucket_500ms_{0};
    std::atomic<uint64_t> page_bucket_1000ms_{0};
    std::atomic<uint64_t> page_bucket_5000ms_{0};
    std::atomic<uint64_t> page_bucket_inf_{0};
    std::atomic<uint64_t> page_sum_ms_{0};
    std::atomic<uint64_t> page_count_{0};
    
    void recordPageFetch(std::chrono::milliseconds duration_ms);
};

} // namespace server
} // namespace themis
