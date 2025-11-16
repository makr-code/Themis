// Ensure correct WinSock include order on Windows
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#endif

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
#include "utils/tracing.h"
#include "utils/cursor.h"
#include "utils/pii_detector.h"
#include "security/key_provider.h"
#include "security/mock_key_provider.h"
#include "security/pki_key_provider.h"
#include "security/encryption.h"
#include "utils/audit_logger.h"
#include "utils/pki_client.h"
#include "content/content_manager.h"
#include "content/content_processor.h"
// Fuer Graph-Kanten-Entities
#include "storage/key_schema.h"
// HKDF Helper fuer Feldschluessel-Ableitung beim Entschluesseln
#include "utils/hkdf_helper.h"

// Sprint A features - include BEFORE http_server.h to have complete types
#include "llm/llm_interaction_store.h"
#include "llm/prompt_manager.h"
#include "cdc/changefeed.h"
#include <algorithm>

// Sprint B features
#include "timeseries/timeseries.h"
#include "timeseries/tsstore.h"
#include "timeseries/continuous_agg.h"

// Sprint C features
#include "index/adaptive_index.h"

// Now include http_server.h which has forward declarations
#include "server/http_server.h"
#include "server/keys_api_handler.h"
#include "server/pki_api_handler.h"
#include "server/classification_api_handler.h"
#if !defined(_WIN32)
#include <time.h>
#endif

// Portable wrappers for tm <-> time_t conversions
static inline time_t portable_mkgmtime_impl(std::tm const* tmin) {
#ifdef _WIN32
    return _mkgmtime(const_cast<std::tm*>(tmin));
#else
    return timegm(const_cast<std::tm*>(tmin));
#endif
}
static inline void portable_gmtime_r_impl(const time_t* t, std::tm* out) {
#ifdef _WIN32
    gmtime_s(out, t);
#else
    gmtime_r(t, out);
#endif
}
#include "server/reports_api_handler.h"
#include "server/auth_middleware.h"
#include "server/ranger_adapter.h"
#include "server/pii_api_handler.h"

#include "query/query_engine.h"
#include "query/query_optimizer.h"
#include "query/aql_parser.h"
#include "query/aql_translator.h"

#include "security/signing.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <tuple>
#include <regex>
#include <queue>
#include <unordered_set>
#include <functional>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <limits>
#include <optional>
#include <cstdlib>
#include <filesystem>
#include <iostream>

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
    // Diagnostic: log raw getenv value for admin token to verify visibility
    try {
        const char* _adm = std::getenv("THEMIS_TOKEN_ADMIN");
        THEMIS_INFO("HttpServer ctor: getenv(THEMIS_TOKEN_ADMIN)='{}'", _adm ? _adm : "<null>");
    } catch (...) {}
    // Initialize ContentManager and register built-in processors
    // ContentManager wird nach Initialisierung von FieldEncryption erstellt (siehe weiter unten).
    // Hier zunächst nur Platzhalter (nullptr); tatsächliche Instanz folgt nach key_provider_/field_encryption_ Setup.
    // (Früher erstellt -> jetzt verschoben um Encryption sofort verfügbar zu machen.)
    // Defer ContentManager + Processor Registrierung bis FieldEncryption initialisiert ist.
    // (Vorheriger Zugriff auf content_manager_ entfernt – war null und verursachte Crash.)
    
    // Initialize Semantic Cache (Sprint A) if feature enabled
    if (config_.feature_semantic_cache) {
        // Use default column family for MVP (no dedicated CF needed)
        cache_cf_handle_ = nullptr;
        semantic_cache_ = std::make_unique<SemanticCache>(
            storage_->getRawDB(),
            cache_cf_handle_,
            3600 // default TTL: 1 hour
        );
        THEMIS_INFO("Semantic Cache initialized (TTL: 3600s) using default CF");
    }
    
    // Initialize LLM Interaction Store (Sprint A) if feature enabled
    if (config_.feature_llm_store) {
        llm_cf_handle_ = nullptr; // Use default CF
        llm_store_ = std::make_unique<LLMInteractionStore>(
            storage_->getRawDB(),
            llm_cf_handle_
        );
        THEMIS_INFO("LLM Interaction Store initialized using default CF");
    }
    
    // Initialize Changefeed (Sprint A CDC) if feature enabled
    if (config_.feature_cdc) {
        cdc_cf_handle_ = nullptr; // Use default CF
        // Changefeed constructor signature accepts (TransactionDB*, ColumnFamilyHandle*)
        // previous code attempted to pass Route identifiers which no longer apply.
        changefeed_ = std::make_shared<Changefeed>(
            storage_->getRawDB(),
            cdc_cf_handle_
        );
        THEMIS_INFO("Changefeed initialized using default CF");
        // Initialize SSE Connection Manager for streaming
        SseConnectionManager::ConnectionConfig sse_config;
        sse_config.heartbeat_interval_ms = 15000;
        sse_config.max_buffered_events = 1000;
        sse_config.event_poll_interval_ms = 500;
        sse_config.max_events_per_second = config_.sse_max_events_per_second; // from server config
        
        sse_manager_ = std::make_unique<SseConnectionManager>(
            changefeed_,
            ioc_,
            sse_config
        );
        THEMIS_INFO("SSE Connection Manager initialized");
    }

    // Initialize PII Mappings ColumnFamily + Handler (independent of CDC)
    if (config_.feature_pii_manager) {
        try {
            pii_cf_handle_ = storage_->getOrCreateColumnFamily("pii_mappings");
            pii_api_ = std::make_unique<PIIApiHandler>(storage_->getRawDB(), pii_cf_handle_);
            THEMIS_INFO("PII Manager initialized with dedicated CF 'pii_mappings'");
        } catch (const std::exception& ex) {
            THEMIS_ERROR("Failed to initialize PII Manager CF: {}", ex.what());
        }
    } else {
        // Fallback: use default CF (still functional, just no separation)
        pii_api_ = std::make_unique<PIIApiHandler>(storage_->getRawDB(), nullptr);
        THEMIS_INFO("PII Manager initialized using default CF (feature flag off, CF isolation disabled)");
    }

    // Initialize PromptManager (Prompt Template Registry)
    try {
        // Try to create a dedicated column family for prompt templates; fall back to default CF
        prompt_cf_handle_ = nullptr;
        if (storage_) {
            try {
                prompt_cf_handle_ = storage_->getOrCreateColumnFamily("prompt_templates");
                THEMIS_INFO("PromptManager: using dedicated CF 'prompt_templates'");
                prompt_manager_ = std::make_unique<themis::PromptManager>(storage_.get(), prompt_cf_handle_);
            } catch (const std::exception& ex) {
                THEMIS_WARN("PromptManager: failed to create dedicated CF, falling back to in-memory: {}", ex.what());
                prompt_manager_ = std::make_unique<themis::PromptManager>();
            }
        } else {
            // No storage available (tests / in-memory run)
            prompt_manager_ = std::make_unique<themis::PromptManager>();
            THEMIS_INFO("PromptManager initialized in-memory (no storage provided)");
        }
    } catch (const std::exception& ex) {
        THEMIS_ERROR("PromptManager initialization failure: {}", ex.what());
        prompt_manager_ = std::make_unique<themis::PromptManager>();
    }
    
    // Initialize Time-Series Store (Sprint B) if feature enabled
    if (config_.feature_timeseries) {
        ts_cf_handle_ = nullptr; // Use default CF
        timeseries_ = std::make_unique<TSStore>(
            storage_->getRawDB(),
            ts_cf_handle_
        );
        THEMIS_INFO("Time-Series Store initialized using default CF");
    }
    
    // Initialize Adaptive Index Manager (Sprint C) - always enabled
    adaptive_index_ = std::make_unique<AdaptiveIndexManager>(storage_->getRawDB());
    THEMIS_INFO("Adaptive Index Manager initialized");

    // Initialize Authorization middleware (MVP: tokens via env)
    auth_ = std::make_unique<themis::AuthMiddleware>();
    auto get_env = [](const char* name) -> std::optional<std::string> {
        const char* v = std::getenv(name);
        if (v && *v) return std::string(v);
        return std::nullopt;
    };
    // Admin token
    if (auto t = get_env("THEMIS_TOKEN_ADMIN")) {
        themis::AuthMiddleware::TokenConfig cfg;
        cfg.token = *t;
        cfg.user_id = "admin";
        cfg.scopes = {
            "admin","config:read","config:write","cdc:read","cdc:admin",
            "metrics:read","data:read","data:write","audit:read",
            // PII feature scopes
            "pii:read","pii:write","pii:reveal"
        };
        auth_->addToken(cfg);
        THEMIS_INFO("Auth: ADMIN token configured via env");
        try {
            auto v = auth_->validateToken(cfg.token);
            THEMIS_INFO("Auth check after addToken: validateToken(token='{}') -> authorized={} user_id='{}' reason='{}'",
                       cfg.token.size() > 8 ? (std::string(cfg.token).substr(0,4) + "..." + std::string(cfg.token).substr(cfg.token.size()-4)) : cfg.token,
                       v.authorized, v.user_id, v.reason);
        } catch(...) {}
    }
    // Read-only token
    if (auto t = get_env("THEMIS_TOKEN_READONLY")) {
        themis::AuthMiddleware::TokenConfig cfg;
        cfg.token = *t;
        cfg.user_id = "readonly";
    cfg.scopes = {"metrics:read","config:read","data:read","cdc:read","audit:read","pii:read"};
        auth_->addToken(cfg);
        THEMIS_INFO("Auth: READONLY token configured via env");
    }
    // Analyst token (read access incl. vectors/query)
    if (auto t = get_env("THEMIS_TOKEN_ANALYST")) {
        themis::AuthMiddleware::TokenConfig cfg;
        cfg.token = *t;
        cfg.user_id = "analyst";
    cfg.scopes = {"metrics:read","data:read","cdc:read","pii:read"};
        auth_->addToken(cfg);
        THEMIS_INFO("Auth: ANALYST token configured via env");
    }

    // Initialize security components  
    key_provider_ = std::make_shared<themis::security::PKIKeyProvider>(
        std::make_shared<themis::utils::VCCPKIClient>(themis::utils::PKIConfig{}),
        storage_,
        "themisdb"
    );
    THEMIS_INFO("PKIKeyProvider initialized with persistent KEK/DEK");

    // Field encryption for PII and schema-based encryption
    field_encryption_ = std::make_shared<themis::FieldEncryption>(key_provider_);
    THEMIS_INFO("FieldEncryption initialized");

    // Initialize ContentManager and register built-in processors (now with encryption)
    try {
        content_manager_ = std::make_unique<themis::content::ContentManager>(
            storage_, vector_index_, graph_index_, secondary_index_, field_encryption_);
        text_processor_ = std::make_unique<themis::content::TextProcessor>();
        content_manager_->registerProcessor(std::unique_ptr<themis::content::IContentProcessor>(text_processor_.release()));
        // Provide FieldEncryption to GraphIndexManager so edges can be encrypted on write
        if (graph_index_) {
            graph_index_->setFieldEncryption(field_encryption_);
        }
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to init ContentManager: {}", e.what());
    }

    // Initialize Keys API Handler with KeyProvider
    keys_api_ = std::make_unique<themis::server::KeysApiHandler>(key_provider_);
    THEMIS_INFO("Keys API Handler initialized");
    // Initialize PKI API Handler using a SigningService backed by the KeyProvider
    try {
        pki_api_ = std::make_unique<themis::server::PkiApiHandler>(themis::createKeyProviderSigningService(key_provider_));
        THEMIS_INFO("PKI API Handler initialized");
    } catch (const std::exception& e) {
        THEMIS_WARN("Failed to initialize PKI API Handler: {}", e.what());
    }
    
    // Initialize PII Detector for Classification API (simplified - no PKI for now)
    auto pii_detector = std::make_shared<themis::utils::PIIDetector>();
    THEMIS_INFO("PII Detector initialized");
    
    // Initialize Classification API Handler with PIIDetector
    classification_api_ = std::make_unique<themis::server::ClassificationApiHandler>(pii_detector);
    THEMIS_INFO("Classification API Handler initialized");

    // Initialize Audit Logger and Audit API Handler
    try {
        // Optional: override audit rate limit via env var for tests
        if (const char* lim = std::getenv("THEMIS_AUDIT_RATE_LIMIT")) {
            try { audit_rate_limit_per_minute_ = static_cast<uint32_t>(std::stoul(lim)); } catch (...) {}
        } else {
            audit_rate_limit_per_minute_ = config_.audit_rate_limit_per_minute;
        }
        THEMIS_INFO("Audit rate limit per minute set to {}", audit_rate_limit_per_minute_);
        themis::utils::PKIConfig pki_cfg;
        pki_cfg.service_id = "themisdb";
        // Optional: allow configuring PKI certificate/key via env for real signing
        auto getenv_opt = [](const char* n) -> std::optional<std::string> {
            const char* v = std::getenv(n);
            if (v && *v) return std::string(v);
            return std::nullopt;
        };
        if (auto v = getenv_opt("THEMIS_PKI_ENDPOINT")) pki_cfg.endpoint = *v;
        if (auto v = getenv_opt("THEMIS_PKI_CERT")) pki_cfg.cert_path = *v;
        if (auto v = getenv_opt("THEMIS_PKI_KEY")) pki_cfg.key_path = *v;
        if (auto v = getenv_opt("THEMIS_PKI_KEY_PASSPHRASE")) pki_cfg.key_passphrase = *v;
        if (auto v = getenv_opt("THEMIS_PKI_SIG_ALG")) pki_cfg.signature_algorithm = *v;

        auto pki_client = std::make_shared<themis::utils::VCCPKIClient>(pki_cfg);

        // Minimal AuditLogger setup
        themis::utils::AuditLoggerConfig audit_cfg;
        audit_cfg.log_path = "data/logs/audit.jsonl";
        audit_cfg.enabled = true;
        audit_logger_ = std::make_shared<themis::utils::AuditLogger>(
            field_encryption_, pki_client, audit_cfg);
        THEMIS_INFO("Audit Logger initialized (path: {})", audit_cfg.log_path);

        // Audit API Handler reads/decrypts/filters audit logs
        audit_api_ = std::make_unique<themis::server::AuditApiHandler>(
            field_encryption_, pki_client, audit_cfg.log_path);
        THEMIS_INFO("Audit API Handler initialized");
    } catch (const std::exception& e) {
        THEMIS_WARN("Failed to initialize Audit components: {}", e.what());
    }

    // Initialize PII Pseudonymizer (used for reveal/erase)
    // DEFERRED: Initialize on first use to avoid deadlock during server construction
    // try {
    //     pii_pseudonymizer_ = std::make_shared<themis::utils::PIIPseudonymizer>(
    //         storage_, field_encryption_, std::make_shared<themis::utils::PIIDetector>(), audit_logger_
    //     );
    //     THEMIS_INFO("PII Pseudonymizer initialized");
    // } catch (const std::exception& e) {
    //     THEMIS_WARN("Failed to initialize PII Pseudonymizer: {}", e.what());
    // }
    
    // Initialize Reports API Handler
    reports_api_ = std::make_unique<themis::server::ReportsApiHandler>();
    THEMIS_INFO("Reports API Handler initialized");

    // Initialize Policy Engine (Governance)
    policy_engine_ = std::make_unique<themis::PolicyEngine>();
    try {
        // Allow overriding the policies path via env `THEMIS_POLICIES_PATH` (useful for tests)
        std::vector<std::filesystem::path> candidates;
        if (const char* envp = std::getenv("THEMIS_POLICIES_PATH")) {
            std::filesystem::path p(envp);
            candidates.push_back(p);
            // If the provided path does not exist, try resolving it relative to repository root
            if (!std::filesystem::exists(p)) {
                // Walk up from current path to find a marker (CMakeLists.txt) to locate repo root
                auto cur = std::filesystem::current_path();
                for (auto up = cur; ; up = up.parent_path()) {
                    if (up == up.parent_path()) break; // reached filesystem root
                    if (std::filesystem::exists(up / "CMakeLists.txt") || std::filesystem::exists(up / ".git")) {
                        std::filesystem::path candidate = up / envp;
                        if (std::filesystem::exists(candidate)) {
                            candidates[0] = candidate;
                            THEMIS_INFO("PolicyEngine: resolved THEMIS_POLICIES_PATH relative to repo root: {}", candidate.string());
                            break;
                        }
                    }
                }
            } else {
                THEMIS_INFO("PolicyEngine: using policies override from THEMIS_POLICIES_PATH={}", p.string());
            }
        } else {
            // Try YAML first, then JSON in default config directory
            candidates = {
                std::filesystem::path("config") / "policies.yaml",
                std::filesystem::path("config") / "policies.yml",
                std::filesystem::path("config") / "policies.json"
            };
        }
        bool loaded_any = false;
        for (const auto& policies_path : candidates) {
            if (std::filesystem::exists(policies_path)) {
                std::string err;
                if (policy_engine_->loadFromFile(policies_path.string(), &err)) {
                    THEMIS_INFO("PolicyEngine: loaded policies from {}", policies_path.string());
                    loaded_any = true;
                    break;
                } else {
                    THEMIS_WARN("PolicyEngine: failed to load {}: {}", policies_path.string(), err);
                }
            }
        }
        if (!loaded_any) {
            THEMIS_INFO("PolicyEngine: no policies file found (config/policies.yaml|yml|json), default allow when empty");
        }
    } catch (const std::exception& e) {
        THEMIS_WARN("PolicyEngine initialization warning: {}", e.what());
    }

    // Initialize Ranger client (optional), configured via environment for secrets
    // Reuse the earlier `get_env` lambda defined above (tokens); avoid duplicate definition.
    if (auto base = get_env("THEMIS_RANGER_BASE_URL")) {
        themis::server::RangerClientConfig rcfg;
        rcfg.base_url = *base;
        rcfg.policies_path = std::getenv("THEMIS_RANGER_POLICIES_PATH") ? std::getenv("THEMIS_RANGER_POLICIES_PATH") : "/service/public/v2/api/policy";
        rcfg.service_name = std::getenv("THEMIS_RANGER_SERVICE") ? std::getenv("THEMIS_RANGER_SERVICE") : "themisdb";
        rcfg.bearer_token = std::getenv("THEMIS_RANGER_BEARER") ? std::getenv("THEMIS_RANGER_BEARER") : "";
        rcfg.tls_verify = true;
        if (auto tlsv = get_env("THEMIS_RANGER_TLS_VERIFY")) {
            if (*tlsv == "0" || *tlsv == "false" || *tlsv == "False") rcfg.tls_verify = false;
        }
        if (auto ca = get_env("THEMIS_RANGER_CA_CERT")) rcfg.ca_cert_path = *ca;
        if (auto cc = get_env("THEMIS_RANGER_CLIENT_CERT")) rcfg.client_cert_path = *cc;
        if (auto ck = get_env("THEMIS_RANGER_CLIENT_KEY")) rcfg.client_key_path = *ck;
        // Optional timeouts & retry configuration
        if (auto ct = get_env("THEMIS_RANGER_CONNECT_TIMEOUT_MS")) {
            try { rcfg.connect_timeout_ms = std::stol(*ct); } catch (...) {}
        }
        if (auto rt = get_env("THEMIS_RANGER_REQUEST_TIMEOUT_MS")) {
            try { rcfg.request_timeout_ms = std::stol(*rt); } catch (...) {}
        }
        if (auto mr = get_env("THEMIS_RANGER_MAX_RETRIES")) {
            try { rcfg.max_retries = std::stoi(*mr); } catch (...) {}
        }
        if (auto rb = get_env("THEMIS_RANGER_RETRY_BACKOFF_MS")) {
            try { rcfg.retry_backoff_ms = std::stol(*rb); } catch (...) {}
        }
        try {
            ranger_client_ = std::make_unique<themis::server::RangerClient>(std::move(rcfg));
            THEMIS_INFO("Ranger client configured for {}", *base);
        } catch (...) {
            THEMIS_WARN("Failed to initialize Ranger client; integration disabled");
        }
    }
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
    THEMIS_INFO("Initiating graceful shutdown...");
    running_ = false;

    // Stop accepting new connections
    beast::error_code ec;
    acceptor_.close(ec);

    // Give active requests time to complete
    THEMIS_INFO("Waiting for active requests to complete...");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Flush and cleanup Sprint A/B features
    if (semantic_cache_) {
        THEMIS_INFO("Flushing Semantic Cache...");
        // Cache is in RocksDB, will be flushed with DB
    }
    
    if (llm_store_) {
        THEMIS_INFO("Flushing LLM Interaction Store...");
        // Store is in RocksDB, will be flushed with DB
    }
    
    if (changefeed_) {
        THEMIS_INFO("Flushing Changefeed...");
        // Changefeed is in RocksDB, will be flushed with DB
    }
    
    if (timeseries_) {
        THEMIS_INFO("Flushing Time-Series Store...");
        // Time-series is in RocksDB, will be flushed with DB
    }
    
    // Vector index auto-save
    if (vector_index_) {
        THEMIS_INFO("Saving vector index (if auto-save enabled)...");
        vector_index_->shutdown();
    }
    
    // Flush RocksDB WAL and memtables
    if (storage_) {
        THEMIS_INFO("Flushing RocksDB memtables...");
        // RocksDB will flush on close, but we can trigger it explicitly
        storage_->close(); // This flushes and closes cleanly
        THEMIS_INFO("RocksDB closed cleanly");
    }

    // Stop io_context
    ioc_.stop();

    // Wait for all threads
    THEMIS_INFO("Waiting for worker threads to finish...");
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();

    THEMIS_INFO("HTTP Server stopped gracefully");
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
        CapabilitiesGet,
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
    GraphEdgePost,
    GraphEdgeDelete,
        VectorSearchPost,
    VectorBatchInsertPost,
    VectorDeleteByFilterDelete,
        // Beta endpoints
        CacheQueryPost,
        CachePutPost,
        CacheStatsGet,
    // Prompt Template endpoints
    PromptTemplatePost,
    PromptTemplateList,
    PromptTemplateGet,
    PromptTemplatePut,
        LlmInteractionPost,
        LlmInteractionGetList,
        LlmInteractionGetById,
        ChangefeedGet,
        ChangefeedStreamSse,
    ChangefeedStatsGet,
    ChangefeedRetentionPost,
        // Sprint B
        TimeSeriesPut,
        TimeSeriesQuery,
        TimeSeriesAggregate,
        TimeSeriesConfigGet,
        TimeSeriesConfigPut,
    // Additional TimeSeries endpoints (list aggregates/retention)
    TimeSeriesAggregatesGet,
    TimeSeriesRetentionGet,
        // Sprint C
        IndexSuggestionsGet,
        IndexPatternsGet,
        IndexRecordPatternPost,
        IndexClearPatternsDelete,
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
        FusionSearchPost,
        FulltextSearchPost,
        ContentFilterSchemaGet,
        ContentFilterSchemaPut,
        ContentConfigGet,
        ContentConfigPut,
        EdgeWeightConfigGet,
        EdgeWeightConfigPut,
        // Encryption Schema Management
        EncryptionSchemaGet,
        EncryptionSchemaPut,
        // Keys / Classification / Reports
    PkiSignPost,
    PkiVerifyPost,
        KeysListGet,
        KeysRotatePost,
        ClassificationRulesGet,
        ClassificationTestPost,
        ReportsComplianceGet,
        PoliciesImportRangerPost,
        PoliciesExportRangerGet,
        // PII
    PiiListGet,
    PiiPost,
    PiiGetByUuid,
    PiiExportCsvGet,
    PiiRevealGet,
    PiiDeleteDelete,
       // Audit API
       AuditQueryGet,
       AuditExportCsvGet,
        NotFound
    };

    Route classifyRoute(const http::request<http::string_body>& req) {
        const auto method = req.method();
        const std::string target = std::string(req.target());
        // Normalize path by stripping query string to allow matching endpoints with params
        std::string path_only = target;
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);

        if (target == "/" || target == "/health") return Route::Health;
    if (target == "/stats" && method == http::verb::get) return Route::Stats;
    if (target == "/api/capabilities" && method == http::verb::get) return Route::CapabilitiesGet;
    if (target == "/metrics" && method == http::verb::get) return Route::Metrics;
    if (target == "/config" && (method == http::verb::get || method == http::verb::post)) return Route::Config;
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
    // Backward compatibility alias
    if (target == "/api/aql" && method == http::verb::post) return Route::QueryAqlPost;
        if (target == "/index/create" && method == http::verb::post) return Route::IndexCreatePost;
        if (target == "/index/drop" && method == http::verb::post) return Route::IndexDropPost;
        if (target == "/index/stats" && method == http::verb::get) return Route::IndexStatsGet;
        if (target == "/index/rebuild" && method == http::verb::post) return Route::IndexRebuildPost;
        if (target == "/index/reindex" && method == http::verb::post) return Route::IndexReindexPost;
        if (target == "/graph/traverse" && method == http::verb::post) return Route::GraphTraversePost;
    if (target == "/graph/edge" && method == http::verb::post) return Route::GraphEdgePost;
    if (target.rfind("/graph/edge/", 0) == 0 && method == http::verb::delete_) return Route::GraphEdgeDelete;
        if (target == "/vector/search" && method == http::verb::post) return Route::VectorSearchPost;
    if (target == "/vector/batch_insert" && method == http::verb::post) return Route::VectorBatchInsertPost;
    if (target == "/vector/by-filter" && method == http::verb::delete_) return Route::VectorDeleteByFilterDelete;
        // Sprint A beta endpoints
        if (target == "/cache/query" && method == http::verb::post) return Route::CacheQueryPost;
        if (target == "/cache/put" && method == http::verb::post) return Route::CachePutPost;
        if (target == "/cache/stats" && method == http::verb::get) return Route::CacheStatsGet;
    if (target == "/prompt_template" && method == http::verb::post) return Route::PromptTemplatePost;
    if (target == "/prompt_template" && method == http::verb::get) return Route::PromptTemplateList;
    if (target.rfind("/prompt_template/", 0) == 0 && method == http::verb::get) return Route::PromptTemplateGet;
    if (target.rfind("/prompt_template/", 0) == 0 && method == http::verb::put) return Route::PromptTemplatePut;
        if (target == "/llm/interaction" && method == http::verb::post) return Route::LlmInteractionPost;
    if (target == "/llm/interaction" && method == http::verb::get) return Route::LlmInteractionGetList;
    if (target.rfind("/llm/interaction/", 0) == 0 && method == http::verb::get) return Route::LlmInteractionGetById;
    // Changefeed endpoint should match even with query parameters
    if (path_only == "/changefeed" && method == http::verb::get) return Route::ChangefeedGet;
    if (path_only == "/changefeed/stream" && method == http::verb::get) return Route::ChangefeedStreamSse;
    if (path_only == "/changefeed/stats" && method == http::verb::get) return Route::ChangefeedStatsGet;
    if (path_only == "/changefeed/retention" && method == http::verb::post) return Route::ChangefeedRetentionPost;
        // Sprint B endpoints
    if (target == "/ts/put" && method == http::verb::post) return Route::TimeSeriesPut;
    if (target == "/ts/query" && method == http::verb::post) return Route::TimeSeriesQuery;
    if (target == "/ts/aggregate" && method == http::verb::post) return Route::TimeSeriesAggregate;
    if (target == "/ts/config" && method == http::verb::get) return Route::TimeSeriesConfigGet;
    if (target == "/ts/config" && method == http::verb::put) return Route::TimeSeriesConfigPut;
    if (path_only == "/ts/aggregates" && method == http::verb::get) return Route::TimeSeriesAggregatesGet;
    if (path_only == "/ts/retention" && method == http::verb::get) return Route::TimeSeriesRetentionGet;
        // Sprint C endpoints
        if (target.find("/index/suggestions") == 0 && method == http::verb::get) return Route::IndexSuggestionsGet;
        if (target.find("/index/patterns") == 0 && method == http::verb::get) return Route::IndexPatternsGet;
        if (target == "/index/record-pattern" && method == http::verb::post) return Route::IndexRecordPatternPost;
        if (target == "/index/patterns" && method == http::verb::delete_) return Route::IndexClearPatternsDelete;
        if (target == "/vector/index/save" && method == http::verb::post) return Route::VectorIndexSavePost;
        if (target == "/vector/index/load" && method == http::verb::post) return Route::VectorIndexLoadPost;
        if (target == "/vector/index/config" && method == http::verb::get) return Route::VectorIndexConfigGet;
        if (target == "/vector/index/config" && method == http::verb::put) return Route::VectorIndexConfigPut;
    if (target == "/vector/index/stats" && method == http::verb::get) return Route::VectorIndexStatsGet;
        // PKI endpoints
        if (path_only.rfind("/api/pki/", 0) == 0 && method == http::verb::post) {
            // Expect: /api/pki/:key_id/sign or /api/pki/:key_id/verify
            if (path_only.size() >= 5 && path_only.compare(path_only.size() - 5, 5, "/sign") == 0) return Route::PkiSignPost;
            if (path_only.size() >= 7 && path_only.compare(path_only.size() - 7, 7, "/verify") == 0) return Route::PkiVerifyPost;
        }
        // Keys API
        if (path_only == "/keys" && method == http::verb::get) return Route::KeysListGet;
        if (path_only == "/keys/rotate" && method == http::verb::post) return Route::KeysRotatePost;
        // Classification API
        if (path_only == "/classification/rules" && method == http::verb::get) return Route::ClassificationRulesGet;
        if (path_only == "/classification/test" && method == http::verb::post) return Route::ClassificationTestPost;
        // Reports API
    if (path_only == "/reports/compliance" && method == http::verb::get) return Route::ReportsComplianceGet;
    // Policies (Ranger integration)
    if (path_only == "/policies/import/ranger" && method == http::verb::post) return Route::PoliciesImportRangerPost;
    if (path_only == "/policies/export/ranger" && method == http::verb::get) return Route::PoliciesExportRangerGet;
    // PII endpoints
    if (path_only == "/pii" && method == http::verb::get) return Route::PiiListGet;
    if (path_only == "/pii" && method == http::verb::post) return Route::PiiPost;
    if (path_only.rfind("/pii/export.csv", 0) == 0 && method == http::verb::get) return Route::PiiExportCsvGet;
    if (path_only.rfind("/pii/reveal/", 0) == 0 && method == http::verb::get) return Route::PiiRevealGet;
    if (path_only.rfind("/pii/", 0) == 0 && method == http::verb::get) return Route::PiiGetByUuid;
    if (path_only.rfind("/pii/", 0) == 0 && method == http::verb::delete_) return Route::PiiDeleteDelete;
    // Audit API endpoints
    if (path_only == "/api/audit" && method == http::verb::get) return Route::AuditQueryGet;
    if (path_only == "/api/audit/export/csv" && method == http::verb::get) return Route::AuditExportCsvGet;
        if (target == "/transaction" && method == http::verb::post) return Route::TransactionPost;
        if (target == "/transaction/begin" && method == http::verb::post) return Route::TransactionBeginPost;
        if (target == "/transaction/commit" && method == http::verb::post) return Route::TransactionCommitPost;
        if (target == "/transaction/rollback" && method == http::verb::post) return Route::TransactionRollbackPost;
        if (target == "/transaction/stats" && method == http::verb::get) return Route::TransactionStatsGet;

        // Content API
        if (target == "/content/import" && method == http::verb::post) return Route::ContentImportPost;
        if (target == "/content/config" && method == http::verb::get) return Route::ContentConfigGet;
        if (target == "/content/config" && method == http::verb::put) return Route::ContentConfigPut;
        if (target.rfind("/content/", 0) == 0 && method == http::verb::get) {
            if (target.find("/blob") != std::string::npos) return Route::ContentBlobGet;
            if (target.find("/chunks") != std::string::npos) return Route::ContentChunksGet;
            return Route::ContentGet;
        }

    // Hybrid Search
        if (target == "/search/hybrid" && method == http::verb::post) return Route::HybridSearchPost;
        
        // Fusion Search (Text+Vector with RRF/Weighted)
        if (target == "/search/fusion" && method == http::verb::post) return Route::FusionSearchPost;
        
        // Fulltext Search
        if (target == "/search/fulltext" && method == http::verb::post) return Route::FulltextSearchPost;

    // Content filter schema config
    if (target == "/config/content-filters" && method == http::verb::get) return Route::ContentFilterSchemaGet;
    if (target == "/config/content-filters" && (method == http::verb::put || method == http::verb::post)) return Route::ContentFilterSchemaPut;
    if (target == "/config/edge-weights" && method == http::verb::get) return Route::EdgeWeightConfigGet;
    if (target == "/config/edge-weights" && (method == http::verb::put || method == http::verb::post)) return Route::EdgeWeightConfigPut;
    
    // Encryption schema config
    if (target == "/config/encryption-schema" && method == http::verb::get) return Route::EncryptionSchemaGet;
    if (target == "/config/encryption-schema" && (method == http::verb::put || method == http::verb::post)) return Route::EncryptionSchemaPut;

        return Route::NotFound;
    }
}

http::response<http::string_body> HttpServer::routeRequest(
    const http::request<http::string_body>& req
) {
     // Create span for the entire HTTP request
     auto span = Tracer::startSpan("http_request");
     span.setAttribute("http.method", std::string(http::to_string(req.method())));
     span.setAttribute("http.target", std::string(req.target()));
    
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
        case Route::CapabilitiesGet:
            response = handleCapabilities(req);
            break;
        case Route::Metrics:
            // Prefer the comprehensive metrics exporter (includes vccdb_* metrics).
            // Historically there was an older/smaller `handleMetrics` implementation
            // which only emitted content-related metrics. Use the more complete
            // `handleMetricsJson` handler here so `/metrics` exposes the full set
            // of Prometheus names expected by the tests.
            response = handleMetricsJson(req);
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
        case Route::VectorBatchInsertPost:
            response = handleVectorBatchInsert(req);
            break;
        case Route::VectorDeleteByFilterDelete:
            response = handleVectorDeleteByFilter(req);
            break;
        case Route::CacheQueryPost:
            response = handleCacheQuery(req);
            break;
        case Route::PromptTemplatePost:
            response = handlePromptTemplatePost(req);
            break;
        case Route::PromptTemplateList:
            response = handlePromptTemplateList(req);
            break;
        case Route::PromptTemplateGet:
            response = handlePromptTemplateGet(req);
            break;
        case Route::PromptTemplatePut:
            response = handlePromptTemplatePut(req);
            break;
        case Route::CachePutPost:
            response = handleCachePut(req);
            break;
        case Route::CacheStatsGet:
            response = handleCacheStats(req);
            break;
        case Route::LlmInteractionPost:
            response = handleLlmInteractionPost(req);
            break;
        case Route::LlmInteractionGetList:
            response = handleLlmInteractionList(req);
            break;
        case Route::LlmInteractionGetById:
            response = handleLlmInteractionGet(req);
            break;
        case Route::ChangefeedGet:
            response = handleChangefeedGet(req);
            break;
        case Route::ChangefeedStreamSse:
            response = handleChangefeedStreamSse(req);
            break;
        case Route::ChangefeedStatsGet:
            response = handleChangefeedStats(req);
            break;
        case Route::ChangefeedRetentionPost:
            response = handleChangefeedRetention(req);
            break;
        case Route::TimeSeriesPut:
            response = handleTimeSeriesPut(req);
            break;
        case Route::TimeSeriesQuery:
            response = handleTimeSeriesQuery(req);
            break;
        case Route::TimeSeriesAggregate:
            response = handleTimeSeriesAggregate(req);
            break;
        case Route::TimeSeriesConfigGet:
            response = handleTimeSeriesConfigGet(req);
            break;
        case Route::TimeSeriesConfigPut:
            response = handleTimeSeriesConfigPut(req);
            break;
        case Route::TimeSeriesAggregatesGet:
            response = handleTimeSeriesAggregatesGet(req);
            break;
        case Route::TimeSeriesRetentionGet:
            response = handleTimeSeriesRetentionGet(req);
            break;
        case Route::IndexSuggestionsGet:
            response = handleIndexSuggestions(req);
            break;
        case Route::IndexPatternsGet:
            response = handleIndexPatterns(req);
            break;
        case Route::IndexRecordPatternPost:
            response = handleIndexRecordPattern(req);
            break;
        case Route::IndexClearPatternsDelete:
            response = handleIndexClearPatterns(req);
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
        case Route::KeysListGet:
            response = handleKeysListKeys(req);
            break;
        case Route::PkiSignPost:
            response = handlePkiSign(req);
            break;
        case Route::PkiVerifyPost:
            response = handlePkiVerify(req);
            break;
        case Route::KeysRotatePost:
            response = handleKeysRotateKey(req);
            break;
        case Route::ClassificationRulesGet:
            response = handleClassificationListRules(req);
            break;
        case Route::ClassificationTestPost:
            response = handleClassificationTest(req);
            break;
        case Route::ReportsComplianceGet:
            response = handleReportsCompliance(req);
            break;
        case Route::PiiListGet:
            response = handlePiiListMappings(req);
            break;
        case Route::PiiPost:
            response = handlePiiCreateMapping(req);
            break;
        case Route::PiiGetByUuid:
            response = handlePiiGetByUuid(req);
            break;
        case Route::PiiExportCsvGet:
            response = handlePiiExportCsv(req);
            break;
        case Route::PiiRevealGet:
            response = handlePiiRevealByUuid(req);
            break;
        case Route::PiiDeleteDelete:
            response = handlePiiDeleteByUuid(req);
            break;
        case Route::AuditQueryGet:
            response = handleAuditQuery(req);
            break;
        case Route::AuditExportCsvGet:
            response = handleAuditExportCsv(req);
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
        case Route::FusionSearchPost:
            response = handleFusionSearch(req);
            break;
        case Route::FulltextSearchPost:
            response = handleFulltextSearch(req);
            break;
        case Route::ContentFilterSchemaGet:
            response = handleContentFilterSchemaGet(req);
            break;
        case Route::ContentFilterSchemaPut:
            response = handleContentFilterSchemaPut(req);
            break;
        case Route::ContentConfigGet:
            response = handleContentConfigGet(req);
            break;
        case Route::ContentConfigPut:
            response = handleContentConfigPut(req);
            break;
        case Route::EdgeWeightConfigGet:
            response = handleEdgeWeightConfigGet(req);
            break;
        case Route::EdgeWeightConfigPut:
            response = handleEdgeWeightConfigPut(req);
            break;
        case Route::EncryptionSchemaGet:
            response = handleEncryptionSchemaGet(req);
            break;
        case Route::EncryptionSchemaPut:
            response = handleEncryptionSchemaPut(req);
            break;
        case Route::PoliciesImportRangerPost:
            response = handlePoliciesImportRanger(req);
            break;
        case Route::PoliciesExportRangerGet:
            response = handlePoliciesExportRanger(req);
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
    // Trace status code
    span.setAttribute("http.status_code", static_cast<int64_t>(response.result_int()));
    if (response.result_int() >= 200 && response.result_int() < 400) {
    span.setStatus(true);
    } else {
    span.setStatus(false);
    }

    return response;
}

// -----------------------------------------------------------------------------
// Keys / Classification / Reports API Handlers
// -----------------------------------------------------------------------------

http::response<http::string_body> HttpServer::handleKeysListKeys(
    const http::request<http::string_body>& req
) {
    try {
        if (!keys_api_) {
            return makeErrorResponse(http::status::service_unavailable, "Keys API not available", req);
        }
        auto result = keys_api_->listKeys();
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePkiSign(
    const http::request<http::string_body>& req
) {
    try {
        if (!pki_api_) return makeErrorResponse(http::status::service_unavailable, "PKI API not available", req);

        // Authorization: require pki:sign when auth is enabled
        if (auto resp = requireAccess(req, "pki:sign", "pki.sign", "/api/pki")) return *resp;

        // Extract key_id from path: /api/pki/:key_id/sign
        auto path = std::string(req.target());
        auto key_id = extractPathParam(path, "/api/pki/");
        // key_id currently contains "<key_id>/sign" -> trim suffix
        if (key_id.size() > 5 && key_id.compare(key_id.size() - 5, 5, "/sign") == 0) {
            key_id = key_id.substr(0, key_id.size() - 5);
        }
        if (key_id.empty()) return makeErrorResponse(http::status::bad_request, "Missing key_id", req);

        nlohmann::json body = nlohmann::json::object();
        if (!req.body().empty()) body = nlohmann::json::parse(req.body());

        auto result = pki_api_->sign(key_id, body);
        if (result.contains("status_code")) {
            int sc = result.value("status_code", 500);
            return makeErrorResponse(static_cast<http::status>(sc), result.dump(), req);
        }
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePkiVerify(
    const http::request<http::string_body>& req
) {
    try {
        if (!pki_api_) return makeErrorResponse(http::status::service_unavailable, "PKI API not available", req);

        // Authorization: require pki:verify when auth is enabled
        if (auto resp = requireAccess(req, "pki:verify", "pki.verify", "/api/pki")) return *resp;

        // Extract key_id from path: /api/pki/:key_id/verify
        auto path = std::string(req.target());
        auto key_id = extractPathParam(path, "/api/pki/");
        if (key_id.size() > 7 && key_id.compare(key_id.size() - 7, 7, "/verify") == 0) {
            key_id = key_id.substr(0, key_id.size() - 7);
        }
        if (key_id.empty()) return makeErrorResponse(http::status::bad_request, "Missing key_id", req);

        nlohmann::json body = nlohmann::json::object();
        if (!req.body().empty()) body = nlohmann::json::parse(req.body());

        auto result = pki_api_->verify(key_id, body);
        if (result.contains("status_code")) {
            int sc = result.value("status_code", 500);
            return makeErrorResponse(static_cast<http::status>(sc), result.dump(), req);
        }
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleKeysRotateKey(
    const http::request<http::string_body>& req
) {
    try {
        if (!keys_api_) {
            return makeErrorResponse(http::status::service_unavailable, "Keys API not available", req);
        }
        // Parse key_id from body or query string
        std::string key_id;
        try {
            if (!req.body().empty()) {
                auto body = json::parse(req.body());
                if (body.contains("key_id")) key_id = body.value("key_id", "");
            }
        } catch (...) {
            // ignore body parse errors; fallback to query param
        }
        if (key_id.empty()) {
            std::string target = std::string(req.target());
            auto qpos = target.find('?');
            if (qpos != std::string::npos) {
                auto qs = target.substr(qpos + 1);
                std::istringstream iss(qs);
                std::string kv;
                while (std::getline(iss, kv, '&')) {
                    auto eq = kv.find('=');
                    if (eq != std::string::npos) {
                        auto k = kv.substr(0, eq);
                        auto v = kv.substr(eq + 1);
                        if (k == "key_id") { key_id = v; break; }
                    }
                }
            }
        }
        if (key_id.empty()) {
            return makeErrorResponse(http::status::bad_request, "Missing key_id", req);
        }
        // Pass original body if JSON else empty
        json body_json;
        try { if (!req.body().empty()) body_json = json::parse(req.body()); } catch (...) {}
        auto result = keys_api_->rotateKey(key_id, body_json);
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleClassificationListRules(
    const http::request<http::string_body>& req
) {
    try {
        if (!classification_api_) {
            return makeErrorResponse(http::status::service_unavailable, "Classification API not available", req);
        }
        auto result = classification_api_->listRules();
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleClassificationTest(
    const http::request<http::string_body>& req
) {
    try {
        if (!classification_api_) {
            return makeErrorResponse(http::status::service_unavailable, "Classification API not available", req);
        }
        if (req.body().empty()) {
            return makeErrorResponse(http::status::bad_request, "Missing JSON body", req);
        }
        auto body = json::parse(req.body());
        auto result = classification_api_->testClassification(body);
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

// -----------------------------------------------------------------------------
// Prompt Template CRUD handlers
// -----------------------------------------------------------------------------

http::response<http::string_body> HttpServer::handlePromptTemplatePost(
    const http::request<http::string_body>& req
) {
    try {
        if (!prompt_manager_) return makeErrorResponse(http::status::service_unavailable, "PromptManager not available", req);
        // Authorization: require data:write for creating templates
        if (auto resp = requireAccess(req, "data:write", "prompt_template.create", "/prompt_template")) return *resp;

        if (req.body().empty()) return makeErrorResponse(http::status::bad_request, "Missing JSON body", req);
        auto body = json::parse(req.body());
        themis::PromptManager::PromptTemplate t;
        if (body.contains("id")) t.id = body.value("id", std::string());
        if (body.contains("name")) t.name = body.value("name", std::string());
        if (body.contains("version")) t.version = body.value("version", std::string());
        if (body.contains("content")) t.content = body.value("content", std::string());
        if (body.contains("metadata")) t.metadata = body["metadata"];
        if (body.contains("active")) t.active = body.value("active", true);

        auto created = prompt_manager_->createTemplate(std::move(t));
        return makeResponse(http::status::created, created.toJson().dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePromptTemplateList(
    const http::request<http::string_body>& req
) {
    try {
        if (!prompt_manager_) return makeErrorResponse(http::status::service_unavailable, "PromptManager not available", req);
        // Authorization: require data:read for listing templates
        if (auto resp = requireAccess(req, "data:read", "prompt_template.list", "/prompt_template")) return *resp;

        auto list = prompt_manager_->listTemplates();
        nlohmann::json out = nlohmann::json::array();
        for (const auto& t : list) out.push_back(t.toJson());
        return makeResponse(http::status::ok, out.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePromptTemplateGet(
    const http::request<http::string_body>& req
) {
    try {
        if (!prompt_manager_) return makeErrorResponse(http::status::service_unavailable, "PromptManager not available", req);
        // Authorization: require data:read
        if (auto resp = requireAccess(req, "data:read", "prompt_template.get", "/prompt_template")) return *resp;

        std::string path = std::string(req.target());
        auto id = extractPathParam(path, "/prompt_template/");
        if (id.empty()) return makeErrorResponse(http::status::bad_request, "Missing template id", req);
        auto opt = prompt_manager_->getTemplate(id);
        if (!opt.has_value()) return makeErrorResponse(http::status::not_found, "Template not found", req);
        return makeResponse(http::status::ok, opt->toJson().dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePromptTemplatePut(
    const http::request<http::string_body>& req
) {
    try {
        if (!prompt_manager_) return makeErrorResponse(http::status::service_unavailable, "PromptManager not available", req);
        // Authorization: require data:write for updates
        if (auto resp = requireAccess(req, "data:write", "prompt_template.update", "/prompt_template")) return *resp;

        std::string path = std::string(req.target());
        auto id = extractPathParam(path, "/prompt_template/");
        if (id.empty()) return makeErrorResponse(http::status::bad_request, "Missing template id", req);
        if (req.body().empty()) return makeErrorResponse(http::status::bad_request, "Missing JSON body", req);
        auto body = json::parse(req.body());
        nlohmann::json metadata = nlohmann::json::object();
        bool active = true;
        if (body.contains("metadata")) metadata = body["metadata"];
        if (body.contains("active")) active = body.value("active", true);
        bool ok = prompt_manager_->updateTemplate(id, metadata, active);
        if (!ok) return makeErrorResponse(http::status::not_found, "Template not found", req);
        auto updated_opt = prompt_manager_->getTemplate(id);
        nlohmann::json out = updated_opt ? updated_opt->toJson() : nlohmann::json::object();
        return makeResponse(http::status::ok, out.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleReportsCompliance(
    const http::request<http::string_body>& req
) {
    try {
        if (!reports_api_) {
            return makeErrorResponse(http::status::service_unavailable, "Reports API not available", req);
        }
        std::string report_type = "overview";
        std::string target = std::string(req.target());
        auto qpos = target.find('?');
        if (qpos != std::string::npos) {
            auto qs = target.substr(qpos + 1);
            std::istringstream iss(qs);
            std::string kv;
            while (std::getline(iss, kv, '&')) {
                auto eq = kv.find('=');
                if (eq != std::string::npos) {
                    auto k = kv.substr(0, eq);
                    auto v = kv.substr(eq + 1);
                    if (k == "type" && !v.empty()) { report_type = v; break; }
                }
            }
        }
        auto result = reports_api_->generateComplianceReport(report_type);
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

// -----------------------------------------------------------------------------
// Metrics exporter (Prometheus text exposition)
// -----------------------------------------------------------------------------
http::response<http::string_body> HttpServer::handleMetrics(const http::request<http::string_body>& req) {
    try {
        std::ostringstream out;
        out << "# HELP themis_content_blob_compressed_bytes_total Total bytes stored compressed for content blobs\n";
        out << "# TYPE themis_content_blob_compressed_bytes_total counter\n";
        out << "# HELP themis_content_blob_uncompressed_bytes_total Total uncompressed/original bytes observed for content blob uploads\n";
        out << "# TYPE themis_content_blob_uncompressed_bytes_total counter\n";
        out << "# HELP themis_content_blob_compression_skipped_total Number of uploads skipped for compression (by MIME prefix)\n";
        out << "# TYPE themis_content_blob_compression_skipped_total counter\n";
        out << "# HELP themis_content_blob_compression_ratio Histogram of compression ratios (original_size / compressed_size) per upload\n";
        out << "# TYPE themis_content_blob_compression_ratio histogram\n";

        // Content metrics (if available)
        if (content_manager_) {
            const auto& m = content_manager_->getMetrics();
            uint64_t comp_bytes = m.compressed_bytes_total.load(std::memory_order_relaxed);
            uint64_t uncomp_bytes = m.uncompressed_bytes_total.load(std::memory_order_relaxed);
            uint64_t skipped = m.compression_skipped_total.load(std::memory_order_relaxed);
            out << "themis_content_blob_compressed_bytes_total " << comp_bytes << "\n";
            out << "themis_content_blob_uncompressed_bytes_total " << uncomp_bytes << "\n";
            out << "themis_content_blob_compression_skipped_total " << skipped << "\n";

            // Skipped by known categories
            uint64_t skipped_img = m.compression_skipped_image_total.load(std::memory_order_relaxed);
            uint64_t skipped_vid = m.compression_skipped_video_total.load(std::memory_order_relaxed);
            uint64_t skipped_zip = m.compression_skipped_zip_total.load(std::memory_order_relaxed);
            out << "themis_content_blob_compression_skipped_total{mime_prefix=\"image/\"} " << skipped_img << "\n";
            out << "themis_content_blob_compression_skipped_total{mime_prefix=\"video/\"} " << skipped_vid << "\n";
            out << "themis_content_blob_compression_skipped_total{mime_prefix=\"application/zip\"} " << skipped_zip << "\n";

            // Build cumulative buckets from per-bucket counts
            std::vector<std::pair<std::string, uint64_t>> buckets = {
                {"1", m.comp_ratio_le_1.load(std::memory_order_relaxed)},
                {"1.5", m.comp_ratio_le_1_5.load(std::memory_order_relaxed)},
                {"2", m.comp_ratio_le_2.load(std::memory_order_relaxed)},
                {"3", m.comp_ratio_le_3.load(std::memory_order_relaxed)},
                {"5", m.comp_ratio_le_5.load(std::memory_order_relaxed)},
                {"10", m.comp_ratio_le_10.load(std::memory_order_relaxed)},
                {"100", m.comp_ratio_le_100.load(std::memory_order_relaxed)},
                {"+Inf", m.comp_ratio_le_inf.load(std::memory_order_relaxed)}
            };
            // Convert per-bucket counts to cumulative counts while preserving order
            uint64_t running = 0;
            for (size_t i = 0; i < buckets.size(); ++i) {
                running += buckets[i].second;
                out << "themis_content_blob_compression_ratio_bucket{le=\"" << buckets[i].first << "\"} " << running << "\n";
            }
            // sum and count
            uint64_t cnt = m.comp_ratio_count.load(std::memory_order_relaxed);
            double sum = static_cast<double>(m.comp_ratio_sum_milli.load(std::memory_order_relaxed)) / 1000.0;
            out << "themis_content_blob_compression_ratio_sum " << std::fixed << std::setprecision(3) << sum << "\n";
            out << "themis_content_blob_compression_ratio_count " << cnt << "\n";
        } else {
            // No content manager configured: emit zeros
            out << "themis_content_blob_compressed_bytes_total 0\n";
            out << "themis_content_blob_uncompressed_bytes_total 0\n";
            out << "themis_content_blob_compression_skipped_total 0\n";
            out << "themis_content_blob_compression_skipped_total{mime_prefix=\"image/\"} 0\n";
            out << "themis_content_blob_compression_skipped_total{mime_prefix=\"video/\"} 0\n";
            out << "themis_content_blob_compression_skipped_total{mime_prefix=\"application/zip\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"1\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"1.5\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"2\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"3\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"5\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"10\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"100\"} 0\n";
            out << "themis_content_blob_compression_ratio_bucket{le=\"+Inf\"} 0\n";
            out << "themis_content_blob_compression_ratio_sum 0\n";
            out << "themis_content_blob_compression_ratio_count 0\n";
        }

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "text/plain; version=0.0.4; charset=utf-8");
        res.body() = out.str();
        res.prepare_payload();
        return res;
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

// -----------------------------------------------------------------------------
// Existing handlers
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Audit API Handlers
// -----------------------------------------------------------------------------

namespace {
    // Percent-decode helper for application/x-www-form-urlencoded style (handles + and %HH)
    static std::string urlDecode(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            char c = in[i];
            if (c == '+') { out.push_back(' '); }
            else if (c == '%' && i + 2 < in.size()) {
                auto hex = in.substr(i + 1, 2);
                int v = 0;
                if (std::isxdigit(static_cast<unsigned char>(hex[0])) && std::isxdigit(static_cast<unsigned char>(hex[1]))) {
                    v = std::stoi(hex, nullptr, 16);
                    out.push_back(static_cast<char>(v));
                    i += 2;
                } else {
                    out.push_back(c);
                }
            } else { out.push_back(c); }
        }
        return out;
    }

    // URL query parser with percent-decoding
    static std::unordered_map<std::string, std::string> parseQuery(const std::string& target) {
        std::unordered_map<std::string, std::string> out;
        auto qpos = target.find('?');
        if (qpos == std::string::npos) return out;
        auto qs = target.substr(qpos + 1);
        std::istringstream iss(qs);
        std::string kv;
        while (std::getline(iss, kv, '&')) {
            auto eq = kv.find('=');
            std::string k = (eq == std::string::npos) ? kv : kv.substr(0, eq);
            std::string v = (eq == std::string::npos) ? std::string() : kv.substr(eq + 1);
            out[urlDecode(k)] = urlDecode(v);
        }
        return out;
    }
    // Parse ISO8601 with optional fractional seconds and timezone (Z or ±HH:MM), or epoch ms
    static int64_t parseTimeMs(const std::string& s) {
        if (s.empty()) return 0;
        bool numeric = std::all_of(s.begin(), s.end(), [](char c){ return c >= '0' && c <= '9'; });
        if (numeric) {
            try { return std::stoll(s); } catch (...) { return 0; }
        }
        // ISO8601 parsing
        // Expected: YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]
        std::tm tm{};
        tm.tm_isdst = -1;
        // Split at 'T'
        auto tpos = s.find('T');
        if (tpos == std::string::npos) return 0;
        std::string date = s.substr(0, tpos);
        std::string rest = s.substr(tpos + 1);
        // Parse date
        if (date.size() != 10) return 0;
        std::istringstream dss(date);
        dss >> std::get_time(&tm, "%Y-%m-%d");
        if (dss.fail()) return 0;
        // Find timezone marker
        int tz_sign = 0; int tz_h = 0; int tz_m = 0;
        int64_t millis = 0;
        // Separate time part from timezone
        size_t zpos = rest.find('Z');
        size_t plus = rest.rfind('+');
        size_t minus = rest.rfind('-');
        size_t tzpos = std::string::npos;
        if (zpos != std::string::npos) tzpos = zpos;
        else if (plus != std::string::npos) tzpos = plus;
        else if (minus != std::string::npos) tzpos = (minus > 1 ? minus : std::string::npos);
        std::string timepart = (tzpos == std::string::npos) ? rest : rest.substr(0, tzpos);
        std::string tzpart = (tzpos == std::string::npos) ? std::string() : rest.substr(tzpos);
        // Parse time with optional fractional seconds
        // timepart like HH:MM:SS[.fff]
        int H=0,M=0; double S=0.0;
        char c1=':', c2=':';
        std::istringstream tss(timepart);
        tss >> H >> c1 >> M >> c2 >> S;
        if (tss.fail() || c1 != ':' || c2 != ':') return 0;
        tm.tm_hour = H; tm.tm_min = M; tm.tm_sec = static_cast<int>(S);
        millis = static_cast<int64_t>((S - tm.tm_sec) * 1000.0 + 0.5);
        // Parse timezone
        if (!tzpart.empty()) {
            if (tzpart[0] == 'Z') { tz_sign = 0; }
            else if (tzpart[0] == '+' || tzpart[0] == '-') {
                tz_sign = (tzpart[0] == '+') ? +1 : -1;
                // format ±HH:MM
                if (tzpart.size() >= 6 && tzpart[3] == ':') {
                    try {
                        tz_h = std::stoi(tzpart.substr(1,2));
                        tz_m = std::stoi(tzpart.substr(4,2));
                    } catch (...) { tz_h = tz_m = 0; tz_sign = 0; }
                }
            }
        }
    // Build UTC epoch seconds from tm (interpreted as UTC)
    time_t secs = portable_mkgmtime_impl(&tm);
        if (secs == static_cast<time_t>(-1)) return 0;
        // Adjust for timezone offset: local time part represents wall time in given TZ
        int offset_secs = tz_sign * (tz_h * 3600 + tz_m * 60);
        int64_t epoch_ms = (static_cast<int64_t>(secs) - offset_secs) * 1000 + millis;
        return epoch_ms;
    }
}

http::response<http::string_body> HttpServer::handleAuditQuery(
    const http::request<http::string_body>& req
) {
    try {
        if (auto rl = enforceAuditRateLimit(req, "/api/audit")) return *rl;
        // Authorization: require audit:read when auth is enabled
        if (auto resp = requireAccess(req, "audit:read", "audit.read", "/api/audit")) return *resp;
        if (!audit_api_) {
            return makeErrorResponse(http::status::service_unavailable, "Audit API not available", req);
        }
        auto params = parseQuery(std::string(req.target()));
        themis::server::AuditQueryFilter f;
        if (auto it = params.find("start"); it != params.end()) f.start_ts_ms = parseTimeMs(it->second);
        if (auto it = params.find("end"); it != params.end()) f.end_ts_ms = parseTimeMs(it->second);
        if (auto it = params.find("user"); it != params.end()) f.user = it->second;
        if (auto it = params.find("action"); it != params.end()) f.action = it->second;
        if (auto it = params.find("entity_type"); it != params.end()) f.entity_type = it->second;
        if (auto it = params.find("entity_id"); it != params.end()) f.entity_id = it->second;
        if (auto it = params.find("success"); it != params.end()) {
            auto v = it->second; std::transform(v.begin(), v.end(), v.begin(), ::tolower);
            f.success_only = (v == "true" || v == "1" || v == "yes");
        }
        if (auto it = params.find("page"); it != params.end()) {
            try { f.page = std::max(1, std::stoi(it->second)); } catch (...) {}
        }
        if (auto it = params.find("page_size"); it != params.end()) {
            try {
                f.page_size = std::stoi(it->second);
                if (f.page_size < 1) f.page_size = 1;
                if (f.page_size > 1000) f.page_size = 1000;
            } catch (...) {}
        }
        auto result = audit_api_->queryAuditLogs(f);
        return makeResponse(http::status::ok, result.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleAuditExportCsv(
    const http::request<http::string_body>& req
) {
    try {
        if (auto rl = enforceAuditRateLimit(req, "/api/audit/export/csv")) return *rl;
        // Authorization: require audit:read when auth is enabled
        if (auto resp = requireAccess(req, "audit:read", "audit.read", "/api/audit/export/csv")) return *resp;
        if (!audit_api_) {
            return makeErrorResponse(http::status::service_unavailable, "Audit API not available", req);
        }
        auto params = parseQuery(std::string(req.target()));
        themis::server::AuditQueryFilter f;
        if (auto it = params.find("start"); it != params.end()) f.start_ts_ms = parseTimeMs(it->second);
        if (auto it = params.find("end"); it != params.end()) f.end_ts_ms = parseTimeMs(it->second);
        if (auto it = params.find("user"); it != params.end()) f.user = it->second;
        if (auto it = params.find("action"); it != params.end()) f.action = it->second;
        if (auto it = params.find("entity_type"); it != params.end()) f.entity_type = it->second;
        if (auto it = params.find("entity_id"); it != params.end()) f.entity_id = it->second;
        if (auto it = params.find("success"); it != params.end()) {
            auto v = it->second; std::transform(v.begin(), v.end(), v.begin(), ::tolower);
            f.success_only = (v == "true" || v == "1" || v == "yes");
        }
        if (auto it = params.find("page"); it != params.end()) {
            try { f.page = std::max(1, std::stoi(it->second)); } catch (...) {}
        }
        if (auto it = params.find("page_size"); it != params.end()) {
            try {
                f.page_size = std::stoi(it->second);
                if (f.page_size < 1) f.page_size = 1;
                if (f.page_size > 10000) f.page_size = 10000; // allow larger for export
            } catch (...) {}
        }

        auto csv = audit_api_->exportAuditLogsCsv(f);
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "THEMIS/0.1.0");
        res.set(http::field::content_type, "text/csv");
        res.set(http::field::content_disposition, "attachment; filename=themis_audit_export.csv");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(csv);
        applyGovernanceHeaders(req, res);
        res.prepare_payload();
        return res;
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

std::optional<http::response<http::string_body>> HttpServer::enforceAuditRateLimit(
    const http::request<http::string_body>& req,
    std::string_view route_key
) {
    try {
        if (audit_rate_limit_per_minute_ == 0) return std::nullopt;
        // Determine bucket key: Authorization header if present, else "anon"
        std::string key = std::string(route_key) + ":";
        auto it = req.find(http::field::authorization);
        if (it != req.end()) key += std::string(it->value()); else key += "anon";
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now()).time_since_epoch().count();
        const uint64_t window_ms = 60ull * 1000ull;
        uint32_t limit = audit_rate_limit_per_minute_;
        uint32_t count = 0;
        {
            std::lock_guard<std::mutex> lk(audit_rate_mutex_);
            auto& st = audit_rate_buckets_[key];
            if (now - st.window_start_ms >= window_ms) {
                st.window_start_ms = now;
                st.count = 0;
            }
            if (st.count >= limit) {
                THEMIS_DEBUG("AUDIT_RL_HIT key={} count={} limit={}", key, st.count, limit);
                // respond 429 with diagnostic headers
                auto resp = makeErrorResponse(http::status::too_many_requests, "Rate limit exceeded", req);
                resp.set(http::field::retry_after, "60");
                resp.set("X-RateLimit-Limit", std::to_string(limit));
                resp.set("X-RateLimit-Remaining", "0");
                return resp;
            }
            st.count++;
            count = st.count;
            THEMIS_DEBUG("AUDIT_RL_OK key={} count={} limit={}", key, count, limit);
        }
        (void)count;
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
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

http::response<http::string_body> HttpServer::handleCapabilities(
    const http::request<http::string_body>& req
) {
    // No auth required for capabilities (read-only, non-sensitive)
    json caps;

    // Build flags
#ifdef THEMIS_GEO_ENABLED
    const bool geo_enabled = true;
#else
    const bool geo_enabled = false;
#endif
#ifdef THEMIS_GEO_SIMD_ENABLED
    const bool geo_simd = true;
#else
    const bool geo_simd = false;
#endif
#ifdef THEMIS_GEO_GPU_ENABLED
    const bool geo_gpu = true;
#else
    const bool geo_gpu = false;
#endif
#ifdef THEMIS_GEO_H3_ENABLED
    const bool geo_h3 = true;
#else
    const bool geo_h3 = false;
#endif
#ifdef THEMIS_GEO_GEOS_PLUGIN_ENABLED
    const bool geo_geos = true;
#else
    const bool geo_geos = false;
#endif
#ifdef THEMIS_ENTERPRISE_ENABLED
    const bool enterprise = true;
#else
    const bool enterprise = false;
#endif

#ifdef THEMIS_GPU_ENABLED
    const bool vector_gpu = true;
#else
    const bool vector_gpu = false;
#endif

    caps["geo"] = {
        {"enabled", geo_enabled},
        {"enterprise_compiled", enterprise},
        {"accel", {
            {"simd_compiled", geo_simd},
            {"gpu_compiled", geo_gpu}
        }},
        {"plugins_compiled", {
            {"geos", geo_geos},
            {"h3", geo_h3}
        }}
    };

    caps["vector"] = {
        {"gpu_compiled", vector_gpu}
    };

    // Server basics
    caps["server"] = {
        {"version", "1.0.0"},
        {"threads", config_.num_threads}
    };

    return makeResponse(http::status::ok, caps.dump(), req);
}

http::response<http::string_body> HttpServer::handleConfig(
    const http::request<http::string_body>& req
) {
    // GET -> config:read, POST -> config:write (if auth enabled)
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (req.method() == http::verb::post) {
            if (auto resp = requireAccess(req, "config:write", "config.write", path_only)) return *resp;
        } else {
            if (auto resp = requireAccess(req, "config:read", "config.read", path_only)) return *resp;
        }
    }
    try {
        // Allow POST to update runtime config (Hot-Reload)
        if (req.method() == http::verb::post) {
            json body;
            try {
                body = json::parse(req.body());
            } catch (...) {
                return makeErrorResponse(http::status::bad_request, "Invalid JSON body", req);
            }
            
            // 1) Logging config (level, format)
            if (body.contains("logging") && body["logging"].is_object()) {
                const auto& lg = body["logging"];
                // level
                if (lg.contains("level")) {
                    auto lvl = lg["level"].get<std::string>();
                    auto mapped = themis::utils::Logger::levelFromString(lvl);
                    themis::utils::Logger::setLevel(mapped);
                    THEMIS_INFO("Hot-reload: logging.level set to {}", lvl);
                }
                // format
                if (lg.contains("format")) {
                    auto fmt = lg["format"].get<std::string>();
                    std::string pattern;
                    if (fmt == "json") {
                        // Minimal JSON line pattern
                        pattern = "{\"ts\":\"%Y-%m-%dT%H:%M:%S.%e\",\"level\":\"%l\",\"thread\":%t,\"msg\":\"%v\"}";
                    } else {
                        // Default text pattern
                        pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [thread %t] %v";
                    }
                    themis::utils::Logger::setPattern(pattern);
                    THEMIS_INFO("Hot-reload: logging.format set to {}", fmt);
                }
            }
            
            // 2) Request timeout
            if (body.contains("request_timeout_ms")) {
                auto timeout = body["request_timeout_ms"].get<uint32_t>();
                if (timeout >= 1000 && timeout <= 300000) { // 1s - 5min range
                    config_.request_timeout_ms = timeout;
                    THEMIS_INFO("Hot-reload: request_timeout_ms set to {}", timeout);
                } else {
                    return makeErrorResponse(http::status::bad_request, "request_timeout_ms must be 1000-300000", req);
                }
            }
            
            // 3) Feature flags (runtime toggle for beta features)
            if (body.contains("features") && body["features"].is_object()) {
                const auto& features = body["features"];
                if (features.contains("semantic_cache")) {
                    bool enabled = features["semantic_cache"].get<bool>();
                    config_.feature_semantic_cache = enabled;
                    THEMIS_INFO("Hot-reload: feature_semantic_cache set to {}", enabled);
                }
                if (features.contains("llm_store")) {
                    bool enabled = features["llm_store"].get<bool>();
                    config_.feature_llm_store = enabled;
                    THEMIS_INFO("Hot-reload: feature_llm_store set to {}", enabled);
                }
                if (features.contains("cdc")) {
                    bool enabled = features["cdc"].get<bool>();
                    config_.feature_cdc = enabled;
                    THEMIS_INFO("Hot-reload: feature_cdc set to {}", enabled);
                }
                if (features.contains("timeseries")) {
                    bool enabled = features["timeseries"].get<bool>();
                    config_.feature_timeseries = enabled;
                    THEMIS_INFO("Hot-reload: feature_timeseries set to {}", enabled);
                }
            }
            
            // 4) CDC Retention policy (auto-cleanup threshold)
            if (body.contains("cdc_retention_hours")) {
                if (!config_.feature_cdc || !changefeed_) {
                    return makeErrorResponse(http::status::bad_request, "CDC not enabled", req);
                }
                auto hours = body["cdc_retention_hours"].get<uint32_t>();
                if (hours < 1 || hours > 8760) { // 1 hour - 1 year
                    return makeErrorResponse(http::status::bad_request, "cdc_retention_hours must be 1-8760", req);
                }
                // Store retention policy (Changefeed class would need to expose this)
                // For MVP, we'll just log it - actual auto-cleanup requires background task
                THEMIS_INFO("Hot-reload: cdc_retention_hours set to {} (requires manual /changefeed/retention call)", hours);
            }
            
            // Respond with updated config
        }

        // Build comprehensive config response
        json response = {
            {"server", {
                {"port", config_.port},
                {"threads", config_.num_threads},
                {"request_timeout_ms", config_.request_timeout_ms}
            }},
            {"features", {
                {"semantic_cache", config_.feature_semantic_cache},
                {"llm_store", config_.feature_llm_store},
                {"cdc", config_.feature_cdc},
                {"timeseries", config_.feature_timeseries}
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

http::response<http::string_body> HttpServer::handleMetricsJson(
    const http::request<http::string_body>& req
) {
    // Require metrics:read scope when auth is enabled
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "metrics:read", "metrics.read", path_only)) return *resp;
    }
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
        // Auth metrics (if enabled)
        if (auth_ && auth_->isEnabled()) {
            const auto& m = auth_->getMetrics();
            out += "# HELP vccdb_authz_success_total Successful authorizations\n";
            out += "# TYPE vccdb_authz_success_total counter\n";
            out += "vccdb_authz_success_total " + std::to_string(m.authz_success_total.load()) + "\n";
            out += "# HELP vccdb_authz_denied_total Denied authorizations (forbidden)\n";
            out += "# TYPE vccdb_authz_denied_total counter\n";
            out += "vccdb_authz_denied_total " + std::to_string(m.authz_denied_total.load()) + "\n";
            out += "# HELP vccdb_authz_invalid_token_total Invalid or missing tokens\n";
            out += "# TYPE vccdb_authz_invalid_token_total counter\n";
            out += "vccdb_authz_invalid_token_total " + std::to_string(m.authz_invalid_token_total.load()) + "\n";
        }

        // Policy Engine metrics (if enabled)
        if (policy_engine_) {
            const auto& pm = policy_engine_->getMetrics();
            out += "# HELP vccdb_policy_eval_total Total policy evaluations\n";
            out += "# TYPE vccdb_policy_eval_total counter\n";
            out += "vccdb_policy_eval_total " + std::to_string(pm.policy_eval_total.load()) + "\n";
            out += "# HELP vccdb_policy_allow_total Allow decisions by policy engine\n";
            out += "# TYPE vccdb_policy_allow_total counter\n";
            out += "vccdb_policy_allow_total " + std::to_string(pm.policy_allow_total.load()) + "\n";
            out += "# HELP vccdb_policy_deny_total Deny decisions by policy engine\n";
            out += "# TYPE vccdb_policy_deny_total counter\n";
            out += "vccdb_policy_deny_total " + std::to_string(pm.policy_deny_total.load()) + "\n";
        }

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

        // Export latency histogram buckets (ensure Prometheus cumulative semantics)
        {
            // load raw bucket values (may be cumulative already or per-bucket).
            std::vector<uint64_t> raw = {
                latency_bucket_100us_.load(std::memory_order_relaxed),
                latency_bucket_500us_.load(std::memory_order_relaxed),
                latency_bucket_1ms_.load(std::memory_order_relaxed),
                latency_bucket_5ms_.load(std::memory_order_relaxed),
                latency_bucket_10ms_.load(std::memory_order_relaxed),
                latency_bucket_50ms_.load(std::memory_order_relaxed),
                latency_bucket_100ms_.load(std::memory_order_relaxed),
                latency_bucket_500ms_.load(std::memory_order_relaxed),
                latency_bucket_1s_.load(std::memory_order_relaxed),
                latency_bucket_5s_.load(std::memory_order_relaxed),
                latency_bucket_inf_.load(std::memory_order_relaxed)
            };

            // detect if raw is already non-decreasing (cumulative). If not, treat as per-bucket and compute running sum.
            bool non_decreasing = true;
            for (size_t i = 1; i < raw.size(); ++i) if (raw[i] < raw[i-1]) { non_decreasing = false; break; }

            uint64_t running = 0;
            auto emit = [&](const char* name, uint64_t value){ out += std::string(name) + " " + std::to_string(value) + "\n"; };

            const char* names[] = {
                "vccdb_latency_bucket_microseconds{le=\"100\"}",
                "vccdb_latency_bucket_microseconds{le=\"500\"}",
                "vccdb_latency_bucket_microseconds{le=\"1000\"}",
                "vccdb_latency_bucket_microseconds{le=\"5000\"}",
                "vccdb_latency_bucket_microseconds{le=\"10000\"}",
                "vccdb_latency_bucket_microseconds{le=\"50000\"}",
                "vccdb_latency_bucket_microseconds{le=\"100000\"}",
                "vccdb_latency_bucket_microseconds{le=\"500000\"}",
                "vccdb_latency_bucket_microseconds{le=\"1000000\"}",
                "vccdb_latency_bucket_microseconds{le=\"5000000\"}",
                "vccdb_latency_bucket_microseconds{le=\"+Inf\"}"
            };

            for (size_t i = 0; i < raw.size(); ++i) {
                if (non_decreasing) {
                    // values already cumulative
                    running = raw[i];
                } else {
                    // raw are per-bucket counts -> accumulate
                    running += raw[i];
                }
                emit(names[i], running);
            }
        }

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
    {
        std::vector<uint64_t> raw = {
            page_bucket_1ms_.load(std::memory_order_relaxed),
            page_bucket_5ms_.load(std::memory_order_relaxed),
            page_bucket_10ms_.load(std::memory_order_relaxed),
            page_bucket_25ms_.load(std::memory_order_relaxed),
            page_bucket_50ms_.load(std::memory_order_relaxed),
            page_bucket_100ms_.load(std::memory_order_relaxed),
            page_bucket_250ms_.load(std::memory_order_relaxed),
            page_bucket_500ms_.load(std::memory_order_relaxed),
            page_bucket_1000ms_.load(std::memory_order_relaxed),
            page_bucket_5000ms_.load(std::memory_order_relaxed),
            page_bucket_inf_.load(std::memory_order_relaxed)
        };
        bool non_decreasing = true;
        for (size_t i = 1; i < raw.size(); ++i) if (raw[i] < raw[i-1]) { non_decreasing = false; break; }
        uint64_t running = 0;
        const char* names[] = {
            "vccdb_page_fetch_time_ms_bucket{le=\"1\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"5\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"10\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"25\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"50\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"100\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"250\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"500\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"1000\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"5000\"}",
            "vccdb_page_fetch_time_ms_bucket{le=\"+Inf\"}"
        };
        for (size_t i = 0; i < raw.size(); ++i) {
            if (non_decreasing) running = raw[i]; else running += raw[i];
            out += std::string(names[i]) + " " + std::to_string(running) + "\n";
        }
    }
    out += "# HELP vccdb_page_fetch_time_ms_sum Total cursor page fetch time in milliseconds\n";
    out += "# TYPE vccdb_page_fetch_time_ms_sum counter\n";
    out += "vccdb_page_fetch_time_ms_sum " + std::to_string(page_sum_ms_.load(std::memory_order_relaxed)) + "\n";
    out += "# HELP vccdb_page_fetch_time_ms_count Total number of cursor pages fetched\n";
    out += "# TYPE vccdb_page_fetch_time_ms_count counter\n";
    out += "vccdb_page_fetch_time_ms_count " + std::to_string(page_count_.load(std::memory_order_relaxed)) + "\n";

        // Vector Index metrics
        if (vector_index_) {
            uint64_t vector_count = vector_index_->getVectorCount();
            int dimension = vector_index_->getDimension();
            bool hnsw_enabled = vector_index_->isHnswEnabled();
            
            out += "# HELP vccdb_vector_index_vectors_total Total number of vectors in the index\n";
            out += "# TYPE vccdb_vector_index_vectors_total gauge\n";
            out += "vccdb_vector_index_vectors_total " + std::to_string(vector_count) + "\n";
            
            out += "# HELP vccdb_vector_index_dimension Dimension of vectors in the index\n";
            out += "# TYPE vccdb_vector_index_dimension gauge\n";
            out += "vccdb_vector_index_dimension " + std::to_string(dimension) + "\n";
            
            out += "# HELP vccdb_vector_index_hnsw_enabled HNSW index enabled (1=yes, 0=no)\n";
            out += "# TYPE vccdb_vector_index_hnsw_enabled gauge\n";
            out += "vccdb_vector_index_hnsw_enabled " + std::to_string(hnsw_enabled ? 1 : 0) + "\n";
            
            if (hnsw_enabled) {
                out += "# HELP vccdb_vector_index_ef_search Current efSearch parameter for HNSW\n";
                out += "# TYPE vccdb_vector_index_ef_search gauge\n";
                out += "vccdb_vector_index_ef_search " + std::to_string(vector_index_->getEfSearch()) + "\n";
                
                out += "# HELP vccdb_vector_index_m HNSW M parameter (neighbors per layer)\n";
                out += "# TYPE vccdb_vector_index_m gauge\n";
                out += "vccdb_vector_index_m " + std::to_string(vector_index_->getM()) + "\n";
            }
        }

        // SSE/Changefeed streaming metrics
        if (sse_manager_) {
            auto sstats = sse_manager_->getStats();
            out += "# HELP vccdb_sse_active_connections Number of active SSE connections\n";
            out += "# TYPE vccdb_sse_active_connections gauge\n";
            out += "vccdb_sse_active_connections " + std::to_string(sstats.active_connections) + "\n";

            out += "# HELP vccdb_sse_events_sent_total Total SSE events sent\n";
            out += "# TYPE vccdb_sse_events_sent_total counter\n";
            out += "vccdb_sse_events_sent_total " + std::to_string(sstats.total_events_sent) + "\n";

            out += "# HELP vccdb_sse_heartbeats_total Total SSE heartbeats sent\n";
            out += "# TYPE vccdb_sse_heartbeats_total counter\n";
            out += "vccdb_sse_heartbeats_total " + std::to_string(sstats.total_heartbeats_sent) + "\n";

            out += "# HELP vccdb_sse_dropped_events_total Total buffered SSE events dropped due to backpressure\n";
            out += "# TYPE vccdb_sse_dropped_events_total counter\n";
            out += "vccdb_sse_dropped_events_total " + std::to_string(sstats.total_dropped_events) + "\n";
        }

        // Build plain text response with proper content-type
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "THEMIS/0.1.0");
        res.set(http::field::content_type, "text/plain; version=0.0.4");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(out);
    // Also apply governance headers to the metrics endpoint
    applyGovernanceHeaders(req, res);
        res.prepare_payload();
        return res;
    } catch (const std::exception& e) {
        error_count_.fetch_add(1, std::memory_order_relaxed);
        return makeErrorResponse(http::status::internal_server_error, std::string("metrics error: ") + e.what(), req);
    }
}

// Authorization helper: returns optional error response if unauthorized
std::optional<http::response<http::string_body>> HttpServer::requireScope(
    const http::request<http::string_body>& req,
    std::string_view scope
) {
    if (!auth_ || !auth_->isEnabled()) return std::nullopt; // No auth configured

    auto it = req.find(http::field::authorization);
    if (it == req.end()) {
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
    res.body() = R"({"error":"missing_authorization","message":"Missing Authorization header"})";
    applyGovernanceHeaders(req, res);
    res.prepare_payload();
        return res;
    }
    auto token = themis::AuthMiddleware::extractBearerToken(std::string_view(it->value().data(), it->value().size()));
    if (!token) {
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
    res.body() = R"({"error":"invalid_authorization","message":"Invalid Bearer token format"})";
    applyGovernanceHeaders(req, res);
    res.prepare_payload();
        return res;
    }
    auto ar = auth_->authorize(*token, scope);
    if (!ar.authorized) {
        http::response<http::string_body> res{http::status::forbidden, req.version()};
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
        std::string body = std::string("{\"error\":\"forbidden\",\"message\":\"") + ar.reason + "\"}";
    res.body() = std::move(body);
    applyGovernanceHeaders(req, res);
    res.prepare_payload();
        return res;
    }
    return std::nullopt;
}

// Combined scope + policy authorization
std::optional<http::response<http::string_body>> HttpServer::requireAccess(
    const http::request<http::string_body>& req,
    std::string_view required_scope,
    std::string_view action,
    std::string_view resource_path
) {
    // If auth is disabled and no policy engine configured, allow
    bool auth_enabled = (auth_ && auth_->isEnabled());
    bool policy_enabled = (policy_engine_ != nullptr);
    if (!auth_enabled && !policy_enabled) return std::nullopt;

    // Normalize resource path (strip query string) if empty passed
    std::string resource = std::string(resource_path);
    if (resource.empty()) {
        resource = std::string(req.target());
    }
    auto qpos = resource.find('?');
    if (qpos != std::string::npos) resource = resource.substr(0, qpos);

    // 1) Scope-based authorization (if auth enabled)
    std::string user_id = "";
    if (auth_enabled) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) {
            http::response<http::string_body> res{http::status::unauthorized, req.version()};
            res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"error":"missing_authorization","message":"Missing Authorization header"})";
            applyGovernanceHeaders(req, res);
            res.prepare_payload();
            return res;
        }
        // Log Authorization header presence for this DELETE request
        try {
            std::string auth_hdr = std::string(it->value());
            auto mask = [](const std::string& s) {
                if (s.size() <= 8) return s;
                return s.substr(0,4) + "..." + s.substr(s.size()-4);
            };
            THEMIS_INFO("handlePiiDeleteByUuid: Authorization header='{}'", mask(auth_hdr));
        } catch (...) {}
        auto token = themis::AuthMiddleware::extractBearerToken(std::string_view(it->value().data(), it->value().size()));
        // Log presence of Authorization header for debugging (mask token)
        try {
            std::string auth_hdr = std::string(it->value());
            auto mask = [](const std::string& s) {
                if (s.size() <= 8) return s;
                return s.substr(0,4) + "..." + s.substr(s.size()-4);
            };
            THEMIS_INFO("PII DELETE: Authorization header present: '{}'", mask(auth_hdr));
        } catch (...) {}
        if (!token) {
            http::response<http::string_body> res{http::status::unauthorized, req.version()};
            res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"error":"invalid_authorization","message":"Invalid Bearer token format"})";
            applyGovernanceHeaders(req, res);
            res.prepare_payload();
            return res;
        }
            // Diagnostic: validate token to see which user_id (if any) is associated
            try {
                auto vres = auth_->validateToken(*token);
                THEMIS_INFO("requireAccess: validateToken -> authorized={} user_id='{}' reason='{}'", vres.authorized, vres.user_id, vres.reason);
                try {
                    std::cerr << "[AUTH-DBG] validateToken -> authorized=" << (vres.authorized?"true":"false")
                              << " user_id='" << vres.user_id << "' reason='" << vres.reason << "'\n";
                } catch(...) {}
            } catch (...) {}
            auto ar = auth_->authorize(*token, required_scope);
            try {
                std::cerr << "[AUTH-DBG] authorize -> authorized=" << (ar.authorized?"true":"false")
                          << " user_id='" << ar.user_id << "' reason='" << ar.reason << "'\n";
            } catch(...) {}
        if (!ar.authorized) {
            http::response<http::string_body> res{http::status::forbidden, req.version()};
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            std::string body = std::string("{\"error\":\"forbidden\",\"message\":\"") + ar.reason + "\"}";
            res.body() = std::move(body);
            applyGovernanceHeaders(req, res);
            res.prepare_payload();
            return res;
        }
        user_id = ar.user_id;
    }

    // 2) Policy evaluation (if enabled)
    if (policy_enabled) {
        // In unauthenticated mode (no tokens configured), treat PolicyEngine as advisory only.
        // Do not enforce denials to keep developer/test ergonomics unless auth is enabled.
        if (!auth_enabled) {
            return std::nullopt;
        }
        // Admin users bypass policy checks by design
        if (!user_id.empty() && user_id == "admin") {
            THEMIS_INFO("Policy check bypass for admin user_id='{}'", user_id);
            return std::nullopt;
        }
        // Diagnostic: show user_id before policy check
        try {
            std::cerr << "[AUTH-DBG] before_policy_check -> user_id='" << user_id << "' action='" << action << "' resource='" << resource << "'\n";
        } catch(...) {}

        // Extract client IP from headers (X-Forwarded-For or X-Real-IP)
        std::optional<std::string> client_ip;
        for (const auto& h : req) {
            auto name = h.name_string();
            if (beast::iequals(name, "x-forwarded-for")) {
                std::string v = std::string(h.value());
                // take first value before ','
                auto comma = v.find(',');
                if (comma != std::string::npos) v = v.substr(0, comma);
                // trim spaces
                auto start = v.find_first_not_of(" \t");
                auto end = v.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    v = v.substr(start, end - start + 1);
                    client_ip = v;
                }
                break;
            } else if (beast::iequals(name, "x-real-ip")) {
                std::string v = std::string(h.value());
                client_ip = v;
            }
        }

        THEMIS_INFO("PolicyEngine: evaluating user='{}' action='{}' resource='{}' client_ip='{}'", user_id, action, resource, client_ip.has_value() ? *client_ip : std::string("<none>"));
        auto decision = policy_engine_->authorize(user_id, std::string(action), resource, client_ip);
        THEMIS_INFO("PolicyEngine: decision.allowed={} reason='{}' policy_id='{}'", decision.allowed, decision.reason, decision.policy_id);
        if (!decision.allowed) {
            http::response<http::string_body> res{http::status::forbidden, req.version()};
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            nlohmann::json j = {{"error","policy_denied"},{"message",decision.reason}};
            if (!decision.policy_id.empty()) j["policy_id"] = decision.policy_id;
            res.body() = j.dump();
            applyGovernanceHeaders(req, res);
            res.prepare_payload();
            return res;
        }
    }

    return std::nullopt;
}

HttpServer::AuthContext HttpServer::extractAuthContext(const http::request<http::string_body>& req) const {
    AuthContext ctx;
    
    // If auth is disabled, return empty context
    if (!auth_ || !auth_->isEnabled()) {
        return ctx;
    }
    
    // Extract Authorization header
    auto it = req.find(http::field::authorization);
    if (it == req.end()) {
        return ctx; // No token -> empty context
    }
    
    // Extract Bearer token
    auto token = themis::AuthMiddleware::extractBearerToken(
        std::string_view(it->value().data(), it->value().size())
    );
    if (!token) {
        return ctx; // Invalid token format -> empty context
    }
    
    // Validate token and extract user_id + groups
    auto ar = auth_->validateToken(*token);
    if (ar.authorized) {
        ctx.user_id = ar.user_id;
        ctx.groups = ar.groups;
    }
    
    return ctx;
}
http::response<http::string_body> HttpServer::handlePiiRevealByUuid(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handlePiiRevealByUuid");
    span.setAttribute("http.path", "/pii/reveal/{uuid}");

    // Ensure pseudonymizer is available (lazy init) - check early before auth/policy
    try {
        ensurePIIPseudonymizer();
    } catch (const std::exception& ex) {
        return makeErrorResponse(http::status::service_unavailable, 
            std::string("PII service initialization failed: ") + ex.what(), req);
    }
    
    if (!pii_pseudonymizer_) {
        return makeErrorResponse(http::status::service_unavailable, "PII service not initialized", req);
    }

    // Extract UUID from path
    std::string target = std::string(req.target());
    std::string path_only = target;
    auto qpos = path_only.find('?');
    if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
    const std::string prefix = "/pii/reveal/";
    if (path_only.rfind(prefix, 0) != 0) {
        return makeErrorResponse(http::status::bad_request, "Invalid path", req);
    }
    std::string uuid = path_only.substr(prefix.size());
    if (uuid.empty()) {
        return makeErrorResponse(http::status::bad_request, "Missing UUID", req);
    }

    // Authorization: allow tokens with scope 'pii:reveal' OR 'admin'
    std::string user_id = "";
    if (auth_ && auth_->isEnabled()) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) {
            http::response<http::string_body> res{http::status::unauthorized, req.version()};
            res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"error":"missing_authorization","message":"Missing Authorization header"})";
            applyGovernanceHeaders(req, res);
            res.prepare_payload();
            return res;
        }
        auto token = themis::AuthMiddleware::extractBearerToken(std::string_view(it->value().data(), it->value().size()));
        if (!token) {
            http::response<http::string_body> res{http::status::unauthorized, req.version()};
            res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"error":"invalid_authorization","message":"Invalid Bearer token format"})";
            applyGovernanceHeaders(req, res);
            res.prepare_payload();
            return res;
        }
        auto ar = auth_->authorize(*token, "pii:reveal");
        if (!ar.authorized) {
            ar = auth_->authorize(*token, "admin");
            if (!ar.authorized) {
                http::response<http::string_body> res{http::status::forbidden, req.version()};
                res.set(http::field::content_type, "application/json");
                res.keep_alive(req.keep_alive());
                std::string body = std::string("{\"error\":\"forbidden\",\"message\":\"") + ar.reason + "\"}";
                res.body() = std::move(body);
                applyGovernanceHeaders(req, res);
                res.prepare_payload();
                return res;
            }
        }
        user_id = ar.user_id;
    }

    // Policy check (if configured)
    if (policy_engine_) {
        // Extract client IP (X-Forwarded-For or X-Real-IP)
        std::optional<std::string> client_ip;
        for (const auto& h : req) {
            auto name = h.name_string();
            if (beast::iequals(name, "x-forwarded-for")) {
                std::string v = std::string(h.value());
                auto comma = v.find(',');
                if (comma != std::string::npos) v = v.substr(0, comma);
                auto s = v.find_first_not_of(" \t");
                auto e = v.find_last_not_of(" \t");
                if (s != std::string::npos) client_ip = v.substr(s, e - s + 1);
                break;
            } else if (beast::iequals(name, "x-real-ip")) {
                client_ip = std::string(h.value());
            }
        }
        // Admin bypass: admin token should be able to perform PII operations regardless of policies
        if (!user_id.empty() && user_id == "admin") {
            THEMIS_INFO("PII DELETE: bypassing PolicyEngine for admin user_id='{}'", user_id);
        } else {
            auto decision = policy_engine_->authorize(user_id, "pii.write", path_only, client_ip);
            if (!decision.allowed) {
            http::response<http::string_body> res{http::status::forbidden, req.version()};
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            nlohmann::json j = {
                {"error", "policy_denied"},
                {"message", decision.reason},
            };
            if (!decision.policy_id.empty()) j["policy_id"] = decision.policy_id;
            res.body() = j.dump();
            applyGovernanceHeaders(req, res);
            res.prepare_payload();
            return res;
        }
        }
    }

    // Reveal
    auto value_opt = pii_pseudonymizer_->revealPII(uuid, user_id.empty() ? std::string("unknown") : user_id);
    if (!value_opt) {
        return makeErrorResponse(http::status::not_found, "PII mapping not found", req);
    }
    nlohmann::json resp{{"uuid", uuid}, {"value", *value_opt}};
    span.setStatus(true);
    return makeResponse(http::status::ok, resp.dump(), req);
}

http::response<http::string_body> HttpServer::handlePiiDeleteByUuid(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handlePiiDeleteByUuid");
    span.setAttribute("http.path", "/pii/{uuid}");

    // Extract path and uuid
    std::string target = std::string(req.target());
    auto qpos = target.find('?');
    std::string path_only = (qpos == std::string::npos) ? target : target.substr(0, qpos);
    std::string query = (qpos == std::string::npos) ? std::string() : target.substr(qpos + 1);
    const std::string prefix = "/pii/";
    if (path_only.rfind(prefix, 0) != 0) {
        return makeErrorResponse(http::status::bad_request, "Invalid path", req);
    }
    std::string uuid = path_only.substr(prefix.size());
    if (uuid.empty()) {
        return makeErrorResponse(http::status::bad_request, "Missing UUID", req);
    }

    // Parse mode from query (?mode=soft|hard), default soft (applies to pseudonymizer fallback)
    std::string mode = "soft";
    if (!query.empty()) {
        auto pos = query.find("mode=");
        if (pos != std::string::npos) {
            auto val = query.substr(pos + 5);
            auto amp = val.find('&'); if (amp != std::string::npos) val = val.substr(0, amp);
            if (val == "hard") mode = "hard";
        }
    }

    // Authorization: require pii:write or admin (erase is a write operation)
    std::string user_id;
    if (auth_ && auth_->isEnabled()) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) {
            http::response<http::string_body> res{http::status::unauthorized, req.version()};
            res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"error":"missing_authorization","message":"Missing Authorization header"})";
            res.prepare_payload();
            return res;
        }
        auto token = themis::AuthMiddleware::extractBearerToken(std::string_view(it->value().data(), it->value().size()));
        if (!token) {
            http::response<http::string_body> res{http::status::unauthorized, req.version()};
            res.set(http::field::www_authenticate, "Bearer realm=\"themis\"");
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"error":"invalid_authorization","message":"Invalid Bearer token format"})";
            res.prepare_payload();
            return res;
        }
        // Diagnostic: mask token and log authorize attempts
        auto mask = [](std::string_view t) {
            std::string s(t);
            if (s.size() <= 8) return s;
            return s.substr(0,4) + std::string("...") + s.substr(s.size()-4);
        };
        THEMIS_INFO("PII Delete: Authorization header present, token='{}', required_scope='pii:write'", mask(*token));
        
        auto ar = auth_->authorize(*token, "pii:write");
        THEMIS_INFO("PII Delete: authorize('pii:write') -> authorized={} user='{}' reason='{}'", ar.authorized, ar.user_id, ar.reason);
        if (!ar.authorized) {
            THEMIS_INFO("PII Delete: trying fallback authorize('admin') for token='{}'", mask(*token));
            ar = auth_->authorize(*token, "admin");
            THEMIS_INFO("PII Delete: authorize('admin') -> authorized={} user='{}' reason='{}'", ar.authorized, ar.user_id, ar.reason);
            if (!ar.authorized) {
                http::response<http::string_body> res{http::status::forbidden, req.version()};
                res.set(http::field::content_type, "application/json");
                res.keep_alive(req.keep_alive());
                std::string body = std::string("{\"error\":\"forbidden\",\"message\":\"") + ar.reason + "\"}";
                res.body() = std::move(body);
                applyGovernanceHeaders(req, res);
                res.prepare_payload();
                return res;
            }
        }
        user_id = ar.user_id;
    }

    // Policy check
    if (policy_engine_) {
        std::optional<std::string> client_ip;
        for (const auto& h : req) {
            auto name = h.name_string();
            if (beast::iequals(name, "x-forwarded-for")) {
                std::string v = std::string(h.value());
                auto comma = v.find(',');
                if (comma != std::string::npos) v = v.substr(0, comma);
                auto s = v.find_first_not_of(" \t");
                auto e = v.find_last_not_of(" \t");
                if (s != std::string::npos) client_ip = v.substr(s, e - s + 1);
                break;
            } else if (beast::iequals(name, "x-real-ip")) {
                client_ip = std::string(h.value());
            }
        }
    // Policy action aligned to write semantics
        auto decision = policy_engine_->authorize(user_id, "pii.write", path_only, client_ip);
        if (!decision.allowed) {
            http::response<http::string_body> res{http::status::forbidden, req.version()};
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            nlohmann::json j = {{"error","policy_denied"},{"message",decision.reason}};
            if (!decision.policy_id.empty()) j["policy_id"] = decision.policy_id;
            res.body() = j.dump();
            res.prepare_payload();
            return res;
        }
    }
    // Prefer CRUD mapping deletion when PII Manager feature is enabled
    if (config_.feature_pii_manager && pii_api_) {
        bool ok = pii_api_->deleteMapping(uuid);
        nlohmann::json resp = {{"status", ok ? "deleted" : "not_found"}, {"uuid", uuid}};
        return makeResponse(http::status::ok, resp.dump(), req);
    }

    // Fallback to pseudonymizer erase/soft-delete
    try {
        ensurePIIPseudonymizer();
    } catch (const std::exception& ex) {
        return makeErrorResponse(http::status::service_unavailable, 
            std::string("PII service initialization failed: ") + ex.what(), req);
    }
    if (!pii_pseudonymizer_) {
        return makeErrorResponse(http::status::service_unavailable, "PII service not initialized", req);
    }

    nlohmann::json resp;
    if (mode == "hard") {
        bool ok = pii_pseudonymizer_->erasePII(uuid);
        resp = {{"status", ok ? "ok" : "not_found"}, {"mode", "hard"}, {"uuid", uuid}, {"deleted", ok}};
    } else {
        bool ok = pii_pseudonymizer_->softDeletePII(uuid, user_id.empty() ? std::string("unknown") : user_id);
        resp = {{"status", ok ? "ok" : "not_found"}, {"mode", "soft"}, {"uuid", uuid}, {"updated", ok}};
    }
    return makeResponse(http::status::ok, resp.dump(), req);
}

http::response<http::string_body> HttpServer::handlePiiListMappings(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_pii_manager || !pii_api_) {
        return makeErrorResponse(http::status::not_found, "Feature 'pii_manager' disabled", req);
    }

    // Authorization: require scope pii:read or admin
    if (auto unauth = requireScope(req, "pii:read"); unauth.has_value()) return *unauth;

    // Parse query params
    std::string target = std::string(req.target());
    auto qpos = target.find('?');
    std::string query = (qpos == std::string::npos) ? std::string() : target.substr(qpos + 1);
    auto getParam = [&](const std::string& key) -> std::string {
        auto pos = query.find(key + "=");
        if (pos == std::string::npos) return {};
        auto val = query.substr(pos + key.size() + 1);
        auto amp = val.find('&');
        if (amp != std::string::npos) val = val.substr(0, amp);
        return val;
    };
    themis::server::PiiQueryFilter filter;
    filter.original_uuid = getParam("original_uuid");
    filter.pseudonym = getParam("pseudonym");
    filter.active_only = (getParam("active_only") == "true");
    try {
        if (!getParam("page").empty()) filter.page = std::stoi(getParam("page"));
        if (!getParam("page_size").empty()) filter.page_size = std::stoi(getParam("page_size"));
    } catch (...) {}

    auto js = pii_api_->listMappings(filter);
    return makeResponse(http::status::ok, js.dump(), req);
}

http::response<http::string_body> HttpServer::handlePiiCreateMapping(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_pii_manager || !pii_api_) {
        return makeErrorResponse(http::status::not_found, "Feature 'pii_manager' disabled", req);
    }
    if (req.method() != http::verb::post) {
        return makeErrorResponse(http::status::method_not_allowed, "Method not allowed", req);
    }
    if (auto unauth = requireScope(req, "pii:write"); unauth.has_value()) return *unauth;
    try {
        nlohmann::json body = nlohmann::json::parse(req.body());
        if (!body.contains("original_uuid") || !body.contains("pseudonym")) {
            return makeErrorResponse(http::status::bad_request, "Missing fields 'original_uuid' or 'pseudonym'", req);
        }
        themis::server::PiiMapping m;
        m.original_uuid = body["original_uuid"].get<std::string>();
        m.pseudonym = body["pseudonym"].get<std::string>();
        m.active = body.value("active", true);
        if (!pii_api_->addMapping(m)) {
            return makeErrorResponse(http::status::conflict, "Mapping already exists", req);
        }
        return makeResponse(http::status::created, m.toJson().dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("JSON error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePiiGetByUuid(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_pii_manager || !pii_api_) {
        return makeErrorResponse(http::status::not_found, "Feature 'pii_manager' disabled", req);
    }
    if (auto unauth = requireScope(req, "pii:read"); unauth.has_value()) return *unauth;
    std::string target = std::string(req.target());
    auto qpos = target.find('?');
    std::string path_only = (qpos == std::string::npos) ? target : target.substr(0, qpos);
    const std::string prefix = "/pii/";
    if (path_only.rfind(prefix, 0) != 0) {
        return makeErrorResponse(http::status::bad_request, "Invalid path", req);
    }
    std::string uuid = path_only.substr(prefix.size());
    if (uuid.empty() || uuid == "export.csv" || uuid == "reveal") {
        return makeErrorResponse(http::status::bad_request, "Invalid UUID", req);
    }
    auto m = pii_api_->getMapping(uuid);
    if (!m) return makeErrorResponse(http::status::not_found, "PII mapping not found", req);
    return makeResponse(http::status::ok, m->toJson().dump(), req);
}

http::response<http::string_body> HttpServer::handlePiiExportCsv(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_pii_manager || !pii_api_) {
        return makeErrorResponse(http::status::not_found, "Feature 'pii_manager' disabled", req);
    }
    if (auto unauth = requireScope(req, "pii:read"); unauth.has_value()) return *unauth;
    // Reuse list parsing
    std::string target = std::string(req.target());
    auto qpos = target.find('?');
    std::string query = (qpos == std::string::npos) ? std::string() : target.substr(qpos + 1);
    auto getParam = [&](const std::string& key) -> std::string {
        auto pos = query.find(key + "=");
        if (pos == std::string::npos) return {};
        auto val = query.substr(pos + key.size() + 1);
        auto amp = val.find('&');
        if (amp != std::string::npos) val = val.substr(0, amp);
        return val;
    };
    themis::server::PiiQueryFilter filter;
    filter.original_uuid = getParam("original_uuid");
    filter.pseudonym = getParam("pseudonym");
    filter.active_only = (getParam("active_only") == "true");
    try {
        if (!getParam("page").empty()) filter.page = std::stoi(getParam("page"));
        if (!getParam("page_size").empty()) filter.page_size = std::stoi(getParam("page_size"));
    } catch (...) {}

    std::string csv = pii_api_->exportCsv(filter);
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::content_type, "text/csv; charset=utf-8");
    res.keep_alive(req.keep_alive());
    res.body() = std::move(csv);
    applyGovernanceHeaders(req, res);
    res.prepare_payload();
    return res;
}
http::response<http::string_body> HttpServer::handleCacheQuery(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_semantic_cache) {
        return makeErrorResponse(http::status::not_found, "Feature 'semantic_cache' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleCacheQuery");
    span.setAttribute("http.path", "/cache/query");
    
    if (!semantic_cache_) {
        span.setStatus(false, "cache_not_initialized");
        return makeErrorResponse(http::status::internal_server_error, "Semantic cache not initialized", req);
    }
    
    try {
        nlohmann::json body = nlohmann::json::parse(req.body());
        
        if (!body.contains("prompt")) {
            span.setStatus(false, "missing_prompt");
            return makeErrorResponse(http::status::bad_request, "Missing 'prompt' field", req);
        }
        
        std::string prompt = body["prompt"].get<std::string>();
        nlohmann::json params = body.value("params", nlohmann::json::object());
        
        span.setAttribute("prompt.length", static_cast<int64_t>(prompt.size()));
        
        auto result = semantic_cache_->query(prompt, params);
        
        if (result) {
            // Cache hit
            span.setAttribute("cache.hit", true);
            nlohmann::json response = {
                {"hit", true},
                {"response", result->response},
                {"metadata", result->metadata},
                {"timestamp_ms", result->timestamp_ms}
            };
            span.setStatus(true);
            return makeResponse(http::status::ok, response.dump(), req);
        } else {
            // Cache miss
            span.setAttribute("cache.hit", false);
            nlohmann::json response = {
                {"hit", false}
            };
            span.setStatus(true);
            return makeResponse(http::status::ok, response.dump(), req);
        }
        
    } catch (const nlohmann::json::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "json_parse_error");
        return makeErrorResponse(http::status::bad_request, std::string("JSON error: ") + e.what(), req);
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleCachePut(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_semantic_cache) {
        return makeErrorResponse(http::status::not_found, "Feature 'semantic_cache' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleCachePut");
    span.setAttribute("http.path", "/cache/put");
    
    if (!semantic_cache_) {
        span.setStatus(false, "cache_not_initialized");
        return makeErrorResponse(http::status::internal_server_error, "Semantic cache not initialized", req);
    }
    
    try {
        nlohmann::json body = nlohmann::json::parse(req.body());
        
        if (!body.contains("prompt") || !body.contains("response")) {
            span.setStatus(false, "missing_fields");
            return makeErrorResponse(http::status::bad_request, "Missing 'prompt' or 'response' field", req);
        }
        
        std::string prompt = body["prompt"].get<std::string>();
        std::string response = body["response"].get<std::string>();
        nlohmann::json params = body.value("params", nlohmann::json::object());
        nlohmann::json metadata = body.value("metadata", nlohmann::json::object());
        int ttl_seconds = body.value("ttl_seconds", 0);
        
        span.setAttribute("prompt.length", static_cast<int64_t>(prompt.size()));
        span.setAttribute("response.length", static_cast<int64_t>(response.size()));
        
        bool success = semantic_cache_->put(prompt, params, response, metadata, ttl_seconds);
        
        if (success) {
            nlohmann::json result = {
                {"success", true},
                {"message", "Response cached successfully"}
            };
            span.setStatus(true);
            return makeResponse(http::status::ok, result.dump(), req);
        } else {
            span.setStatus(false, "cache_put_failed");
            return makeErrorResponse(http::status::internal_server_error, "Failed to cache response", req);
        }
        
    } catch (const nlohmann::json::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "json_parse_error");
        return makeErrorResponse(http::status::bad_request, std::string("JSON error: ") + e.what(), req);
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleCacheStats(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_semantic_cache) {
        return makeErrorResponse(http::status::not_found, "Feature 'semantic_cache' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleCacheStats");
    span.setAttribute("http.path", "/cache/stats");
    
    if (!semantic_cache_) {
        span.setStatus(false, "cache_not_initialized");
        return makeErrorResponse(http::status::internal_server_error, "Semantic cache not initialized", req);
    }
    
    try {
        auto stats = semantic_cache_->getStats();
        nlohmann::json response = stats.toJson();
        
        span.setAttribute("cache.hit_count", static_cast<int64_t>(stats.hit_count));
        span.setAttribute("cache.miss_count", static_cast<int64_t>(stats.miss_count));
        span.setAttribute("cache.hit_rate", stats.hit_rate);
        
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleLlmInteractionPost(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_llm_store) {
        return makeErrorResponse(http::status::not_found, "Feature 'llm_store' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleLlmInteractionPost");
    span.setAttribute("http.path", "/llm/interaction");
    
    try {
        // Parse request body
        auto body_json = json::parse(req.body());
        
        // Build interaction from JSON
        LLMInteractionStore::Interaction interaction;
        interaction.prompt_template_id = body_json.value("prompt_template_id", "");
        interaction.prompt = body_json.value("prompt", "");
        
        if (body_json.contains("reasoning_chain") && body_json["reasoning_chain"].is_array()) {
            interaction.reasoning_chain = body_json["reasoning_chain"].get<std::vector<std::string>>();
        }
        
        interaction.response = body_json.value("response", "");
        interaction.model_version = body_json.value("model_version", "");
        interaction.latency_ms = body_json.value("latency_ms", 0);
        interaction.token_count = body_json.value("token_count", 0);
        
        if (body_json.contains("metadata")) {
            interaction.metadata = body_json["metadata"];
        }
        
        // Store interaction (ID and timestamp will be generated)
        auto stored = llm_store_->createInteraction(interaction);
        
        // Build response
        json response;
        response["success"] = true;
        response["interaction"] = stored.toJson();
        
        span.setAttribute("interaction.id", stored.id);
        span.setAttribute("interaction.tokens", static_cast<int64_t>(stored.token_count));
        span.setStatus(true);
        
        return makeResponse(http::status::created, response.dump(), req);
        
    } catch (const json::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "json_parse_error");
        return makeErrorResponse(http::status::bad_request, std::string("JSON error: ") + e.what(), req);
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleLlmInteractionList(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_llm_store) {
        return makeErrorResponse(http::status::not_found, "Feature 'llm_store' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleLlmInteractionList");
    span.setAttribute("http.path", "/llm/interaction");
    
    try {
        // Parse query parameters
        LLMInteractionStore::ListOptions options;
        
        // Simple query param parsing (can be enhanced with proper URL parsing library)
        std::string target = std::string(req.target());
        size_t query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            std::string query_str = target.substr(query_pos + 1);
            
            // Parse limit
            size_t limit_pos = query_str.find("limit=");
            if (limit_pos != std::string::npos) {
                size_t limit_end = query_str.find('&', limit_pos);
                std::string limit_str = query_str.substr(limit_pos + 6, 
                    limit_end == std::string::npos ? std::string::npos : limit_end - limit_pos - 6);
                options.limit = std::stoull(limit_str);
            }
            
            // Parse start_after
            size_t start_pos = query_str.find("start_after=");
            if (start_pos != std::string::npos) {
                size_t start_end = query_str.find('&', start_pos);
                std::string start_id = query_str.substr(start_pos + 12,
                    start_end == std::string::npos ? std::string::npos : start_end - start_pos - 12);
                options.start_after_id = start_id;
            }
            
            // Parse filter_model
            size_t model_pos = query_str.find("model=");
            if (model_pos != std::string::npos) {
                size_t model_end = query_str.find('&', model_pos);
                std::string model = query_str.substr(model_pos + 6,
                    model_end == std::string::npos ? std::string::npos : model_end - model_pos - 6);
                options.filter_model = model;
            }
        }
        
        // List interactions
        auto interactions = llm_store_->listInteractions(options);
        
        // Build response
        json response;
        response["interactions"] = json::array();
        for (const auto& interaction : interactions) {
            response["interactions"].push_back(interaction.toJson());
        }
        response["count"] = interactions.size();
        
        span.setAttribute("interaction.count", static_cast<int64_t>(interactions.size()));
        span.setStatus(true);
        
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleLlmInteractionGet(
    const http::request<http::string_body>& req
) {
    if (!config_.feature_llm_store) {
        return makeErrorResponse(http::status::not_found, "Feature 'llm_store' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleLlmInteractionGet");
    span.setAttribute("http.path", "/llm/interaction/:id");
    
    try {
        // Extract ID from path: /llm/interaction/{id}
        std::string id = extractPathParam(std::string(req.target()), "/llm/interaction/");
        
        if (id.empty()) {
            span.setStatus(false, "missing_id");
            return makeErrorResponse(http::status::bad_request, "Missing interaction ID", req);
        }
        
        span.setAttribute("interaction.id", id);
        
        // Get interaction
        auto interaction_opt = llm_store_->getInteraction(id);
        
        if (!interaction_opt.has_value()) {
            span.setStatus(false, "not_found");
            return makeErrorResponse(http::status::not_found, "Interaction not found", req);
        }
        
        // Build response
        json response = interaction_opt->toJson();
        
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleChangefeedGet(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "cdc:read", "cdc.read", path_only)) return *resp;
    }
    if (!config_.feature_cdc) {
        return makeErrorResponse(http::status::not_found, "Feature 'cdc' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleChangefeedGet");
    span.setAttribute("http.path", "/changefeed");
    
    try {
        // Parse query parameters
        Changefeed::ListOptions options;
        
        std::string target = std::string(req.target());
        size_t query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            std::string query_str = target.substr(query_pos + 1);
            
            // Parse from_seq
            size_t from_pos = query_str.find("from_seq=");
            if (from_pos != std::string::npos) {
                size_t from_end = query_str.find('&', from_pos);
                std::string from_str = query_str.substr(from_pos + 9,
                    from_end == std::string::npos ? std::string::npos : from_end - from_pos - 9);
                options.from_sequence = std::stoull(from_str);
            }
            
            // Parse limit
            size_t limit_pos = query_str.find("limit=");
            if (limit_pos != std::string::npos) {
                size_t limit_end = query_str.find('&', limit_pos);
                std::string limit_str = query_str.substr(limit_pos + 6,
                    limit_end == std::string::npos ? std::string::npos : limit_end - limit_pos - 6);
                options.limit = std::stoull(limit_str);
            }
            
            // Parse long_poll_ms
            size_t poll_pos = query_str.find("long_poll_ms=");
            if (poll_pos != std::string::npos) {
                size_t poll_end = query_str.find('&', poll_pos);
                std::string poll_str = query_str.substr(poll_pos + 13,
                    poll_end == std::string::npos ? std::string::npos : poll_end - poll_pos - 13);
                options.long_poll_ms = std::stoul(poll_str);
            }
            
            // Parse key_prefix
            size_t key_pos = query_str.find("key_prefix=");
            if (key_pos != std::string::npos) {
                size_t key_end = query_str.find('&', key_pos);
                std::string key_prefix = query_str.substr(key_pos + 11,
                    key_end == std::string::npos ? std::string::npos : key_end - key_pos - 11);
                options.key_prefix = key_prefix;
            }
        }
        
        // List events
        auto events = changefeed_->listEvents(options);
        
        // Build response
        json response;
        response["events"] = json::array();
        for (const auto& event : events) {
            response["events"].push_back(event.toJson());
        }
        response["count"] = events.size();
        response["latest_sequence"] = changefeed_->getLatestSequence();
        
        span.setAttribute("events.count", static_cast<int64_t>(events.size()));
        span.setAttribute("events.from_seq", static_cast<int64_t>(options.from_sequence));
        span.setStatus(true);
        
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleChangefeedStreamSse(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "cdc:read", "cdc.read", path_only)) return *resp;
    }
    if (!config_.feature_cdc) {
        return makeErrorResponse(http::status::not_found, "Feature 'cdc' disabled", req);
    }
    
    auto span = Tracer::startSpan("handleChangefeedStreamSse");
    span.setAttribute("http.path", "/changefeed/stream");
    
    try {
        // Parse query parameters
        uint64_t from_seq = 0;
        std::string key_prefix;
        bool keep_alive = true; // New parameter for production streaming
        int max_seconds = 30;   // Optional limit for testability
        int heartbeat_ms_override = -1; // Optional per-request heartbeat interval
    int retry_ms = 3000;
        size_t max_events_per_poll = 100; // Backpressure: limit events consumed per poll
        
        std::string target = std::string(req.target());
        size_t query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            std::string query_str = target.substr(query_pos + 1);
            
            // Parse from_seq
            size_t from_pos = query_str.find("from_seq=");
            if (from_pos != std::string::npos) {
                size_t from_end = query_str.find('&', from_pos);
                std::string from_str = query_str.substr(from_pos + 9,
                    from_end == std::string::npos ? std::string::npos : from_end - from_pos - 9);
                from_seq = std::stoull(from_str);
            }
            
            // Parse key_prefix
            size_t key_pos = query_str.find("key_prefix=");
            if (key_pos != std::string::npos) {
                size_t key_end = query_str.find('&', key_pos);
                key_prefix = query_str.substr(key_pos + 11,
                    key_end == std::string::npos ? std::string::npos : key_end - key_pos - 11);
            }
            
            // Parse keep_alive (default true for production)
            size_t ka_pos = query_str.find("keep_alive=");
            if (ka_pos != std::string::npos) {
                size_t ka_end = query_str.find('&', ka_pos);
                std::string ka_str = query_str.substr(ka_pos + 11,
                    ka_end == std::string::npos ? std::string::npos : ka_end - ka_pos - 11);
                keep_alive = (ka_str == "true" || ka_str == "1");
            }

            // Parse max_seconds (bounds 1..60)
            size_t ms_pos = query_str.find("max_seconds=");
            if (ms_pos != std::string::npos) {
                size_t ms_end = query_str.find('&', ms_pos);
                std::string ms_str = query_str.substr(ms_pos + 12,
                    ms_end == std::string::npos ? std::string::npos : ms_end - ms_pos - 12);
                try {
                    int v = std::stoi(ms_str);
                    if (v < 1) v = 1; if (v > 60) v = 60;
                    max_seconds = v;
                } catch (...) {
                    // ignore parse error, keep default
                }
            }

            // Parse heartbeat_ms (test override)
            size_t hb_pos = query_str.find("heartbeat_ms=");
            if (hb_pos != std::string::npos) {
                size_t hb_end = query_str.find('&', hb_pos);
                std::string hb_str = query_str.substr(hb_pos + 13,
                    hb_end == std::string::npos ? std::string::npos : hb_end - hb_pos - 13);
                try {
                    int v = std::stoi(hb_str);
                    if (v < 100) v = 100; // minimum 100ms
                    if (v > 60000) v = 60000;
                    heartbeat_ms_override = v;
                } catch (...) {
                    // ignore parse error
                }
            }

            // Parse retry_ms
            size_t r_pos = query_str.find("retry_ms=");
            if (r_pos != std::string::npos) {
                size_t r_end = query_str.find('&', r_pos);
                std::string r_str = query_str.substr(r_pos + 9,
                    r_end == std::string::npos ? std::string::npos : r_end - r_pos - 9);
                try {
                    int v = std::stoi(r_str);
                    if (v < 100) v = 100; if (v > 120000) v = 120000;
                    retry_ms = v;
                } catch (...) {}
            }

            // Parse max_events_per_poll
            size_t me_pos = query_str.find("max_events=");
            if (me_pos != std::string::npos) {
                size_t me_end = query_str.find('&', me_pos);
                std::string me_str = query_str.substr(me_pos + 11,
                    me_end == std::string::npos ? std::string::npos : me_end - me_pos - 11);
                try {
                    int v = std::stoi(me_str);
                    if (v < 1) v = 1; if (v > 1000) v = 1000;
                    max_events_per_poll = static_cast<size_t>(v);
                } catch (...) {}
            }
        }

        // Support Last-Event-ID header for resume
        // Search case-insensitively
        for (const auto& h : req) {
            auto name = h.name_string();
            if (beast::iequals(name, "Last-Event-ID")) {
                try {
                    uint64_t last_id = std::stoull(std::string(h.value()));
                    if (from_seq == 0) from_seq = last_id;
                } catch (...) {}
                break;
            }
        }
        
        // Build SSE response
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "THEMIS/0.1.0");
    res.set(http::field::content_type, "text/event-stream");
    res.set(http::field::cache_control, "no-cache, no-transform");
        res.set(http::field::connection, "keep-alive");
    // Best-effort proxies
    res.set(http::field::access_control_allow_origin, "*");
        res.keep_alive(true);
        
        std::ostringstream body;
    // Advise client reconnect delay
    body << "retry: " << retry_ms << "\n\n";
        
        if (keep_alive && sse_manager_) {
            // Production mode: Register connection for streaming
            // Note: Current Beast setup limits us to batch-based streaming
            // Full keep-alive requires custom async write loop (see TODO in docs)
            
            uint64_t conn_id = sse_manager_->registerConnection(from_seq, key_prefix);
            span.setAttribute("sse.connection_id", static_cast<int64_t>(conn_id));
            
            // Stream events for limited duration (configurable for tests)
            auto start = std::chrono::steady_clock::now();
            const auto max_duration = std::chrono::seconds(max_seconds);
            size_t total_events = 0;
            size_t heartbeats = 0;
            
            auto last_hb = start;
            while (std::chrono::steady_clock::now() - start < max_duration) {
                // Poll for new events
                auto events = sse_manager_->pollEvents(conn_id, max_events_per_poll);
                
                if (!events.empty()) {
                    for (const auto& event_line : events) {
                        body << event_line;
                        total_events++;
                    }
                } else {
                    bool sent_hb = false;
                    if (heartbeat_ms_override > 0) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - last_hb
                        ).count();
                        if (elapsed >= heartbeat_ms_override) {
                            body << ": heartbeat\n\n";
                            sse_manager_->recordHeartbeat(conn_id);
                            heartbeats++;
                            last_hb = std::chrono::steady_clock::now();
                            sent_hb = true;
                        }
                    }
                    if (!sent_hb && sse_manager_->needsHeartbeat(conn_id)) {
                        body << ": heartbeat\n\n";
                        sse_manager_->recordHeartbeat(conn_id);
                        heartbeats++;
                    }
                }
                
                // Sleep briefly to avoid busy-wait
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Cleanup connection
            sse_manager_->unregisterConnection(conn_id);
            
            span.setAttribute("sse.total_events", static_cast<int64_t>(total_events));
            span.setAttribute("sse.heartbeats", static_cast<int64_t>(heartbeats));
            span.setAttribute("sse.duration_s", static_cast<int64_t>(max_seconds));
            
            THEMIS_INFO("SSE stream completed: conn={}, events={}, heartbeats={}",
                conn_id, total_events, heartbeats);
            
        } else {
            // MVP mode: Send one batch and close (backward compatible)
            Changefeed::ListOptions options;
            options.from_sequence = from_seq;
            options.limit = 1000;
            
            if (!key_prefix.empty()) {
                options.key_prefix = key_prefix;
            }
            
            auto events = changefeed_->listEvents(options);
            
            for (const auto& ev : events) {
                body << "id: " << ev.sequence << "\n";
                body << "data: " << ev.toJson().dump() << "\n\n";
            }
            
            if (events.empty()) {
                body << ": heartbeat\n\n";
            }
            
            span.setAttribute("sse.mode", "mvp_batch");
            span.setAttribute("events.count", static_cast<int64_t>(events.size()));
        }
        
    res.body() = body.str();
    // Apply governance headers also to SSE response
    applyGovernanceHeaders(req, res);
        res.prepare_payload();
        
        span.setStatus(true);
        return res;
        
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, std::string("Error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleChangefeedStats(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "cdc:admin", "cdc.admin", path_only)) return *resp;
    }
    if (!config_.feature_cdc) {
        return makeErrorResponse(http::status::not_found, "Feature 'cdc' disabled", req);
    }

    auto span = Tracer::startSpan("handleChangefeedStats");
    span.setAttribute("http.path", "/changefeed/stats");

    try {
        auto stats = changefeed_->getStats();
        nlohmann::json response = {
            {"total_events", stats.total_events},
            {"latest_sequence", stats.latest_sequence},
            {"total_size_bytes", stats.total_size_bytes}
        };
        span.setAttribute("events.total", static_cast<int64_t>(stats.total_events));
        span.setAttribute("events.latest_seq", static_cast<int64_t>(stats.latest_sequence));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleChangefeedRetention(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "cdc:admin", "cdc.admin", path_only)) return *resp;
    }
    if (!config_.feature_cdc) {
        return makeErrorResponse(http::status::not_found, "Feature 'cdc' disabled", req);
    }

    auto span = Tracer::startSpan("handleChangefeedRetention");
    span.setAttribute("http.path", "/changefeed/retention");

    try {
        auto body = nlohmann::json::parse(req.body());
        // Support either before_sequence (explicit) or max_age_ms (relative)
        uint64_t before_seq = 0;
        if (body.contains("before_sequence")) {
            before_seq = body["before_sequence"].get<uint64_t>();
        } else if (body.contains("max_age_ms")) {
            // Compute a cut based on timestamp: scan stats.latest_sequence backwards is expensive;
            // MVP: if max_age_ms is provided, require also current latest_sequence from client or ignore.
            // For simplicity, we ignore timestamp-based deletion in MVP and return 400 if latest not provided.
            return makeErrorResponse(http::status::bad_request, "Only 'before_sequence' is supported for retention in MVP", req);
        } else {
            return makeErrorResponse(http::status::bad_request, "Provide 'before_sequence' (uint64)", req);
        }

        span.setAttribute("retention.before_seq", static_cast<int64_t>(before_seq));
        auto deleted = changefeed_->deleteOldEvents(before_seq);
        nlohmann::json response = {
            {"deleted", deleted},
            {"before_sequence", before_seq}
        };
        span.setAttribute("retention.deleted", static_cast<int64_t>(deleted));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
    } catch (const nlohmann::json::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "json_parse_error");
        return makeErrorResponse(http::status::bad_request, std::string("JSON error: ") + e.what(), req);
    } catch (const std::exception& e) {
        span.recordError(e.what());
        span.setStatus(false, "internal_error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
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
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "data:read", "read", path_only)) return *resp;
    }
    auto span = Tracer::startSpan("GET /entities/:key");
    
    try {
        // Extract entity key from path: /entities/{key}
        auto key = extractPathParam(std::string(req.target()), "/entities/");
        if (key.empty()) {
            span.setStatus(false, "Missing entity key");
            return makeErrorResponse(http::status::bad_request, "Missing entity key", req);
        }

        span.setAttribute("entity.key", key);

        // Retrieve entity blob (persisted JSON string)
        auto blob_opt = storage_->get(key);
        if (!blob_opt.has_value()) {
            span.setStatus(false, "Entity not found");
            return makeErrorResponse(http::status::not_found, "Entity not found", req);
        }
        const auto& blob_vec = blob_opt.value();
        std::string blob_str(blob_vec.begin(), blob_vec.end());
        span.setAttribute("entity.size_bytes", static_cast<int64_t>(blob_str.size()));

        // Optional Entschluesselung via Query-Parameter ?decrypt=true
        bool decrypt = false;
        {
            std::string target = std::string(req.target());
            auto qpos = target.find('?');
            if (qpos != std::string::npos) {
                auto qs = target.substr(qpos + 1);
                std::istringstream iss(qs);
                std::string kv;
                while (std::getline(iss, kv, '&')) {
                    auto eq = kv.find('=');
                    std::string k = (eq == std::string::npos) ? kv : kv.substr(0, eq);
                    std::string v = (eq == std::string::npos) ? std::string() : kv.substr(eq + 1);
                    if (k == "decrypt") {
                        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                        decrypt = (v == "true" || v == "1" || v == "yes");
                    }
                }
            }
        }

        if (!decrypt) {
            span.setStatus(true);
            json response = {{"key", key}, {"blob", blob_str}};
            return makeResponse(http::status::ok, response.dump(), req);
        }

        // Entschluesselung nur wenn Schema konfiguriert ist und Felder markiert
        json entity_json;
        try { entity_json = json::parse(blob_str); } catch (...) {
            span.setStatus(false, "Stored blob is not valid JSON");
            return makeErrorResponse(http::status::internal_server_error, "Stored entity JSON parse failed", req);
        }

        // Tabelle aus Key extrahieren (table:pk Format)
        auto pos = key.find(':');
        if (pos == std::string::npos || pos == 0 || pos == key.size()-1) {
            span.setStatus(false, "Invalid key format");
            return makeErrorResponse(http::status::bad_request, "Key must be in format 'table:pk'", req);
        }
        std::string table = key.substr(0, pos);

        try {
            auto schema_bytes = storage_->get("config:encryption_schema");
            if (schema_bytes) {
                std::string schema_json(schema_bytes->begin(), schema_bytes->end());
                auto schema = nlohmann::json::parse(schema_json);
                if (schema.contains("collections") && schema["collections"].contains(table)) {
                    auto coll = schema["collections"][table];
                    if (coll.contains("encryption") && coll["encryption"].value("enabled", false)) {
                        std::string context_type = coll["encryption"].value("context_type", "user");
                        std::vector<std::string> fields;
                        if (coll["encryption"].contains("fields")) {
                            for (auto& f : coll["encryption"]["fields"]) if (f.is_string()) fields.push_back(f.get<std::string>());
                        }
                        // Extract user_id and groups from JWT for decryption context
                        auto auth_ctx = extractAuthContext(req);
                        std::string user_ctx = auth_ctx.user_id.empty() ? "anonymous" : auth_ctx.user_id;
                        auto pki = std::dynamic_pointer_cast<themis::security::PKIKeyProvider>(key_provider_);
                        for (const auto& f : fields) {
                            if (!entity_json.contains(f + "_enc") || !entity_json.contains(f + "_encrypted")) continue;
                            bool encFlag = false;
                            try { encFlag = entity_json[f + "_enc"].get<bool>(); } catch (...) { encFlag = false; }
                            if (!encFlag) continue;
                            try {
                                auto enc_meta_str = entity_json[f + "_encrypted"].get<std::string>();
                                auto enc_meta = nlohmann::json::parse(enc_meta_str);
                                auto blob = themis::EncryptedBlob::fromJson(enc_meta);
                                std::vector<uint8_t> raw_key;
                                if (context_type == "group" && pki && entity_json.contains(f + "_group")) {
                                    // Gruppen-Kontext (MVP: erste Gruppe / einzelner String)
                                    std::string group_name;
                                    try { group_name = entity_json[f + "_group"].get<std::string>(); } catch (...) { group_name.clear(); }
                                    if (!group_name.empty()) {
                                        auto gdek = pki->getGroupDEK(group_name);
                                        std::vector<uint8_t> salt; // leer
                                        std::string info = "field:" + f;
                                        raw_key = themis::utils::HKDFHelper::derive(gdek, salt, info, 32);
                                    }
                                }
                                if (raw_key.empty()) {
                                    // User-/Anonymous-Kontext
                                    auto dek = key_provider_->getKey("dek");
                                    std::vector<uint8_t> salt(user_ctx.begin(), user_ctx.end());
                                    std::string info = "field:" + f;
                                    raw_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
                                }
                                auto plain_bytes = field_encryption_->decryptWithKey(blob, raw_key);
                                
                                // Deserialisierung basierend auf Datenformat
                                // Versuche JSON-Deserialisierung für strukturierte Typen
                                std::string plain_str(plain_bytes.begin(), plain_bytes.end());
                                
                                // Heuristik: Wenn es wie JSON aussieht, parse es
                                if (!plain_str.empty() && (plain_str[0] == '[' || plain_str[0] == '{')) {
                                    try {
                                        auto parsed = nlohmann::json::parse(plain_str);
                                        entity_json[f] = parsed; // JSON-Struktur übernehmen
                                    } catch (...) {
                                        // Kein valides JSON → als String behandeln
                                        entity_json[f] = plain_str;
                                    }
                                } else {
                                    // Primitive Typen als String zurückgeben
                                    entity_json[f] = plain_str;
                                }
                            } catch (const std::exception& e) {
                                THEMIS_WARN("Entschluesselung Feld {} fehlgeschlagen: {}", f, e.what());
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            THEMIS_WARN("Decrypt schema Verarbeitung Fehler: {}", e.what());
        }

        span.setStatus(true);
        json response = {{"key", key}, {"decrypted", true}, {"entity", entity_json}};
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const std::exception& e) {
        THEMIS_ERROR("GET entity error: {}", e.what());
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePutEntity(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "data:write", "write", path_only)) return *resp;
    }
    auto span = Tracer::startSpan("PUT /entities/:key");
    
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
            span.setStatus(false, "Missing entity key");
            return makeErrorResponse(http::status::bad_request, "Missing entity key", req);
        }

        span.setAttribute("entity.key", key);

        if (!body_json.contains("blob")) {
            span.setStatus(false, "Missing blob field");
            return makeErrorResponse(http::status::bad_request, "Missing 'blob' field", req);
        }

        // Split key into table:pk
        auto pos = key.find(':');
        if (pos == std::string::npos || pos == 0 || pos == key.size()-1) {
            span.setStatus(false, "Invalid key format");
            return makeErrorResponse(http::status::bad_request, "Key must be in format 'table:pk'", req);
        }
        std::string table = key.substr(0, pos);
        std::string pk = key.substr(pos+1);

        span.setAttribute("entity.table", table);
        span.setAttribute("entity.pk", pk);

        // Build BaseEntity from blob (assume JSON payload string)
        std::string blob_str = body_json["blob"].get<std::string>();
        span.setAttribute("entity.size_bytes", static_cast<int64_t>(blob_str.size()));
        
        BaseEntity entity = BaseEntity::fromJson(pk, blob_str);

        // Schema-basierte Feldverschlüsselung (MVP): Falls eine Encryption-Schema-Config persistiert ist,
        // verschlüssele deklarierte Felder vor Index-/Storage-Persistenz.
        try {
            auto schema_bytes = storage_->get("config:encryption_schema");
            if (schema_bytes) {
                std::string schema_json(schema_bytes->begin(), schema_bytes->end());
                auto schema = nlohmann::json::parse(schema_json);
                if (schema.contains("collections") && schema["collections"].contains(table)) {
                    auto coll = schema["collections"][table];
                    if (coll.contains("encryption") && coll["encryption"].contains("enabled") && coll["encryption"]["enabled"].get<bool>()) {
                        // Erforderliche Komponenten prüfen
                        if (!field_encryption_) {
                            THEMIS_WARN("Encryption schema aktiv aber field_encryption_ fehlt");
                        } else if (!key_provider_) {
                            THEMIS_WARN("Encryption schema aktiv aber key_provider_ fehlt");
                        } else {
                            // Kontexttyp (user|group)
                            std::string context_type = coll["encryption"].value("context_type", "user");
                            std::vector<std::string> fields;
                            if (coll["encryption"].contains("fields")) {
                                for (auto& f : coll["encryption"]["fields"]) {
                                    if (f.is_string()) fields.push_back(f.get<std::string>());
                                }
                            }
                            // Extract user_id and groups from JWT token
                            auto auth_ctx = extractAuthContext(req);
                            std::string user_id = auth_ctx.user_id;
                            std::vector<std::string> groups_claim = auth_ctx.groups;
                            // Hole DEK / Group-DEK aus PKIKeyProvider (dynamic_cast für Group-Funktionalität)
                            auto pki = std::dynamic_pointer_cast<themis::security::PKIKeyProvider>(key_provider_);
                            for (const auto& f : fields) {
                                if (!entity.hasField(f)) continue; // Feld existiert nicht
                                auto valOpt = entity.getField(f);
                                if (!valOpt.has_value()) continue;
                                
                                // Serialisierung des Values für alle unterstützten Typen
                                std::vector<uint8_t> plain_bytes;
                                const auto& v = *valOpt;
                                
                                if (std::holds_alternative<std::string>(v)) {
                                    const auto& str = std::get<std::string>(v);
                                    plain_bytes.assign(str.begin(), str.end());
                                } else if (std::holds_alternative<int64_t>(v)) {
                                    std::string str = std::to_string(std::get<int64_t>(v));
                                    plain_bytes.assign(str.begin(), str.end());
                                } else if (std::holds_alternative<double>(v)) {
                                    std::string str = std::to_string(std::get<double>(v));
                                    plain_bytes.assign(str.begin(), str.end());
                                } else if (std::holds_alternative<bool>(v)) {
                                    std::string str = std::get<bool>(v) ? "true" : "false";
                                    plain_bytes.assign(str.begin(), str.end());
                                } else if (std::holds_alternative<std::vector<float>>(v)) {
                                    // Vector<float>: Serialize als JSON-Array
                                    const auto& vec = std::get<std::vector<float>>(v);
                                    nlohmann::json j_arr = nlohmann::json::array();
                                    for (float val : vec) j_arr.push_back(val);
                                    std::string json_str = j_arr.dump();
                                    plain_bytes.assign(json_str.begin(), json_str.end());
                                } else if (std::holds_alternative<std::vector<uint8_t>>(v)) {
                                    // Binary blob: direkt als Byte-Array
                                    plain_bytes = std::get<std::vector<uint8_t>>(v);
                                } else if (std::holds_alternative<std::monostate>(v)) {
                                    // Null-Wert überspringen
                                    continue;
                                } else {
                                    // Unbekannter Typ überspringen
                                    THEMIS_WARN("Feldverschlüsselung: Unbekannter Typ für Feld {}", f);
                                    continue;
                                }
                                
                                std::vector<uint8_t> raw_key;
                                std::string key_id;
                                if (context_type == "group" && pki && !groups_claim.empty()) {
                                    // Nehme erste Gruppe als Kontext (MVP)
                                    auto gdek = pki->getGroupDEK(groups_claim.front());
                                    // HKDF über gdek mit Info=field:<name>
                                    std::vector<uint8_t> salt; // leer
                                    std::string info = "field:" + f;
                                    raw_key = utils::HKDFHelper::derive(gdek, salt, info, 32);
                                    key_id = "group_field:" + f;
                                    entity.setField(f + "_group", groups_claim.front());
                                } else {
                                    // Per-User oder fallback auf allgemeines Feld-Key
                                    std::string user_ctx = user_id.empty() ? "anonymous" : user_id;
                                    auto dek = key_provider_->getKey("dek");
                                    // salt = user_id (kann leer sein) – falls leer, fallback auf statischen salt, um HKDF-Funktion stabil zu halten
                                    std::vector<uint8_t> salt;
                                    if (!user_ctx.empty()) salt.assign(user_ctx.begin(), user_ctx.end());
                                    std::string info = "field:" + f;
                                    raw_key = utils::HKDFHelper::derive(dek, salt, info, 32);
                                    key_id = "user_field:" + f;
                                }
                                // Verschlüsseln
                                try {
                                    std::string plain_str(plain_bytes.begin(), plain_bytes.end());
                                    auto blob = field_encryption_->encryptWithKey(plain_str, key_id, 1, raw_key);
                                    auto j = blob.toJson();
                                    entity.setField(f + "_encrypted", j.dump());
                                    entity.setField(f + "_enc", true);
                                    // Klartext entfernen
                                    entity.setField(f, std::monostate{});
                                } catch (const std::exception& e) {
                                    THEMIS_WARN("Feldverschlüsselung fehlgeschlagen für {}: {}", f, e.what());
                                }
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            THEMIS_WARN("Schema Encryption Verarbeitung Fehler: {}", e.what());
        }

        // Upsert via SecondaryIndexManager to keep indexes consistent
        auto st = secondary_index_->put(table, entity);
        if (!st.ok) {
            // Check for unique constraint violation
            if (st.message.find("Unique constraint violation") != std::string::npos) {
                span.setStatus(false, "Unique constraint violation");
                return makeErrorResponse(http::status::conflict,
                    st.message, req);
            }
            span.setStatus(false, st.message);
            return makeErrorResponse(http::status::internal_server_error,
                "Index/Storage update failed: " + st.message, req);
        }

        // Record CDC event if changefeed enabled
        if (changefeed_ && config_.feature_cdc) {
            try {
                Changefeed::ChangeEvent event;
                event.type = Changefeed::ChangeEventType::EVENT_PUT;
                event.key = key;
                event.value = blob_str;
                event.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                event.metadata = {{"table", table}, {"pk", pk}};
                changefeed_->recordEvent(event);
            } catch (const std::exception& e) {
                // Log but don't fail the request
                THEMIS_WARN("CDC event recording failed: {}", e.what());
            }
        }

        span.setStatus(true);
        span.setAttribute("entity.cdc_recorded", changefeed_ && config_.feature_cdc);

        json response = {
            {"success", true},
            {"key", key},
            {"blob_size", blob_str.size()}
        };
        return makeResponse(http::status::created, response.dump(), req);

    } catch (const json::exception& e) {
        THEMIS_ERROR("PUT entity JSON error: {}", e.what());
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::bad_request, 
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        THEMIS_ERROR("PUT entity error: {}", e.what());
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleDeleteEntity(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "data:write", "delete", path_only)) return *resp;
    }
    auto span = Tracer::startSpan("DELETE /entities/:key");
    
    try {
        auto key = extractPathParam(std::string(req.target()), "/entities/");
        if (key.empty()) {
            span.setStatus(false, "Missing entity key");
            return makeErrorResponse(http::status::bad_request, "Missing entity key", req);
        }

        span.setAttribute("entity.key", key);

        // Split key into table:pk
        auto pos = key.find(':');
        if (pos == std::string::npos || pos == 0 || pos == key.size()-1) {
            span.setStatus(false, "Invalid key format");
            return makeErrorResponse(http::status::bad_request, "Key must be in format 'table:pk'", req);
        }
        std::string table = key.substr(0, pos);
        std::string pk = key.substr(pos+1);

        span.setAttribute("entity.table", table);
        span.setAttribute("entity.pk", pk);

        auto st = secondary_index_->erase(table, pk);
        if (!st.ok) {
            span.setStatus(false, st.message);
            return makeErrorResponse(http::status::internal_server_error,
                "Index/Storage delete failed: " + st.message, req);
        }

        // Record CDC event if changefeed enabled
        if (changefeed_ && config_.feature_cdc) {
            try {
                Changefeed::ChangeEvent event;
                event.type = Changefeed::ChangeEventType::EVENT_DELETE;
                event.key = key;
                event.value = std::nullopt; // No value for DELETE
                event.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                event.metadata = {{"table", table}, {"pk", pk}};
                changefeed_->recordEvent(event);
            } catch (const std::exception& e) {
                THEMIS_WARN("CDC event recording failed: {}", e.what());
            }
        }

        span.setStatus(true);
        span.setAttribute("entity.cdc_recorded", changefeed_ && config_.feature_cdc);

        json response = {
            {"success", true},
            {"key", key}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const std::exception& e) {
        THEMIS_ERROR("DELETE entity error: {}", e.what());
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleQuery(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "data:read", "query", path_only)) return *resp;
    }
    auto span = Tracer::startSpan("POST /query");
    
    try {
        auto body = json::parse(req.body());
        if (!body.contains("table")) {
            span.setStatus(false, "Missing table");
            return makeErrorResponse(http::status::bad_request, "Missing 'table'", req);
        }

        std::string table = body["table"].get<std::string>();
        span.setAttribute("query.table", table);
        
        std::vector<themis::PredicateEq> preds;
        if (body.contains("predicates")) {
            for (const auto& p : body["predicates"]) {
                if (!p.contains("column") || !p.contains("value")) {
                    span.setStatus(false, "Invalid predicate");
                    return makeErrorResponse(http::status::bad_request, "Each predicate needs 'column' and 'value'", req);
                }
                preds.push_back({p["column"].get<std::string>(), p["value"].get<std::string>()});
            }
        }

        span.setAttribute("query.predicates_count", static_cast<int64_t>(preds.size()));

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
    bool decrypt = body.contains("decrypt") ? body["decrypt"].get<bool>() : false;

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
                span.setStatus(false, res.first.message);
                return makeErrorResponse(http::status::bad_request, res.first.message, req);
            }
            
            span.setAttribute("query.exec_mode", exec_mode);
            span.setAttribute("query.result_count", static_cast<int64_t>(res.second.size()));
            span.setStatus(true);
            
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
                span.setStatus(false, res.first.message);
                return makeErrorResponse(http::status::bad_request, res.first.message, req);
            }
            
            span.setAttribute("query.exec_mode", exec_mode);
            span.setAttribute("query.result_count", static_cast<int64_t>(res.second.size()));
            span.setStatus(true);
            
            // Serialize entities; optional Entschluesselung basierend auf Schema
            json entities = json::array();
            if (!decrypt) {
                for (const auto& e : res.second) {
                    // Kompatible Rueckgabe: JSON-String je Entity
                    entities.push_back(e.toJson());
                }
            } else {
                // Lade Schema einmal
                nlohmann::json schema;
                try {
                    if (auto schema_bytes = storage_->get("config:encryption_schema")) {
                        std::string schema_json(schema_bytes->begin(), schema_bytes->end());
                        schema = nlohmann::json::parse(schema_json);
                    }
                } catch (...) {}
                bool enabled = false;
                std::vector<std::string> fields;
                std::string context_type = "user";
                if (!schema.is_null() && schema.contains("collections") && schema["collections"].contains(table)) {
                    auto coll = schema["collections"][table];
                    enabled = coll.contains("encryption") && coll["encryption"].value("enabled", false);
                    if (enabled && coll["encryption"].contains("fields")) {
                        for (auto& f : coll["encryption"]["fields"]) if (f.is_string()) fields.push_back(f.get<std::string>());
                        context_type = coll["encryption"].value("context_type", "user");
                    }
                }
                // Extract user_id and groups from JWT for decryption context
                auto auth_ctx = extractAuthContext(req);
                std::string user_ctx = auth_ctx.user_id.empty() ? "anonymous" : auth_ctx.user_id;
                auto pki = std::dynamic_pointer_cast<themis::security::PKIKeyProvider>(key_provider_);
                for (const auto& e : res.second) {
                    nlohmann::json obj;
                    try { obj = nlohmann::json::parse(e.toJson()); } catch (...) { entities.push_back(e.toJson()); continue; }
                    if (enabled) {
                        for (const auto& f : fields) {
                            if (!obj.contains(f + "_enc") || !obj.contains(f + "_encrypted")) continue;
                            bool encFlag = false; try { encFlag = obj[f + "_enc"].get<bool>(); } catch (...) { encFlag = false; }
                            if (!encFlag) continue;
                            try {
                                auto enc_meta_str = obj[f + "_encrypted"].get<std::string>();
                                auto enc_meta = nlohmann::json::parse(enc_meta_str);
                                auto blob = themis::EncryptedBlob::fromJson(enc_meta);
                                std::vector<uint8_t> raw_key;
                                if (context_type == "group" && pki && obj.contains(f + "_group")) {
                                    std::string group_name; try { group_name = obj[f + "_group"].get<std::string>(); } catch (...) { group_name.clear(); }
                                    if (!group_name.empty()) {
                                        auto gdek = pki->getGroupDEK(group_name);
                                        std::vector<uint8_t> salt; std::string info = "field:" + f;
                                        raw_key = themis::utils::HKDFHelper::derive(gdek, salt, info, 32);
                                    }
                                }
                                if (raw_key.empty()) {
                                    auto dek = key_provider_->getKey("dek");
                                    std::vector<uint8_t> salt(user_ctx.begin(), user_ctx.end());
                                    std::string info = "field:" + f;
                                    raw_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
                                }
                                auto plain_bytes = field_encryption_->decryptWithKey(blob, raw_key);
                                
                                // Deserialisierung basierend auf Datenformat
                                std::string plain_str(plain_bytes.begin(), plain_bytes.end());
                                
                                // Heuristik: JSON-Strukturen erkennen und parsen
                                if (!plain_str.empty() && (plain_str[0] == '[' || plain_str[0] == '{')) {
                                    try {
                                        auto parsed = nlohmann::json::parse(plain_str);
                                        obj[f] = parsed;
                                    } catch (...) {
                                        obj[f] = plain_str;
                                    }
                                } else {
                                    obj[f] = plain_str;
                                }
                            } catch (const std::exception& ex) {
                                THEMIS_WARN("Query decrypt field {} failed: {}", f, ex.what());
                            }
                        }
                    }
                    entities.push_back(obj);
                }
            }
            json j = {{"table", table}, {"count", res.second.size()}, {"entities", entities}, {"decrypted", decrypt}};
            if (explain && !plan_json.is_null()) j["plan"] = plan_json;
            return makeResponse(http::status::ok, j.dump(), req);
        }

    } catch (const json::exception& e) {
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleQueryAql(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("POST /query/aql");
    
    try {
        auto body = json::parse(req.body());
        
        // Validate request
        if (!body.contains("query")) {
            span.setStatus(false, "Missing query field");
            return makeErrorResponse(http::status::bad_request, "Missing 'query' field", req);
        }

        std::string aql_query = body["query"].get<std::string>();
        span.setAttribute("aql.query", aql_query);
        bool explain = body.contains("explain") ? body["explain"].get<bool>() : false;
        span.setAttribute("aql.explain", explain);
        bool optimize = body.contains("optimize") ? body["optimize"].get<bool>() : true;
        span.setAttribute("aql.optimize", optimize);
        bool allow_full_scan = body.contains("allow_full_scan") ? body["allow_full_scan"].get<bool>() : false;
        span.setAttribute("aql.allow_full_scan", allow_full_scan);
        
    // Cursor-based pagination parameters
        std::string cursor_token = body.contains("cursor") ? body["cursor"].get<std::string>() : "";
        bool use_cursor = body.contains("use_cursor") ? body["use_cursor"].get<bool>() : false;
    auto page_fetch_start = std::chrono::steady_clock::now();
        
    // Cursor-Pagination: Wir verlagern Cursor-Handling in die Engine (Anker-basiert)
        
        // Optional: Frontier-Limits f�r Traversal (Soft-Limit)
        size_t max_frontier_size = body.contains("max_frontier_size") ? body["max_frontier_size"].get<size_t>() : 100000;
        size_t max_results = body.contains("max_results") ? body["max_results"].get<size_t>() : 10000;
        
        // Parse AQL query
        auto parseSpan = Tracer::startSpan("aql.parse");
        parseSpan.setAttribute("aql.query_length", static_cast<int64_t>(aql_query.size()));
        themis::query::AQLParser parser;
        auto parse_result = parser.parse(aql_query);
        
        if (!parse_result.success) {
            std::string error_msg = "AQL parse error: " + parse_result.error.message + 
                " at line " + std::to_string(parse_result.error.line) + 
                ", column " + std::to_string(parse_result.error.column);
            
            if (!parse_result.error.context.empty()) {
                error_msg += " (context: " + parse_result.error.context + ")";
            }
            
            parseSpan.setStatus(false, error_msg);
            span.setStatus(false, "Parse error");
            return makeErrorResponse(http::status::bad_request, error_msg, req);
        }
        parseSpan.setStatus(true);

        // EARLY: Join-Erkennung vor Translation (Translator unterstützt keine Field==Field Prädikate)
        if (parse_result.query && parse_result.query->traversal == nullptr && !parse_result.query->for_nodes.empty() && parse_result.query->for_nodes.size() >= 2) {
            // Wiederverwendung der Join-Logik wie weiter unten
            auto joinSpan = Tracer::startSpan("aql.join");
            const auto& f1 = parse_result.query->for_nodes[0];
            const auto& f2 = parse_result.query->for_nodes[1];
            const std::string var1 = f1.variable;
            const std::string var2 = f2.variable;
            const std::string table1 = f1.collection;
            const std::string table2 = f2.collection;
            joinSpan.setAttribute("join.var_left", var1);
            joinSpan.setAttribute("join.var_right", var2);
            joinSpan.setAttribute("join.table_left", table1);
            joinSpan.setAttribute("join.table_right", table2);

            using namespace themis::query;
            std::function<std::string(const std::shared_ptr<Expression>&, std::string&)> fieldFromFA = [&](const std::shared_ptr<Expression>& expr, std::string& rootVar)->std::string {
                auto* fa = dynamic_cast<FieldAccessExpr*>(expr.get());
                if (!fa) return std::string();
                std::vector<std::string> parts; parts.push_back(fa->field);
                auto* cur = fa->object.get();
                while (auto* fa2 = dynamic_cast<FieldAccessExpr*>(cur)) { parts.push_back(fa2->field); cur = fa2->object.get(); }
                auto* root = dynamic_cast<VariableExpr*>(cur); if (!root) return std::string();
                rootVar = root->name; std::string col; for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!col.empty()) col += "."; col += *it; } return col;
            };
            auto literalToString = [&](const LiteralValue& value)->std::string{
                return std::visit([](auto&& arg)->std::string{
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>) return std::string("null");
                    else if constexpr (std::is_same_v<T, bool>) return arg ? std::string("true") : std::string("false");
                    else if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
                    else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                    else if constexpr (std::is_same_v<T, std::string>) return arg;
                    else return std::string();
                }, value);
            };
            std::optional<std::pair<std::string,std::string>> joinCols; std::vector<PredicateEq> eq1, eq2; std::vector<PredicateRange> r1, r2;
            std::function<void(const std::shared_ptr<Expression>&)> collectPreds;
            collectPreds = [&](const std::shared_ptr<Expression>& e){
                if (!e) return; if (e->getType() != ASTNodeType::BinaryOp) return; auto bin = std::static_pointer_cast<BinaryOpExpr>(e);
                if (bin->op == BinaryOperator::And) { collectPreds(bin->left); collectPreds(bin->right); return; }
                if (bin->op == BinaryOperator::Eq) {
                    std::string rvL, rvR; std::string colL = fieldFromFA(bin->left, rvL); std::string colR = fieldFromFA(bin->right, rvR);
                    if (!colL.empty() && !colR.empty() && ((rvL == var1 && rvR == var2) || (rvL == var2 && rvR == var1))) {
                        if (!joinCols.has_value()) { if (rvL == var1) joinCols = std::make_pair(colL, colR); else joinCols = std::make_pair(colR, colL); }
                        return;
                    }
                    if (!colL.empty() && rvL == var1 && bin->right->getType() == ASTNodeType::Literal) { auto lit = std::static_pointer_cast<LiteralExpr>(bin->right); eq1.push_back({colL, literalToString(lit->value)}); return; }
                    if (!colL.empty() && rvL == var2 && bin->right->getType() == ASTNodeType::Literal) { auto lit = std::static_pointer_cast<LiteralExpr>(bin->right); eq2.push_back({colL, literalToString(lit->value)}); return; }
                    if (bin->left->getType() == ASTNodeType::Literal) { std::string rv; std::string col = fieldFromFA(bin->right, rv); if (!col.empty()) { auto lit = std::static_pointer_cast<LiteralExpr>(bin->left); if (rv == var1) eq1.push_back({col, literalToString(lit->value)}); else if (rv == var2) eq2.push_back({col, literalToString(lit->value)}); } return; }
                }
            };
            for (const auto& f : parse_result.query->filters) collectPreds(f->condition);
            if (!joinCols.has_value()) {
                joinSpan.setStatus(false, "join_predicate_missing"); span.setStatus(false, "JOIN requires equality predicate between variables");
                return makeErrorResponse(http::status::bad_request, "JOIN requires equality predicate between variables", req);
            }
            themis::ConjunctiveQuery q1; q1.table = table1; q1.predicates = eq1; q1.rangePredicates = r1;
            themis::ConjunctiveQuery q2; q2.table = table2; q2.predicates = eq2; q2.rangePredicates = r2;
            themis::QueryEngine engine(*storage_, *secondary_index_);
            auto res1 = allow_full_scan ? engine.executeAndEntitiesWithFallback(q1, optimize) : engine.executeAndEntities(q1);
            if (!res1.first.ok) { joinSpan.setStatus(false, res1.first.message); span.setStatus(false, "Left side execution failed"); return makeErrorResponse(http::status::bad_request, res1.first.message, req); }
            auto res2 = allow_full_scan ? engine.executeAndEntitiesWithFallback(q2, optimize) : engine.executeAndEntities(q2);
            if (!res2.first.ok) { joinSpan.setStatus(false, res2.first.message); span.setStatus(false, "Right side execution failed"); return makeErrorResponse(http::status::bad_request, res2.first.message, req); }
            const auto& leftVec = res1.second; const auto& rightVec = res2.second; bool buildLeft = leftVec.size() <= rightVec.size();
            const auto [colLeft, colRight] = *joinCols; std::unordered_multimap<std::string, themis::BaseEntity> hash;
            auto getFieldStr = [&](const themis::BaseEntity& e, const std::string& col)->std::optional<std::string> { auto v = e.getFieldAsString(col); if (v.has_value()) return v; auto d = e.getFieldAsDouble(col); if (d.has_value()) return std::to_string(*d); return std::nullopt; };
            if (buildLeft) { hash.reserve(leftVec.size()*2+1); for (const auto& e : leftVec) { auto k = getFieldStr(e, colLeft); if (k.has_value()) hash.emplace(*k, e); } }
            else { hash.reserve(rightVec.size()*2+1); for (const auto& e : rightVec) { auto k = getFieldStr(e, colRight); if (k.has_value()) hash.emplace(*k, e); } }
            std::string retVar; if (parse_result.query->return_node && parse_result.query->return_node->expression) { if (auto* v = dynamic_cast<VariableExpr*>(parse_result.query->return_node->expression.get())) { retVar = v->name; } }
            if (retVar != var1 && retVar != var2) { joinSpan.setStatus(false, "return_not_supported_for_join"); span.setStatus(false, "JOIN currently supports RETURN of one bound variable (left or right)"); return makeErrorResponse(http::status::bad_request, "JOIN currently supports RETURN of one bound variable (left or right)", req); }
            std::vector<themis::BaseEntity> out;
            if (buildLeft) { for (const auto& e : rightVec) { auto k = getFieldStr(e, colRight); if (!k.has_value()) continue; auto range = hash.equal_range(*k); for (auto it = range.first; it != range.second; ++it) { const themis::BaseEntity& l = it->second; if (retVar == var1) out.push_back(l); else out.push_back(e); } } }
            else { for (const auto& e : leftVec) { auto k = getFieldStr(e, colLeft); if (!k.has_value()) continue; auto range = hash.equal_range(*k); for (auto it = range.first; it != range.second; ++it) { const themis::BaseEntity& r = it->second; if (retVar == var1) out.push_back(e); else out.push_back(r); } } }
            if (parse_result.query && parse_result.query->limit) { auto off = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->offset)); auto cnt = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->count)); if (off < out.size()) { size_t last = std::min(out.size(), off + cnt); std::vector<themis::BaseEntity> tmp; tmp.reserve(last - off); for (size_t i = off; i < last; ++i) tmp.emplace_back(std::move(out[i])); out.swap(tmp); } else { out.clear(); } }
            nlohmann::json entities = nlohmann::json::array(); for (const auto& e : out) entities.push_back(e.toJson()); nlohmann::json response_body = {{"table_left", table1}, {"table_right", table2}, {"count", out.size()}, {"entities", entities}};
            if (explain) { response_body["query"] = aql_query; response_body["ast"] = parse_result.query->toJSON(); nlohmann::json jp; jp["on_left"] = (*joinCols).first; jp["on_right"] = (*joinCols).second; response_body["join"] = jp; }
            joinSpan.setAttribute("join.output_count", static_cast<int64_t>(out.size())); joinSpan.setStatus(true); span.setAttribute("aql.result_count", static_cast<int64_t>(out.size())); span.setStatus(true);
            return makeResponse(http::status::ok, response_body.dump(), req);
        }

    // Translate AST to Query (relational oder traversal)
    // Spezialfall: LET-Variablen in FILTER (MVP) – vor Übersetzung einfache Ersetzung erlauben
    bool letFilterHandled = false; // wenn true, nutzen wir einen manuell konstruierten ConjunctiveQuery
    themis::ConjunctiveQuery letQuery;
    if (parse_result.query && parse_result.query->traversal == nullptr && !parse_result.query->for_nodes.empty()) {
        // Wir unterstützen nur den einfachen relationalen Fall mit genau einer FOR-Klausel
        const auto& forNode = parse_result.query->for_node;
        const std::string loopVar = forNode.variable;
        const std::string table = forNode.collection;
        if (!parse_result.query->filters.empty() && !parse_result.query->let_nodes.empty()) {
            // Map der LET-Bindings: var -> expr
            std::unordered_map<std::string, std::shared_ptr<themis::query::Expression>> letMap;
            for (const auto& ln : parse_result.query->let_nodes) letMap[ln.variable] = ln.expression;

            using namespace themis::query;
            // Helfer: löse Ausdruck zu einer Feldspalte der Loop-Variable auf, ggf. via LET-Variable
            std::function<std::optional<std::string>(const std::shared_ptr<Expression>&)> resolveToLoopField;
            resolveToLoopField = [&](const std::shared_ptr<Expression>& e)->std::optional<std::string> {
                if (!e) return std::nullopt;
                if (auto* fa = dynamic_cast<FieldAccessExpr*>(e.get())) {
                    // Sammle Feldpfad und prüfe Root-Variable
                    std::vector<std::string> parts; parts.push_back(fa->field);
                    auto* cur = fa->object.get();
                    while (auto* fa2 = dynamic_cast<FieldAccessExpr*>(cur)) { parts.push_back(fa2->field); cur = fa2->object.get(); }
                    auto* root = dynamic_cast<VariableExpr*>(cur);
                    if (!root || root->name != loopVar) return std::nullopt;
                    std::string col; for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!col.empty()) col += "."; col += *it; }
                    return col;
                }
                if (auto* v = dynamic_cast<VariableExpr*>(e.get())) {
                    auto it = letMap.find(v->name);
                    if (it == letMap.end()) return std::nullopt;
                    return resolveToLoopField(it->second);
                }
                return std::nullopt;
            };

            // Extrahiere Gleichheitsprädikate mit LET auf linkem oder rechtem Operand
            std::vector<themis::PredicateEq> eqPreds;
            bool unsupported = false;
            // Lokale Literal-zu-String Konvertierung (wie im Übersetzer)
            auto litToString = [&](const themis::query::LiteralValue& value)->std::string{
                return std::visit([](auto&& arg)->std::string{
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>) return std::string("null");
                    else if constexpr (std::is_same_v<T, bool>) return arg ? std::string("true") : std::string("false");
                    else if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
                    else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                    else if constexpr (std::is_same_v<T, std::string>) return arg;
                    else return std::string();
                }, value);
            };
            std::function<void(const std::shared_ptr<Expression>&)> visit;
            visit = [&](const std::shared_ptr<Expression>& ex){
                if (!ex || unsupported) return;
                if (auto* be = dynamic_cast<BinaryOpExpr*>(ex.get())) {
                    if (be->op == BinaryOperator::And) { visit(be->left); visit(be->right); return; }
                    if (be->op == BinaryOperator::Eq) {
                        auto leftCol = resolveToLoopField(be->left);
                        auto rightCol = resolveToLoopField(be->right);
                        if (leftCol.has_value() && be->right->getType() == ASTNodeType::Literal) {
                            auto lit = std::static_pointer_cast<LiteralExpr>(be->right);
                            eqPreds.push_back({*leftCol, litToString(lit->value)});
                            return;
                        }
                        if (rightCol.has_value() && be->left->getType() == ASTNodeType::Literal) {
                            auto lit = std::static_pointer_cast<LiteralExpr>(be->left);
                            eqPreds.push_back({*rightCol, litToString(lit->value)});
                            return;
                        }
                    }
                    // Alles andere (inkl. OR, Range, Funktionen) im MVP nicht via LET-Filter unterstützt
                    // Wir markieren nicht global als unsupported, damit andere AND-Zweige extrahiert werden können.
                    return;
                }
            };
            for (const auto& f : parse_result.query->filters) visit(f->condition);

            // Wenn wir etwas extrahieren konnten, nutzen wir dafür einen direkten ConjunctiveQuery
            if (!eqPreds.empty()) {
                letQuery.table = table;
                letQuery.predicates = std::move(eqPreds);
                letFilterHandled = true;
            }
        }
    }

    auto translateSpan = Tracer::startSpan("aql.translate");
    auto translate_result = letFilterHandled
        ? themis::AQLTranslator::TranslationResult::Success(letQuery)
        : themis::AQLTranslator::translate(parse_result.query);
        
        if (!translate_result.success) {
            translateSpan.setStatus(false, translate_result.error_message);
            span.setStatus(false, "Translation error");
            return makeErrorResponse(http::status::bad_request,
                "AQL translation error: " + translate_result.error_message, req);
        }
        translateSpan.setStatus(true);
        
    // If traversal present, execute via GraphIndexManager
        if (translate_result.traversal.has_value()) {
            auto traversalSpan = Tracer::startSpan("aql.traversal");
            if (!graph_index_) {
                return makeErrorResponse(http::status::bad_request, "Graph traversal requested but graph index manager is not available", req);
            }
            const auto& t = translate_result.traversal.value();
            traversalSpan.setAttribute("traversal.start_vertex", t.startVertex);
            traversalSpan.setAttribute("traversal.min_depth", static_cast<int64_t>(t.minDepth));
            traversalSpan.setAttribute("traversal.max_depth", static_cast<int64_t>(t.maxDepth));
            std::string dirStr = (t.direction == themis::AQLTranslator::TranslationResult::TraversalQuery::Direction::Outbound) ? "OUTBOUND" :
                                 (t.direction == themis::AQLTranslator::TranslationResult::TraversalQuery::Direction::Inbound) ? "INBOUND" : "ANY";
            traversalSpan.setAttribute("traversal.direction", dirStr);
            
            if (t.minDepth < 0 || t.maxDepth < 0 || t.maxDepth < t.minDepth) {
                traversalSpan.setStatus(false, "Invalid depth range");
                span.setStatus(false, "Invalid traversal depth");
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

            // Extrahiere einfache FILTER-Pr�dikate auf v/e im Format: FILTER v.<field> == <literal|funktion> oder FILTER e.<field> == <literal|funktion>
            struct SimplePred {
                enum class Op { Eq, Neq, Lt, Lte, Gt, Gte };
                char var; // 'v' oder 'e'
                std::string field;
                nlohmann::json literal; // als JSON-Literal
                Op op;
            };
            // Unterst�tzte Funktionsauswertung zur Reduktion auf Literale
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
                // Portable conversions between tm and time_t (UTC)
                auto portable_mkgmtime = [&](const std::tm* tmin)->time_t {
#ifdef _WIN32
                    return _mkgmtime(const_cast<std::tm*>(tmin));
#else
                    return timegm(const_cast<std::tm*>(tmin));
#endif
                };
                auto portable_gmtime_r = [&](const time_t* t, std::tm* out)->void {
#ifdef _WIN32
                    gmtime_s(out, t);
#else
                    gmtime_r(t, out);
#endif
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
                    // DATE_ADD(dateStr, amount, unit) � unterst�tzt 'day','month','year'
                    nlohmann::json dateJ, amountJ, unitJ; if (!getArgLit(0, dateJ) || !getArgLit(1, amountJ) || !getArgLit(2, unitJ)) return false;
                    if (!dateJ.is_string() || !amountJ.is_number_integer() || !unitJ.is_string()) return false;
                    std::string unit = unitJ.get<std::string>(); std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
                    std::tm tm{}; if (!parseIso(dateJ.get<std::string>(), tm)) return false;
                    long long amt = amountJ.get<long long>(); if (name == "date_sub") amt = -amt;
                    if (unit == "day") {
                        time_t t = portable_mkgmtime(&tm); if (t == -1) return false;
                        t += static_cast<time_t>(amt * 86400);
                        std::tm outTm{}; portable_gmtime_r(&t, &outTm);
                        out = tmToDateStr(outTm); return true;
                    } else if (unit == "month") {
                        tm.tm_mon += static_cast<int>(amt);
                        time_t t = portable_mkgmtime(&tm); if (t == -1) return false;
                        std::tm outTm{}; portable_gmtime_r(&t, &outTm);
                        out = tmToDateStr(outTm); return true;
                    } else if (unit == "year") {
                        tm.tm_year += static_cast<int>(amt);
                        time_t t = portable_mkgmtime(&tm); if (t == -1) return false;
                        std::tm outTm{}; portable_gmtime_r(&t, &outTm);
                        out = tmToDateStr(outTm); return true;
                    } else {
                        return false;
                    }
                }
                if (name == "now") {
                    // Gibt YYYY-MM-DD zur�ck (UTC)
                    std::time_t t = std::time(nullptr); std::tm tm{}; portable_gmtime_r(&t, &tm);
                    out = tmToDateStr(tm); return true;
                }
                return false; // unbekannte Funktion
            };
            std::vector<SimplePred> preds;
            // XOR-Unterst�tzung: Paare einfacher Pr�dikate (links XOR rechts)
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
                        // XOR-Filter als Ganzes sind verarbeitet; nicht in einfache Pr�dikate aufnehmen
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
                    // Unterst�tzt YYYY-MM-DD oder YYYY-MM-DDTHH:MM:SSZ
                    std::tm tm{}; memset(&tm, 0, sizeof tm);
                    if (s.size() == 10 && std::sscanf(s.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
                        tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
                        t = portable_mkgmtime_impl(&tm); return t != -1;
                    }
                    int Y,M,D,h=0,m=0,sec=0; char Z='\0', T='\0';
                    if (std::sscanf(s.c_str(), "%d-%d-%d%c%d:%d:%d%c", &Y,&M,&D,&T,&h,&m,&sec,&Z) >= 7) {
                        tm.tm_year = Y-1900; tm.tm_mon = M-1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;
                        t = portable_mkgmtime_impl(&tm); return t != -1;
                    }
                    return false;
                };

                // Literaltypen pr�fen
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
                    // Datumsvergleich falls beide ISO-�hnlich
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
            size_t filterShortCircuits = 0;  // Z�hlt Short-Circuit-Evaluationen in Filtern
            // Parent map used by path-based predicates is declared here so lambdas below can capture it
            struct ParentInfo { std::string parent; std::string edgeId; };
            std::unordered_map<std::string, ParentInfo> parent;

            std::function<bool(const Expression*, const std::string&, const std::optional<std::string>&)> evalBoolExpr;
            evalBoolExpr = [&](const Expression* e, const std::string& vpk, const std::optional<std::string>& eid)->bool{
                if (!e) return true;
                // Special handling: PATH.ALL / PATH.ANY / PATH.NONE
                if (auto* fe = dynamic_cast<const FunctionCallExpr*>(e)) {
                    std::string fname = fe->name; std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
                    if (fname == "path.all" || fname == "path.any" || fname == "path.none") {
                        // Expect two arguments: variable name (v or e) and a predicate expression
                        if (fe->arguments.size() != 2) return false;
                        auto* varExpr = dynamic_cast<const VariableExpr*>(fe->arguments[0].get());
                        if (!varExpr) return false;
                        std::string varName = varExpr->name; // 'v' or 'e'
                        const Expression* inner = fe->arguments[1].get();
                        // Reconstruct path from startVertex to vpk using parent map
                        std::vector<std::string> pathNodes;
                        std::vector<std::string> pathEdges; // edges between pathNodes[i] -> pathNodes[i+1]
                        std::string cur = vpk;
                        // If vpk empty, nothing to evaluate
                        if (cur.empty()) {
                            // Empty path: PATH.ALL & PATH.NONE -> true, PATH.ANY -> false
                            if (fname == "path.any") return false; else return true;
                        }
                        // Walk back using parent map if available
                        pathNodes.push_back(cur);
                        auto itp = parent.find(cur);
                        while (itp != parent.end()) {
                            pathEdges.push_back(itp->second.edgeId);
                            pathNodes.push_back(itp->second.parent);
                            itp = parent.find(pathNodes.back());
                        }
                        // Now reverse to get start->...->cur
                        std::reverse(pathNodes.begin(), pathNodes.end());
                        std::reverse(pathEdges.begin(), pathEdges.end());

                        bool any = false;
                        bool all = true;
                        // If varName == "v", evaluate inner for each vertex in pathNodes
                        if (varName == "v") {
                            for (const auto& nodePk : pathNodes) {
                                bool r = evalBoolExpr(inner, nodePk, std::nullopt);
                                any = any || r;
                                all = all && r;
                            }
                        } else if (varName == "e") {
                            // Iterate over edges; align edge i with nodes i -> i+1
                            for (size_t i = 0; i < pathEdges.size(); ++i) {
                                const auto& eid2 = pathEdges[i];
                                // For edge evaluation, set vpk to the 'to' vertex (pathNodes[i+1]) and eid to edge id
                                bool r = evalBoolExpr(inner, pathNodes[i+1], std::optional<std::string>(eid2));
                                any = any || r;
                                all = all && r;
                            }
                        } else {
                            return false; // unknown var
                        }

                        if (fname == "path.all") return all;
                        if (fname == "path.none") return !any;
                        return any; // path.any
                    }
                }
                if (auto* ue = dynamic_cast<const UnaryOpExpr*>(e)) {
                    if (ue->op == UnaryOperator::Not) return !evalBoolExpr(ue->operand.get(), vpk, eid);
                    return false;
                }
                if (auto* be = dynamic_cast<const BinaryOpExpr*>(e)) {
                    auto evalCmp = [&](const Expression* left, BinaryOperator op, const Expression* right)->bool{
                        // Unterst�tze: FieldAccess(v|e).field <op> (Literal|Funktion) und umgekehrt
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
                        return false; // nicht unterst�tztes Muster
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
                // Literale/Variablen alleine als bool nicht unterst�tzt ? false
                return false;
            };

            // Helper: pr�fe, ob ein Ausdruck v/e-Referenzen enth�lt (f�r konstante Vorabpr�fung)
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
                    // Pr�fe, ob das Objekt eine Variable v/e ist
                    if (auto* objVar = dynamic_cast<VariableExpr*>(fa->object.get())) {
                        if (objVar->name == "v" || objVar->name == "e") return true;
                    }
                    // Auch verschachtelte Zugriffe k�nnen v/e enthalten
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

            // Vorab: Wenn alle FILTER-Ausdr�cke keine v/e-Referenz enthalten, einmal bewerten und ggf. fr�h abbrechen
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
                    // Joins via doppeltem FOR (MVP): Wenn mehrere FOR-Klauseln vorhanden sind und keine Traversal-Query aktiv ist
                    if (parse_result.query && parse_result.query->traversal == nullptr && !parse_result.query->for_nodes.empty() && parse_result.query->for_nodes.size() >= 2) {
                        auto joinSpan = Tracer::startSpan("aql.join");
                        // Beschränkung: Genau zwei FOR-Klauseln, Equality-Join über FILTER lhs.field == rhs.field
                        const auto& f1 = parse_result.query->for_nodes[0];
                        const auto& f2 = parse_result.query->for_nodes[1];
                        const std::string var1 = f1.variable;
                        const std::string var2 = f2.variable;
                        const std::string table1 = f1.collection;
                        const std::string table2 = f2.collection;
                        joinSpan.setAttribute("join.var_left", var1);
                        joinSpan.setAttribute("join.var_right", var2);
                        joinSpan.setAttribute("join.table_left", table1);
                        joinSpan.setAttribute("join.table_right", table2);

                        // Hilfsfunktionen zur Extraktion
                        using namespace themis::query;
                        std::function<std::string(const std::shared_ptr<Expression>&, std::string&)> fieldFromFA = [&](const std::shared_ptr<Expression>& expr, std::string& rootVar)->std::string {
                            // Liefert Feldpfad ("a.b") und setzt rootVar auf Variablennamen
                            auto* fa = dynamic_cast<FieldAccessExpr*>(expr.get());
                            if (!fa) return std::string();
                            std::vector<std::string> parts;
                            parts.push_back(fa->field);
                            auto* cur = fa->object.get();
                            while (auto* fa2 = dynamic_cast<FieldAccessExpr*>(cur)) {
                                parts.push_back(fa2->field);
                                cur = fa2->object.get();
                            }
                            auto* root = dynamic_cast<VariableExpr*>(cur);
                            if (!root) return std::string();
                            rootVar = root->name;
                            std::string col;
                            for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!col.empty()) col += "."; col += *it; }
                            return col;
                        };
                        auto literalToString = [&](const LiteralValue& value)->std::string{
                            return std::visit([](auto&& arg)->std::string{
                                using T = std::decay_t<decltype(arg)>;
                                if constexpr (std::is_same_v<T, std::nullptr_t>) return std::string("null");
                                else if constexpr (std::is_same_v<T, bool>) return arg ? std::string("true") : std::string("false");
                                else if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
                                else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                                else if constexpr (std::is_same_v<T, std::string>) return arg;
                                else return std::string();
                            }, value);
                        };

                        // Zerlege FILTER in Konjunktions-Terme und identifiziere Join-Bedingung
                        std::optional<std::pair<std::string,std::string>> joinCols; // (col1, col2)
                        std::vector<PredicateEq> eq1, eq2; std::vector<PredicateRange> r1, r2;
                        std::function<void(const std::shared_ptr<Expression>&)> collectPreds;
                        collectPreds = [&](const std::shared_ptr<Expression>& e){
                            if (!e) return;
                            if (e->getType() == ASTNodeType::BinaryOp) {
                                auto bin = std::static_pointer_cast<BinaryOpExpr>(e);
                                if (bin->op == BinaryOperator::And) {
                                    collectPreds(bin->left); collectPreds(bin->right); return;
                                }
                                // Join-Gleichheit: Field(var1.*) == Field(var2.*) oder umgekehrt
                                if (bin->op == BinaryOperator::Eq) {
                                    std::string rvL, rvR; std::string colL, colR;
                                    colL = fieldFromFA(bin->left, rvL);
                                    colR = fieldFromFA(bin->right, rvR);
                                    if (!colL.empty() && !colR.empty()) {
                                        // beide Seiten FieldAccess
                                        if ((rvL == var1 && rvR == var2) || (rvL == var2 && rvR == var1)) {
                                            if (joinCols.has_value()) {
                                                joinSpan.setStatus(false, "multiple_join_predicates_not_supported");
                                                span.setStatus(false, "Only single equality-join supported");
                                                return; // ignore further
                                            }
                                            if (rvL == var1) joinCols = std::make_pair(colL, colR); else joinCols = std::make_pair(colR, colL);
                                            return;
                                        }
                                    }
                                    // Seiten-Literal Prädikat
                                    if (!colL.empty() && rvL == var1 && bin->right->getType() == ASTNodeType::Literal) {
                                        auto lit = std::static_pointer_cast<LiteralExpr>(bin->right);
                                        if (!lit) return;
                                        switch (bin->op) {
                                            case BinaryOperator::Eq: eq1.push_back({colL, literalToString(lit->value)}); break;
                                            case BinaryOperator::Lt: r1.push_back({colL, std::nullopt, literalToString(lit->value), true, false}); break;
                                            case BinaryOperator::Lte: r1.push_back({colL, std::nullopt, literalToString(lit->value), true, true}); break;
                                            case BinaryOperator::Gt: r1.push_back({colL, literalToString(lit->value), std::nullopt, false, true}); break;
                                            case BinaryOperator::Gte: r1.push_back({colL, literalToString(lit->value), std::nullopt, true, true}); break;
                                            default: break;
                                        }
                                        return;
                                    }
                                    if (!colL.empty() && rvL == var2 && bin->right->getType() == ASTNodeType::Literal) {
                                        auto lit = std::static_pointer_cast<LiteralExpr>(bin->right);
                                        switch (bin->op) {
                                            case BinaryOperator::Eq: eq2.push_back({colL, literalToString(lit->value)}); break;
                                            case BinaryOperator::Lt: r2.push_back({colL, std::nullopt, literalToString(lit->value), true, false}); break;
                                            case BinaryOperator::Lte: r2.push_back({colL, std::nullopt, literalToString(lit->value), true, true}); break;
                                            case BinaryOperator::Gt: r2.push_back({colL, literalToString(lit->value), std::nullopt, false, true}); break;
                                            case BinaryOperator::Gte: r2.push_back({colL, literalToString(lit->value), std::nullopt, true, true}); break;
                                            default: break;
                                        }
                                        return;
                                    }
                                    // Literal == Field(varX.*)
                                    if (bin->left->getType() == ASTNodeType::Literal) {
                                        std::string rv; std::string col = fieldFromFA(bin->right, rv);
                                        if (!col.empty()) {
                                            auto lit = std::static_pointer_cast<LiteralExpr>(bin->left);
                                            if (rv == var1) eq1.push_back({col, literalToString(lit->value)});
                                            else if (rv == var2) eq2.push_back({col, literalToString(lit->value)});
                                        }
                                        return;
                                    }
                                }
                                // Range auf rechter oder linker Seite bereits obigen Fällen abgedeckt (nur MVP)
                            }
                        };
                        for (const auto& f : parse_result.query->filters) {
                            collectPreds(f->condition);
                        }
                        if (!joinCols.has_value()) {
                            joinSpan.setStatus(false, "join_predicate_missing");
                            span.setStatus(false, "JOIN requires equality predicate between variables");
                            return makeErrorResponse(http::status::bad_request, "JOIN requires equality predicate between variables", req);
                        }

                        // Führe Seitenabfragen aus
                        themis::ConjunctiveQuery q1; q1.table = table1; q1.predicates = eq1; q1.rangePredicates = r1;
                        themis::ConjunctiveQuery q2; q2.table = table2; q2.predicates = eq2; q2.rangePredicates = r2;
                        themis::QueryEngine engine(*storage_, *secondary_index_);
                        auto res1 = allow_full_scan ? engine.executeAndEntitiesWithFallback(q1, optimize) : engine.executeAndEntities(q1);
                        if (!res1.first.ok) {
                            joinSpan.setStatus(false, res1.first.message);
                            span.setStatus(false, "Left side execution failed");
                            return makeErrorResponse(http::status::bad_request, res1.first.message, req);
                        }
                        auto res2 = allow_full_scan ? engine.executeAndEntitiesWithFallback(q2, optimize) : engine.executeAndEntities(q2);
                        if (!res2.first.ok) {
                            joinSpan.setStatus(false, res2.first.message);
                            span.setStatus(false, "Right side execution failed");
                            return makeErrorResponse(http::status::bad_request, res2.first.message, req);
                        }

                        // Wähle kleinere Seite für Hash-Index
                        const auto& leftVec = res1.second; const auto& rightVec = res2.second;
                        bool buildLeft = leftVec.size() <= rightVec.size();
                        const auto [colLeft, colRight] = *joinCols;
                        std::unordered_multimap<std::string, themis::BaseEntity> hash;
                        auto getFieldStr = [&](const themis::BaseEntity& e, const std::string& col)->std::optional<std::string> { auto v = e.getFieldAsString(col); if (v.has_value()) return v; auto d = e.getFieldAsDouble(col); if (d.has_value()) return std::to_string(*d); return std::nullopt; };
                        if (buildLeft) {
                            hash.reserve(leftVec.size()*2+1);
                            for (const auto& e : leftVec) { auto k = getFieldStr(e, colLeft); if (k.has_value()) hash.emplace(*k, e); }
                        } else {
                            hash.reserve(rightVec.size()*2+1);
                            for (const auto& e : rightVec) { auto k = getFieldStr(e, colRight); if (k.has_value()) hash.emplace(*k, e); }
                        }

                        // Bestimme welche Variable im RETURN zurückgegeben werden soll
                        std::string retVar;
                        if (parse_result.query->return_node && parse_result.query->return_node->expression) {
                            if (auto* v = dynamic_cast<VariableExpr*>(parse_result.query->return_node->expression.get())) {
                                retVar = v->name;
                            }
                        }
                        if (retVar != var1 && retVar != var2) {
                            joinSpan.setStatus(false, "return_not_supported_for_join");
                            span.setStatus(false, "JOIN currently supports RETURN of one bound variable (left or right)");
                            return makeErrorResponse(http::status::bad_request, "JOIN currently supports RETURN of one bound variable (left or right)", req);
                        }

                        // Probiere Join und sammle Ergebnisse
                        std::vector<themis::BaseEntity> out;
                        if (buildLeft) {
                            for (const auto& e : rightVec) {
                                auto k = getFieldStr(e, colRight); if (!k.has_value()) continue;
                                auto range = hash.equal_range(*k);
                                for (auto it = range.first; it != range.second; ++it) {
                                    const themis::BaseEntity& l = it->second;
                                    if (retVar == var1) out.push_back(l); else out.push_back(e);
                                }
                            }
                        } else {
                            for (const auto& e : leftVec) {
                                auto k = getFieldStr(e, colLeft); if (!k.has_value()) continue;
                                auto range = hash.equal_range(*k);
                                for (auto it = range.first; it != range.second; ++it) {
                                    const themis::BaseEntity& r = it->second;
                                    if (retVar == var1) out.push_back(e); else out.push_back(r);
                                }
                            }
                        }

                        // LIMIT (post-join slicing)
                        if (parse_result.query && parse_result.query->limit) {
                            auto off = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->offset));
                            auto cnt = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->count));
                            if (off < out.size()) {
                                size_t last = std::min(out.size(), off + cnt);
                                std::vector<themis::BaseEntity> tmp; tmp.reserve(last - off);
                                for (size_t i = off; i < last; ++i) tmp.emplace_back(std::move(out[i]));
                                out.swap(tmp);
                            } else {
                                out.clear();
                            }
                        }

                        // Serialize
                        nlohmann::json entities = nlohmann::json::array();
                        for (const auto& e : out) entities.push_back(e.toJson());
                        nlohmann::json response_body = {
                            {"table_left", table1}, {"table_right", table2}, {"count", out.size()}, {"entities", entities}
                        };
                        if (explain) {
                            response_body["query"] = aql_query;
                            response_body["ast"] = parse_result.query->toJSON();
                            nlohmann::json jp; jp["on_left"] = (*joinCols).first; jp["on_right"] = (*joinCols).second; response_body["join"] = jp;
                        }
                        joinSpan.setAttribute("join.output_count", static_cast<int64_t>(out.size()));
                        joinSpan.setStatus(true);
                        span.setAttribute("aql.result_count", static_cast<int64_t>(out.size()));
                        span.setStatus(true);
                        return makeResponse(http::status::ok, response_body.dump(), req);
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
                            // bevorzugt String, f�r Zahlen k�nnte man auch getFieldAsDouble versuchen
                            auto valOpt = ent.getFieldAsString(p.field);
                            if (!valOpt) return false;
                            if (!cmp(*valOpt, p.literal, p.op)) return false;
                        } catch (...) { return false; }
                    }
                }
                return true;
            };

            auto evalSingleV = [&](const std::string& pk, const SimplePred& p)->bool{
                if (p.var != 'v') return true; // nicht zust�ndig
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
                if (p.var != 'e') return true; // nicht zust�ndig
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

            // BFS mit Eltern-/Kanten-Tracking f�r e/p
            std::unordered_set<std::string> visited;
            std::queue<std::pair<std::string,int>> qnodes;
            qnodes.push({t.startVertex, 0});
            visited.insert(t.startVertex);

            // Metriken
            bool constantFilterPrechecked = false; // bleibt false, wenn oben nicht gegriffen (true w�re nur beim Early-Return)
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
            std::vector<std::string> resultTerminalVertices; // f�r Pfadrekonstruktion

            auto withinDepth = [&](int depth){ return depth >= t.minDepth && depth <= t.maxDepth; };

            auto bfsSpan = Tracer::startSpan("aql.traversal.bfs");
            bfsSpan.setAttribute("traversal.max_frontier_size_limit", static_cast<int64_t>(max_frontier_size));
            bfsSpan.setAttribute("traversal.max_results_limit", static_cast<int64_t>(max_results));
            
            while (!qnodes.empty()) {
                // Frontier-Size Limit Check (Soft Limit)
                if (qnodes.size() > max_frontier_size) {
                    frontierLimitHits++;
                    // Optional: Abbruch oder nur Warnung
                    // break;  // hart abbrechen (sp�ter konfigurierbar)
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
                                // Optional: BFS abbrechen (sp�ter konfigurierbar)
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
                        // Konservative Pruning-Strategie am letzten Level: wende einfache v/e-Pr�dikate an
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
            
            bfsSpan.setAttribute("traversal.visited_count", static_cast<int64_t>(visited.size()));
            bfsSpan.setAttribute("traversal.edges_expanded", static_cast<int64_t>(edgesExpanded));
            bfsSpan.setAttribute("traversal.filter_evaluations", static_cast<int64_t>(filterEvaluationsTotal));
            bfsSpan.setStatus(true);

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
                // F�r jeden Terminalknoten Pfad rekonstruieren
                res["count"] = static_cast<int>(resultTerminalVertices.size());
                for (const auto& terminal : resultTerminalVertices) {
                    // Rekonstruiere Knotenfolge und Kanten entlang Elternzeiger
                    std::vector<std::string> vertices;
                    std::vector<std::string> edges;
                    std::string cur = terminal;
                    vertices.push_back(cur);
                    while (cur != t.startVertex) {
                        auto it = parent.find(cur);
                        if (it == parent.end()) break; // sollte bei Start aufh�ren
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
            // EXPLAIN/PROFILE: Traversal-Metriken anh�ngen
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
            
            traversalSpan.setAttribute("traversal.result_count", static_cast<int64_t>(res["count"].get<int>()));
            traversalSpan.setStatus(true);
            span.setAttribute("aql.result_count", static_cast<int64_t>(res["count"].get<int>()));
            span.setStatus(true);
            return makeResponse(http::status::ok, res.dump(), req);
        }

        // Disjunctive Query (OR support)
        if (translate_result.disjunctive.has_value()) {
            const auto& dq = translate_result.disjunctive.value();
            auto orSpan = Tracer::startSpan("aql.or_execution");
            orSpan.setAttribute("or.table", dq.table);
            orSpan.setAttribute("or.disjunct_count", static_cast<int64_t>(dq.disjuncts.size()));
            
            themis::QueryEngine engine(*storage_, *secondary_index_);
            // Nutze Fallback-Variante, damit OR-Queries auch ohne passende Indizes funktionieren
            auto [status, keys] = engine.executeOrKeysWithFallback(dq, optimize);
            
            if (!status.ok) {
                orSpan.setStatus(false, status.message);
                span.setStatus(false, "OR execution failed");
                return makeErrorResponse(http::status::bad_request, status.message, req);
            }
            
            // Fetch entities
            nlohmann::json entities = nlohmann::json::array();
            for (const auto& key : keys) {
                auto pk = themis::KeySchema::makeRelationalKey(dq.table, key);
                auto blob = storage_->get(pk);
                if (blob && !blob->empty()) {
                    try {
                        themis::BaseEntity::Blob entity_blob(blob->begin(), blob->end());
                        auto entity = themis::BaseEntity::deserialize(key, entity_blob);
                        entities.push_back(nlohmann::json::parse(entity.toJson()));
                    } catch (...) {
                        // Skip malformed entities
                    }
                }
            }
            
            nlohmann::json response_body = {
                {"table", dq.table},
                {"count", entities.size()},
                {"entities", entities}
            };
            // Provide "result" alias for compatibility with older clients/tests
            try { response_body["result"] = response_body["entities"]; } catch (...) { /* ignore */ }
            
            if (explain) {
                response_body["query"] = aql_query;
                response_body["ast"] = parse_result.query->toJSON();
                response_body["disjunctive_query"] = true;
                response_body["disjunct_count"] = dq.disjuncts.size();
            }
            
            orSpan.setAttribute("or.result_count", static_cast<int64_t>(entities.size()));
            orSpan.setStatus(true);
            span.setAttribute("aql.result_count", static_cast<int64_t>(entities.size()));
            span.setStatus(true);
            return makeResponse(http::status::ok, response_body.dump(), req);
        }

        // JOIN/LET Query (Multi-FOR or LET without COLLECT)
        // Note: Single-FOR + COLLECT is handled by conversion to ConjunctiveQuery below
        if (translate_result.join.has_value()) {
            const auto& jq = translate_result.join.value();
            
            // For single-FOR + COLLECT, convert back to ConjunctiveQuery and skip to standard path
            if (jq.for_nodes.size() == 1 && jq.collect) {
                // Reconstruct ConjunctiveQuery from JoinQuery
                themis::ConjunctiveQuery cq;
                cq.table = jq.for_nodes[0].collection;
                
                // Convert simple equality filters to predicates
                using namespace themis::query;
                for (const auto& filter : jq.filters) {
                    if (filter->condition->getType() == ASTNodeType::BinaryOp) {
                        auto bin = std::static_pointer_cast<BinaryOpExpr>(filter->condition);
                        if (bin->op == BinaryOperator::Eq) {
                            // Check for pattern: var.field == literal
                            if (bin->left->getType() == ASTNodeType::FieldAccess &&
                                bin->right->getType() == ASTNodeType::Literal) {
                                auto fa = std::static_pointer_cast<FieldAccessExpr>(bin->left);
                                auto lit = std::static_pointer_cast<LiteralExpr>(bin->right);
                                
                                // Extract field path
                                std::vector<std::string> parts;
                                parts.push_back(fa->field);
                                auto* cur = fa->object.get();
                                while (auto* fa2 = dynamic_cast<FieldAccessExpr*>(cur)) {
                                    parts.push_back(fa2->field);
                                    cur = fa2->object.get();
                                }
                                
                                // Verify it's rooted at the FOR variable
                                if (auto* rootVar = dynamic_cast<VariableExpr*>(cur)) {
                                    if (rootVar->name == jq.for_nodes[0].variable) {
                                        std::string col;
                                        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
                                            if (!col.empty()) col += ".";
                                            col += *it;
                                        }
                                        
                                        // Convert literal to string
                                        auto litToString = [](const LiteralValue& value)->std::string {
                                            return std::visit([](auto&& arg)->std::string {
                                                using T = std::decay_t<decltype(arg)>;
                                                if constexpr (std::is_same_v<T, std::nullptr_t>) return std::string("null");
                                                else if constexpr (std::is_same_v<T, bool>) return arg ? std::string("true") : std::string("false");
                                                else if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
                                                else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                                                else if constexpr (std::is_same_v<T, std::string>) return arg;
                                                else return std::string();
                                            }, value);
                                        };
                                        
                                        cq.predicates.push_back({col, litToString(lit->value)});
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Store in translate_result.query for standard processing
                translate_result.query = std::move(cq);
                // Clear join to fall through to standard path
                translate_result.join = std::nullopt;
            } else if (!jq.collect) {
                // Multi-FOR or single-FOR + LET: use executeJoin
                auto joinSpan = Tracer::startSpan("aql.join_execution");
                joinSpan.setAttribute("join.for_count", static_cast<int64_t>(jq.for_nodes.size()));
                joinSpan.setAttribute("join.let_count", static_cast<int64_t>(jq.let_nodes.size()));
                joinSpan.setAttribute("join.filter_count", static_cast<int64_t>(jq.filters.size()));
                
                themis::QueryEngine engine(*storage_, *secondary_index_);
                auto res = engine.executeJoin(
                    jq.for_nodes,
                    jq.filters,
                    jq.let_nodes,
                    jq.return_node,
                    jq.sort,
                    jq.limit
                );
                
                if (!res.first.ok) {
                    joinSpan.setStatus(false, res.first.message);
                    span.setStatus(false, "JOIN execution failed");
                    return makeErrorResponse(http::status::bad_request, res.first.message, req);
                }
                
                nlohmann::json response_body;
                nlohmann::json entities = nlohmann::json::array();
                for (const auto& result : res.second) {
                    entities.push_back(result);
                }
                
                // Determine table name for response (use first FOR collection)
                std::string table = jq.for_nodes.empty() ? std::string("unknown") : jq.for_nodes[0].collection;
                
                response_body = {
                    {"table", table},
                    {"count", entities.size()},
                    {"entities", entities}
                };
                
                if (explain) {
                    response_body["query"] = aql_query;
                    response_body["ast"] = parse_result.query->toJSON();
                    response_body["join_query"] = true;
                }
                
                joinSpan.setAttribute("join.result_count", static_cast<int64_t>(entities.size()));
                joinSpan.setStatus(true);
                span.setAttribute("aql.result_count", static_cast<int64_t>(entities.size()));
                span.setStatus(true);
                return makeResponse(http::status::ok, response_body.dump(), req);
            }
        }

        // Relationale Query (mutable Kopie f�r Cursor-Anker/Limit-Anpassungen)
    auto forSpan = Tracer::startSpan("aql.for");
    auto q = translate_result.query;

        // Detect function-based SORT (BM25(doc) or FULLTEXT_SCORE()) to avoid range-index ORDER BY
        bool sortByScoreFunction = false;
        bool sortAsc = true;
        if (parse_result.query && parse_result.query->sort && !parse_result.query->sort->specifications.empty()) {
            // Helper: recursively check if expression contains a specific function name
            std::function<bool(const std::shared_ptr<themis::query::Expression>&, const std::string&)> exprContainsFn;
            exprContainsFn = [&](const std::shared_ptr<themis::query::Expression>& expr, const std::string& name)->bool{
                if (!expr) return false;
                using namespace themis::query;
                switch (expr->getType()) {
                    case ASTNodeType::FunctionCall: {
                        auto* fc = static_cast<FunctionCallExpr*>(expr.get());
                        std::string n = fc->name; std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                        if (n == name) return true;
                        for (const auto& a : fc->arguments) if (exprContainsFn(a, name)) return true;
                        return false;
                    }
                    case ASTNodeType::BinaryOp: {
                        auto* bo = static_cast<BinaryOpExpr*>(expr.get());
                        return exprContainsFn(bo->left, name) || exprContainsFn(bo->right, name);
                    }
                    case ASTNodeType::UnaryOp: {
                        auto* u = static_cast<UnaryOpExpr*>(expr.get());
                        return exprContainsFn(u->operand, name);
                    }
                    case ASTNodeType::ArrayLiteral: {
                        auto* ar = static_cast<ArrayLiteralExpr*>(expr.get());
                        for (const auto& el : ar->elements) if (exprContainsFn(el, name)) return true;
                        return false;
                    }
                    case ASTNodeType::ObjectConstruct: {
                        auto* oc = static_cast<ObjectConstructExpr*>(expr.get());
                        for (const auto& kv : oc->fields) if (exprContainsFn(kv.second, name)) return true;
                        return false;
                    }
                    default:
                        return false;
                }
            };
            const auto& spec = parse_result.query->sort->specifications[0];
            sortAsc = spec.ascending;
            if (exprContainsFn(spec.expression, "bm25") || exprContainsFn(spec.expression, "fulltext_score")) {
                sortByScoreFunction = true;
            }
        }
        // If SORT uses BM25/FULLTEXT_SCORE, clear index ORDER BY and sort later in-memory
        if (sortByScoreFunction && q.orderBy.has_value()) {
            q.orderBy.reset();
        }

        std::string table = q.table;
        forSpan.setAttribute("for.table", table);
        forSpan.setAttribute("for.predicates_count", static_cast<int64_t>(q.predicates.size()));
        forSpan.setAttribute("for.range_predicates_count", static_cast<int64_t>(q.rangePredicates.size()));
        if (q.orderBy.has_value()) {
            forSpan.setAttribute("for.order_by", q.orderBy->column);
            forSpan.setAttribute("for.order_desc", q.orderBy->desc);
        }

        // Shared score map for BM25/FULLTEXT score lookups (filled on demand later)
        std::unordered_map<std::string, double> fulltextScoreByPk;

        // Cursor-Integration in die QueryEngine: falls ORDER BY vorhanden
        bool early_empty_due_to_cursor = false;
        size_t requested_count_for_cursor = 0;
        if (use_cursor && q.orderBy.has_value()) {
            // Bestimme angeforderte Seitengr��e aus LIMIT
            if (parse_result.query && parse_result.query->limit) {
                requested_count_for_cursor = static_cast<size_t>(std::max<int64_t>(1, parse_result.query->limit->count));
            } else {
                requested_count_for_cursor = 1000;
            }
            // Engine soll count+1 liefern (extra Element f�r has_more detection)
            // Add safety margin for cursor pagination: when using cursor with ORDER BY,
            // the query engine may filter items after cursor positioning (e.g., if there
            // are equality predicates), which can result in fewer items than requested.
            // Conservative approach: fetch extra items to account for potential filtering.
            constexpr size_t CURSOR_SAFETY_MARGIN = 5;
            size_t num_predicates = q.predicates.size();
            size_t safety_margin = (num_predicates > 0) ? CURSOR_SAFETY_MARGIN * num_predicates : CURSOR_SAFETY_MARGIN;
            q.orderBy->limit = requested_count_for_cursor + safety_margin + 1;

            // Wenn ein Cursor-Token �bergeben wurde, ermittle Anker (value, pk)
            if (!cursor_token.empty()) {
                auto decoded = themis::utils::Cursor::decode(cursor_token);
                if (!decoded.has_value()) {
                    // Ung�ltiger Cursor: leere Seite zur�ck
                    early_empty_due_to_cursor = true;
                } else {
                    auto [pk, collection] = *decoded;
                    if (collection != table) {
                        // Falsche Collection im Cursor
                        early_empty_due_to_cursor = true;
                    } else {
                        // Entit�t laden, um Sortierspaltenwert zu extrahieren
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
            // Liefere leere Seite sofort zur�ck
            res = { themis::QueryEngine::Status::OK(), {} };
        } else {
        
        if (allow_full_scan) {
            exec_mode = "full_scan_fallback";
            res = engine.executeAndEntitiesWithFallback(q, optimize);
        } else {
            // Wenn FULLTEXT vorhanden ist, delegiere direkt an Engine (Optimizer kennt FULLTEXT nicht)
            if (q.fulltextPredicate.has_value()) {
                exec_mode = "fulltext";
                res = engine.executeAndEntities(q);
            }
            // Range-aware: Wenn Range-Prädikate oder ORDER BY vorhanden sind,
            // nutze direkt die range-fähige Engine-Logik (Optimizer unterstützt nur Gleichheit).
            else if (!q.rangePredicates.empty() || q.orderBy.has_value()) {
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
            forSpan.setStatus(false, res.first.message);
            span.setStatus(false, "Query execution failed");
            return makeErrorResponse(http::status::bad_request, res.first.message, req);
        }
        
        forSpan.setAttribute("for.result_count", static_cast<int64_t>(res.second.size()));
        forSpan.setAttribute("for.exec_mode", exec_mode);
        forSpan.setStatus(true);
        
        // Apply LIMIT offset,count if provided in the AQL (post-fetch slicing)
        std::vector<themis::BaseEntity> sliced;
        sliced.reserve(res.second.size());
        sliced = std::move(res.second);

        // If SORT uses BM25/FULLTEXT_SCORE, compute scores and sort now (pre-LIMIT)
        if (sortByScoreFunction) {
            if (!q.fulltextPredicate.has_value()) {
                forSpan.setStatus(false, "BM25/FULLTEXT_SCORE sort without FULLTEXT filter");
                span.setStatus(false, "BM25/FULLTEXT_SCORE sort requires FULLTEXT() in FILTER");
                return makeErrorResponse(http::status::bad_request, "SORT by BM25/FULLTEXT_SCORE requires a FULLTEXT(...) filter in the query", req);
            }
            const auto& ft = *q.fulltextPredicate;
            auto scoreSpan = Tracer::startSpan("aql.fulltext_scores_fetch.sort");
            scoreSpan.setAttribute("table", q.table);
            scoreSpan.setAttribute("column", ft.column);
            scoreSpan.setAttribute("limit", static_cast<int64_t>(ft.limit));
            auto [st, results] = secondary_index_->scanFulltextWithScores(q.table, ft.column, ft.query, ft.limit);
            if (!st.ok) {
                scoreSpan.setStatus(false, st.message);
                return makeErrorResponse(http::status::internal_server_error, std::string("Failed to fetch fulltext scores: ") + st.message, req);
            }
            fulltextScoreByPk.clear();
            fulltextScoreByPk.reserve(results.size());
            for (const auto& r : results) fulltextScoreByPk.emplace(r.pk, r.score);
            scoreSpan.setAttribute("count", static_cast<int64_t>(results.size()));
            scoreSpan.setStatus(true);

            std::sort(sliced.begin(), sliced.end(), [&](const themis::BaseEntity& a, const themis::BaseEntity& b){
                double sa = 0.0, sb = 0.0;
                auto ita = fulltextScoreByPk.find(a.getPrimaryKey()); if (ita != fulltextScoreByPk.end()) sa = ita->second;
                auto itb = fulltextScoreByPk.find(b.getPrimaryKey()); if (itb != fulltextScoreByPk.end()) sb = itb->second;
                if (sortAsc) return sa < sb; else return sa > sb;
            });
        }

        if (!use_cursor && parse_result.query && parse_result.query->limit) {
            auto limitSpan = Tracer::startSpan("aql.limit");
            // Klassisches LIMIT offset,count Verhalten
            auto off = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->offset));
            auto cnt = static_cast<size_t>(std::max<int64_t>(0, parse_result.query->limit->count));
            limitSpan.setAttribute("limit.offset", static_cast<int64_t>(off));
            limitSpan.setAttribute("limit.count", static_cast<int64_t>(cnt));
            limitSpan.setAttribute("limit.input_count", static_cast<int64_t>(sliced.size()));
            
            if (off < sliced.size()) {
                size_t last = std::min(sliced.size(), off + cnt);
                std::vector<themis::BaseEntity> tmp;
                tmp.reserve(last - off);
                for (size_t i = off; i < last; ++i) tmp.emplace_back(std::move(sliced[i]));
                sliced.swap(tmp);
            } else {
                sliced.clear();
            }
            
            limitSpan.setAttribute("limit.output_count", static_cast<int64_t>(sliced.size()));
            limitSpan.setStatus(true);
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

        // COLLECT/GROUP BY (MVP): falls vorhanden, f�hre In-Memory-Gruppierung/Aggregation �ber die Ergebnisse aus
        if (parse_result.query && parse_result.query->collect && !use_cursor) {
            auto collectSpan = Tracer::startSpan("aql.collect");
            const auto& collect = *parse_result.query->collect;
            using namespace themis::query;
            
            collectSpan.setAttribute("collect.input_count", static_cast<int64_t>(sliced.size()));
            collectSpan.setAttribute("collect.group_by_count", static_cast<int64_t>(collect.groups.size()));
            collectSpan.setAttribute("collect.aggregates_count", static_cast<int64_t>(collect.aggregations.size()));

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

            // MVP: Unterst�tze 0..1 Group-Variablen
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
            
            collectSpan.setAttribute("collect.group_count", static_cast<int64_t>(groups.size()));
            collectSpan.setStatus(true);
            span.setAttribute("aql.result_count", static_cast<int64_t>(groups.size()));
            span.setStatus(true);
            return makeResponse(http::status::ok, response_body.dump(), req);
        }

        // Serialize entities or projections with LET support
        auto returnSpan = Tracer::startSpan("aql.return");
        returnSpan.setAttribute("return.input_count", static_cast<int64_t>(sliced.size()));

        using namespace themis::query;
        const std::string loopVar = parse_result.query->for_node.variable;

        // Helper: extract column path from FieldAccess rooted at loop var
        std::function<std::optional<std::string>(const std::shared_ptr<Expression>&, bool&)> extractColFromFA;
        extractColFromFA = [&](const std::shared_ptr<Expression>& expr, bool& rootedAtLoop)->std::optional<std::string> {
            auto* fa = dynamic_cast<FieldAccessExpr*>(expr.get()); if (!fa) return std::nullopt;
            std::vector<std::string> parts; parts.push_back(fa->field);
            auto* cur = fa->object.get();
            while (auto* fa2 = dynamic_cast<FieldAccessExpr*>(cur)) { parts.push_back(fa2->field); cur = fa2->object.get(); }
            if (auto* rootVarExpr = dynamic_cast<VariableExpr*>(cur)) { rootedAtLoop = (rootVarExpr->name == loopVar); }
            else { rootedAtLoop = false; }
            std::string col; for (auto it = parts.rbegin(); it != parts.rend(); ++it) { if (!col.empty()) col += "."; col += *it; }
            return col;
        };

        // Detect if RETURN/LET expressions reference FULLTEXT_SCORE()
        std::function<bool(const std::shared_ptr<Expression>&, const std::string&)> containsFunction;
        containsFunction = [&](const std::shared_ptr<Expression>& expr, const std::string& name)->bool{
            if (!expr) return false;
            switch (expr->getType()) {
                case ASTNodeType::FunctionCall: {
                    auto* fc = static_cast<FunctionCallExpr*>(expr.get());
                    std::string n = fc->name; std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    if (n == name) return true;
                    for (const auto& a : fc->arguments) if (containsFunction(a, name)) return true;
                    return false;
                }
                case ASTNodeType::BinaryOp: {
                    auto* bo = static_cast<BinaryOpExpr*>(expr.get());
                    return containsFunction(bo->left, name) || containsFunction(bo->right, name);
                }
                case ASTNodeType::UnaryOp: {
                    auto* u = static_cast<UnaryOpExpr*>(expr.get());
                    return containsFunction(u->operand, name);
                }
                case ASTNodeType::ArrayLiteral: {
                    auto* ar = static_cast<ArrayLiteralExpr*>(expr.get());
                    for (const auto& el : ar->elements) if (containsFunction(el, name)) return true;
                    return false;
                }
                case ASTNodeType::ObjectConstruct: {
                    auto* oc = static_cast<ObjectConstructExpr*>(expr.get());
                    for (const auto& kv : oc->fields) if (containsFunction(kv.second, name)) return true;
                    return false;
                }
                default:
                    return false;
            }
        };

        bool usesFulltextScore = false;
        if (parse_result.query) {
            // RETURN
            if (parse_result.query->return_node && parse_result.query->return_node->expression) {
                usesFulltextScore = containsFunction(parse_result.query->return_node->expression, "fulltext_score");
            }
            // LETs
            if (!usesFulltextScore) {
                for (const auto& ln : parse_result.query->let_nodes) {
                    if (containsFunction(ln.expression, "fulltext_score")) { usesFulltextScore = true; break; }
                }
            }
        }

        // If FULLTEXT_SCORE() or BM25() is referenced in RETURN/LET, ensure scores are prepared (and validate FULLTEXT_SCORE usage)
        bool usesScoreFn = usesFulltextScore;
        if (!usesScoreFn && parse_result.query) {
            if (parse_result.query->return_node && parse_result.query->return_node->expression) {
                usesScoreFn = containsFunction(parse_result.query->return_node->expression, "bm25");
            }
            if (!usesScoreFn) {
                for (const auto& ln : parse_result.query->let_nodes) {
                    if (containsFunction(ln.expression, "bm25")) { usesScoreFn = true; break; }
                }
            }
        }
        if (usesFulltextScore && !q.fulltextPredicate.has_value()) {
            forSpan.setStatus(false, "FULLTEXT_SCORE without FULLTEXT filter");
            span.setStatus(false, "FULLTEXT_SCORE requires FULLTEXT() in FILTER");
            return makeErrorResponse(http::status::bad_request, "FULLTEXT_SCORE() requires a FULLTEXT(...) filter in the query", req);
        }
        if ((usesScoreFn || sortByScoreFunction) && fulltextScoreByPk.empty() && q.fulltextPredicate.has_value()) {
            const auto& ft = *q.fulltextPredicate;
            auto scoreSpan = Tracer::startSpan("aql.fulltext_scores_fetch");
            scoreSpan.setAttribute("table", q.table);
            scoreSpan.setAttribute("column", ft.column);
            scoreSpan.setAttribute("limit", static_cast<int64_t>(ft.limit));
            auto [st, results] = secondary_index_->scanFulltextWithScores(q.table, ft.column, ft.query, ft.limit);
            if (!st.ok) {
                scoreSpan.setStatus(false, st.message);
                return makeErrorResponse(http::status::internal_server_error, std::string("Failed to fetch fulltext scores: ") + st.message, req);
            }
            for (const auto& r : results) fulltextScoreByPk.emplace(r.pk, r.score);
            scoreSpan.setAttribute("count", static_cast<int64_t>(results.size()));
            scoreSpan.setStatus(true);
        }

        // Evaluate expressions to JSON (Literal, Variable, FieldAccess, Binary/Unary, Object, Array, selected FunctionCall)
        std::function<nlohmann::json(const std::shared_ptr<Expression>&,
                                     const themis::BaseEntity&,
                                     const std::unordered_map<std::string, nlohmann::json>&)> evalExpr;
        evalExpr = [&](const std::shared_ptr<Expression>& expr,
                       const themis::BaseEntity& ent,
                       const std::unordered_map<std::string, nlohmann::json>& env)->nlohmann::json {
            if (!expr) return nlohmann::json();
            switch (expr->getType()) {
                case ASTNodeType::Literal: {
                    // reuse toJSON Value
                    return static_cast<LiteralExpr*>(expr.get())->toJSON()["value"]; }
                case ASTNodeType::Variable: {
                    auto* v = static_cast<VariableExpr*>(expr.get());
                    if (v->name == loopVar) return ent.toJson();
                    auto it = env.find(v->name); if (it != env.end()) return it->second; return nullptr; }
                case ASTNodeType::FieldAccess: {
                    bool rooted = false; auto colOpt = extractColFromFA(expr, rooted);
                    if (colOpt.has_value() && rooted) {
                        // Extract from entity
                        auto asDouble = ent.getFieldAsDouble(*colOpt); if (asDouble.has_value()) return *asDouble;
                        auto asStr = ent.getFieldAsString(*colOpt); if (asStr.has_value()) return *asStr; return nullptr;
                    } else {
                        // Evaluate object part and index JSON
                        auto* fa = static_cast<FieldAccessExpr*>(expr.get());
                        auto base = evalExpr(fa->object, ent, env);
                        if (base.is_object()) {
                            auto it = base.find(fa->field); if (it != base.end()) return *it; return nullptr;
                        }
                        return nullptr;
                    }
                }
                case ASTNodeType::BinaryOp: {
                    auto* bo = static_cast<BinaryOpExpr*>(expr.get());
                    auto left = evalExpr(bo->left, ent, env);
                    auto right = evalExpr(bo->right, ent, env);
                    auto toNumber = [](const nlohmann::json& j, double& out)->bool{
                        if (j.is_number()) { out = j.get<double>(); return true; }
                        if (j.is_boolean()) { out = j.get<bool>() ? 1.0 : 0.0; return true; }
                        if (j.is_string()) { char* end=nullptr; std::string s=j.get<std::string>(); out = strtod(s.c_str(), &end); return end && *end=='\0'; }
                        return false;
                    };
                    switch (bo->op) {
                        case BinaryOperator::Eq:  return left == right;
                        case BinaryOperator::Neq: return left != right;
                        case BinaryOperator::Lt:  return left < right;
                        case BinaryOperator::Lte: return left <= right;
                        case BinaryOperator::Gt:  return left > right;
                        case BinaryOperator::Gte: return left >= right;
                        case BinaryOperator::And: {
                            bool lb = left.is_boolean() ? left.get<bool>() : (!left.is_null());
                            bool rb = right.is_boolean() ? right.get<bool>() : (!right.is_null());
                            return nlohmann::json(lb && rb);
                        }
                        case BinaryOperator::Or:  {
                            bool lb = left.is_boolean() ? left.get<bool>() : (!left.is_null());
                            bool rb = right.is_boolean() ? right.get<bool>() : (!right.is_null());
                            return nlohmann::json(lb || rb);
                        }
                        case BinaryOperator::Add: {
                            double a,b; if (toNumber(left,a) && toNumber(right,b)) return a+b; return nullptr; }
                        case BinaryOperator::Sub: {
                            double a,b; if (toNumber(left,a) && toNumber(right,b)) return a-b; return nullptr; }
                        case BinaryOperator::Mul: {
                            double a,b; if (toNumber(left,a) && toNumber(right,b)) return a*b; return nullptr; }
                        case BinaryOperator::Div: {
                            double a,b; if (toNumber(left,a) && toNumber(right,b) && b!=0.0) return a/b; return nullptr; }
                        default: return nullptr;
                    }
                }
                case ASTNodeType::UnaryOp: {
                    auto* u = static_cast<UnaryOpExpr*>(expr.get());
                    auto val = evalExpr(u->operand, ent, env);
                    switch (u->op) {
                        case UnaryOperator::Not:   return val.is_boolean() ? nlohmann::json(!val.get<bool>()) : nlohmann::json(false);
                        case UnaryOperator::Minus: {
                            if (val.is_number()) return -val.get<double>();
                            if (val.is_string()) { char* end=nullptr; std::string s=val.get<std::string>(); double d=strtod(s.c_str(), &end); if (end && *end=='\0') return -d; }
                            return nullptr; }
                        case UnaryOperator::Plus:  {
                            if (val.is_number()) return val.get<double>();
                            if (val.is_string()) { char* end=nullptr; std::string s=val.get<std::string>(); double d=strtod(s.c_str(), &end); if (end && *end=='\0') return d; }
                            return nullptr; }
                        default: return nullptr;
                    }
                }
                case ASTNodeType::FunctionCall: {
                    auto* fc = static_cast<FunctionCallExpr*>(expr.get());
                    std::string name = fc->name; std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (name == "bm25") {
                        // One-arg function: BM25(doc). Returns score for provided document object by _key/_pk
                        if (fc->arguments.size() != 1) return 0.0;
                        auto arg = evalExpr(fc->arguments[0], ent, env);
                        if (arg.is_object()) {
                            std::string pk;
                            if (arg.contains("_key") && arg["_key"].is_string()) pk = arg["_key"].get<std::string>();
                            else if (arg.contains("_pk") && arg["_pk"].is_string()) pk = arg["_pk"].get<std::string>();
                            if (!pk.empty()) {
                                auto it = fulltextScoreByPk.find(pk);
                                if (it != fulltextScoreByPk.end()) return it->second; else return 0.0;
                            }
                        }
                        return 0.0;
                    }
                    if (name == "fulltext_score") {
                        // No-arg function returning score of current entity (requires FULLTEXT filter)
                        auto it = fulltextScoreByPk.find(ent.getPrimaryKey());
                        if (it != fulltextScoreByPk.end()) return it->second; else return 0.0; // default 0.0 when not present
                    }
                    auto evalArg = [&](size_t i)->nlohmann::json{ return (i<fc->arguments.size()) ? evalExpr(fc->arguments[i], ent, env) : nlohmann::json(); };
                    if (name == "concat") {
                        std::string out;
                        for (size_t i=0;i<fc->arguments.size();++i) {
                            auto a = evalArg(i);
                            if (a.is_string()) out += a.get<std::string>();
                            else if (a.is_number()) out += std::to_string(a.get<double>());
                            else if (a.is_boolean()) out += (a.get<bool>()?"true":"false");
                        }
                        return out;
                    }
                    if (name == "substring" || name == "substr") {
                        auto s = evalArg(0); auto off = evalArg(1); auto len = evalArg(2);
                        if (!s.is_string()) return nullptr; std::string str = s.get<std::string>();
                        int start = off.is_number_integer() ? static_cast<int>(off.get<int64_t>()) : 0;
                        int count = len.is_number_integer() ? static_cast<int>(len.get<int64_t>()) : static_cast<int>(str.size() - std::min<int>(start,(int)str.size()));
                        if (start < 0) start = 0; if (start > (int)str.size()) start = (int)str.size();
                        if (count < 0) count = 0; if (start + count > (int)str.size()) count = (int)str.size() - start;
                        return str.substr(static_cast<size_t>(start), static_cast<size_t>(count));
                    }
                    if (name == "length") {
                        auto s = evalArg(0);
                        if (s.is_string()) return static_cast<int64_t>(s.get<std::string>().size());
                        if (s.is_array()) return static_cast<int64_t>(s.size());
                        if (s.is_object()) return static_cast<int64_t>(s.size());
                        return 0;
                    }
                    if (name == "lower") {
                        auto s = evalArg(0); if (!s.is_string()) return nullptr; std::string t=s.get<std::string>();
                        std::transform(t.begin(), t.end(), t.begin(), ::tolower); return t;
                    }
                    if (name == "upper") {
                        auto s = evalArg(0); if (!s.is_string()) return nullptr; std::string t=s.get<std::string>();
                        std::transform(t.begin(), t.end(), t.begin(), ::toupper); return t;
                    }
                    if (name == "to_number") {
                        auto v = evalArg(0); if (v.is_number()) return v.get<double>(); if (v.is_boolean()) return v.get<bool>()?1.0:0.0; if (v.is_string()) { char* end=nullptr; std::string s=v.get<std::string>(); double d=strtod(s.c_str(), &end); if (end && *end=='\0') return d; }
                        return nullptr;
                    }
                    if (name == "to_string") {
                        auto v = evalArg(0); if (v.is_string()) return v; if (v.is_number()) return std::to_string(v.get<double>()); if (v.is_boolean()) return v.get<bool>()?"true":"false"; if (v.is_null()) return "null"; return v.dump();
                    }
                    if (name == "abs" || name == "ceil" || name == "floor" || name == "round") {
                        auto v = evalArg(0); if (!v.is_number()) return nullptr; double d = v.get<double>();
                        if (name == "abs") return std::abs(d);
                        if (name == "ceil") return std::ceil(d);
                        if (name == "floor") return std::floor(d);
                        if (name == "round") return std::llround(d);
                        return nullptr;
                    }
                    if (name == "coalesce") {
                        for (size_t i=0;i<fc->arguments.size();++i) { auto a = evalArg(i); if (!a.is_null()) return a; }
                        return nullptr;
                    }
                    // Unsupported function in MVP eval
                    return nullptr;
                }
                case ASTNodeType::ObjectConstruct: {
                    auto* oc = static_cast<ObjectConstructExpr*>(expr.get());
                    nlohmann::json obj = nlohmann::json::object();
                    for (const auto& kv : oc->fields) obj[kv.first] = evalExpr(kv.second, ent, env);
                    return obj; }
                case ASTNodeType::ArrayLiteral: {
                    auto* ar = static_cast<ArrayLiteralExpr*>(expr.get());
                    nlohmann::json arr = nlohmann::json::array();
                    for (const auto& el : ar->elements) arr.push_back(evalExpr(el, ent, env));
                    return arr; }
                default:
                    return nullptr;
            }
        };

        // Decide if we can fast-path return of the loop variable
        bool simpleReturnLoopVar = false;
        if (parse_result.query->return_node && parse_result.query->return_node->expression) {
            if (auto* v = dynamic_cast<VariableExpr*>(parse_result.query->return_node->expression.get())) {
                simpleReturnLoopVar = (v->name == loopVar) && (parse_result.query->let_nodes.empty());
            }
        }

        json entities = json::array();
        if (simpleReturnLoopVar) {
            for (const auto& e : sliced) entities.push_back(e.toJson());
        } else {
            for (const auto& e : sliced) {
                // Build LET environment per row
                std::unordered_map<std::string, nlohmann::json> env;
                for (const auto& ln : parse_result.query->let_nodes) {
                    // Only allow Literal, FieldAccess and Variable
                    auto val = evalExpr(ln.expression, e, env);
                    env[ln.variable] = std::move(val);
                }
                // Evaluate RETURN expression; if missing, default to loop var entity
                if (parse_result.query->return_node && parse_result.query->return_node->expression) {
                    auto out = evalExpr(parse_result.query->return_node->expression, e, env);
                    entities.push_back(std::move(out));
                } else {
                    entities.push_back(e.toJson());
                }
            }
        }
        
        returnSpan.setStatus(true);
        
        json response_body;
        
        if (use_cursor) {
            // Cursor-basierte Antwort wurde nach Engine-Paginierung erstellt
            themis::utils::PaginatedResponse paged;
            // Bestimme angeforderte Seitengr��e
            size_t requested_count = parse_result.query && parse_result.query->limit 
                ? static_cast<size_t>(std::max<int64_t>(1, parse_result.query->limit->count))
                : 1000;

            bool has_more = false;
            if (sliced.size() > requested_count) {
                has_more = true;
                // Trenne das +1 Element ab (nur f�r has_more Erkennung)
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
            // Provide "result" alias for compatibility
            try { response_body["result"] = response_body["entities"]; } catch (...) { /* ignore */ }
        }
        
        if (explain) {
            response_body["query"] = aql_query;
            response_body["ast"] = parse_result.query->toJSON();
            if (!plan_json.is_null()) {
                // Markiere, wenn LET-Filter vor der Übersetzung extrahiert wurden (MVP-Sonderpfad)
                if (letFilterHandled) {
                    try { plan_json["let_pre_extracted"] = true; } catch (...) { /* noop */ }
                }
                response_body["plan"] = plan_json;
            }
        }
        
    span.setAttribute("aql.result_count", static_cast<int64_t>(sliced.size()));
    span.setStatus(true);
    auto final_res = makeResponse(http::status::ok, response_body.dump(), req);
        // Record page fetch time histogram for cursor-based pagination
        if (use_cursor) {
            auto end = std::chrono::steady_clock::now();
            auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - page_fetch_start);
            recordPageFetch(dur_ms);
        }
        return final_res;
        
    } catch (const json::exception& e) {
        span.recordError("JSON parse error: " + std::string(e.what()));
    span.setStatus(false);
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    }
}

http::response<http::string_body> HttpServer::handleGraphTraverse(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleGraphTraverse");
    span.setAttribute("http.method", "POST");
    span.setAttribute("http.path", "/graph/traverse");
    
    try {
        auto body_json = json::parse(req.body());
        
        if (!body_json.contains("start_vertex") || !body_json.contains("max_depth")) {
            span.setAttribute("error", "missing_required_fields");
            span.setStatus(false, "Missing required fields");
            return makeErrorResponse(http::status::bad_request,
                "Missing 'start_vertex' or 'max_depth'", req);
        }

        std::string start_vertex = body_json["start_vertex"];
        size_t max_depth = body_json["max_depth"];
        
        span.setAttribute("graph.start_vertex", start_vertex);
        span.setAttribute("graph.max_depth", static_cast<int64_t>(max_depth));

        // Perform BFS traversal
        auto [status, visited] = graph_index_->bfs(start_vertex, static_cast<int>(max_depth));

        if (!status.ok) {
            span.setAttribute("error", "traversal_failed");
            span.setStatus(false, status.message);
            span.setStatus(false);
            return makeErrorResponse(http::status::internal_server_error,
                "Traversal failed", req);
        }
        
        span.setAttribute("graph.visited_count", static_cast<int64_t>(visited.size()));
    span.setStatus(true);

        json response = {
            {"start_vertex", start_vertex},
            {"max_depth", max_depth},
            {"visited_count", visited.size()},
            {"visited", visited}
        };
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const json::exception& e) {
        span.recordError("JSON parse error: " + std::string(e.what()));
    span.setStatus(false);
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        span.recordError(e.what());
    span.setStatus(false);
        THEMIS_ERROR("Graph traverse error: {}", e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorSearch(
    const http::request<http::string_body>& req
) {
    if (auth_) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "data:read", "vector.search", path_only)) return *resp;
    }
    auto span = Tracer::startSpan("handleVectorSearch");
    span.setAttribute("http.method", "POST");
    span.setAttribute("http.path", "/vector/search");
    
    try {
        // Governance enforcement: block ANN for certain classifications in enforce mode
        auto to_lower = [](std::string s){ for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c))); return s; };
        std::string classification;
        std::string mode = "observe";
        for (const auto& h : req) {
            auto name = h.name_string();
            if (beast::iequals(name, "X-Classification")) classification = to_lower(std::string(h.value()));
            else if (beast::iequals(name, "X-Governance-Mode")) mode = to_lower(std::string(h.value()));
        }
        if (mode == "enforce") {
            if (classification == "geheim" || classification == "streng-geheim") {
                nlohmann::json j = {{"error","policy_denied"},{"message","ANN blocked by classification"}};
                auto res = makeResponse(http::status::forbidden, j.dump(), req);
                return res;
            }
        }

        auto body_json = json::parse(req.body());
        
        // Validate required fields
        if (!body_json.contains("vector")) {
            span.setAttribute("error", "missing_vector_field");
            span.setStatus(false, "Missing vector field");
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
                    span.setStatus(false, "Invalid vector element");
                    return makeErrorResponse(http::status::bad_request,
                        "Vector elements must be numbers", req);
                }
            }
        } else {
            span.setStatus(false, "Vector must be array");
            return makeErrorResponse(http::status::bad_request,
                "Field 'vector' must be an array", req);
        }
        
        // Parse k (default: 10)
        size_t k = body_json.value("k", 10);
            span.setAttribute("vector.k", static_cast<int64_t>(k));
            span.setAttribute("vector.dimension", static_cast<int64_t>(queryVector.size()));
        
    if (k == 0) {
        span.setAttribute("error", "invalid_k_value");
        span.setStatus(false, "K must be greater than 0");
            return makeErrorResponse(http::status::bad_request,
                "Field 'k' must be greater than 0", req);
        }
        
        // Validate dimension
        int expectedDim = vector_index_->getDimension();
        if (expectedDim > 0 && static_cast<int>(queryVector.size()) != expectedDim) {
            span.setStatus(false, "Dimension mismatch");
            return makeErrorResponse(http::status::bad_request,
                "Vector dimension mismatch: expected " + std::to_string(expectedDim) +
                ", got " + std::to_string(queryVector.size()), req);
        }
        
        // Optional cursor-based pagination for vector search
        bool use_cursor = body_json.value("use_cursor", false);
        size_t offset = 0;
        if (use_cursor && body_json.contains("cursor")) {
            try {
                // Einfaches Cursor-Format: numerischer Offset als String
                std::string cur = body_json["cursor"].get<std::string>();
                offset = static_cast<size_t>(std::stoull(cur));
            } catch (...) {
                offset = 0;
            }
        }

        size_t want_k = use_cursor ? (k + offset + 1) : k;

        // Perform k-NN search (ggf. mit erweitertem k für Pagination)
        auto [status, results] = vector_index_->searchKnn(queryVector, want_k);
        
        if (!status.ok) {
            span.setStatus(false, status.message);
            return makeErrorResponse(http::status::internal_server_error,
                "Vector search failed: " + status.message, req);
        }
        
        if (use_cursor) {
            // Slice [offset, offset+k) und Cursor-Felder setzen
            json items = json::array();
            size_t start = std::min(offset, results.size());
            size_t end = std::min(results.size(), start + k);
            for (size_t i = start; i < end; ++i) {
                items.push_back({{"pk", results[i].pk}, {"distance", results[i].distance}});
            }
            bool has_more = results.size() > end;
            json response = {
                {"items", items},
                {"batch_size", end - start},
                {"has_more", has_more}
            };
            if (has_more) response["next_cursor"] = std::to_string(end);
            span.setAttribute("vector.results_count", static_cast<int64_t>(end - start));
            span.setStatus(true);
            return makeResponse(http::status::ok, response.dump(), req);
        } else {
            // Legacy Format
            json resultJson = json::array();
            for (const auto& result : results) {
                resultJson.push_back({{"pk", result.pk}, {"distance", result.distance}});
            }
            json response = {{"results", resultJson}, {"k", k}, {"count", results.size()}};
            span.setAttribute("vector.results_count", static_cast<int64_t>(results.size()));
            span.setStatus(true);
            return makeResponse(http::status::ok, response.dump(), req);
        }

    } catch (const json::exception& e) {
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::bad_request,
            "Invalid JSON: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        span.setStatus(false, e.what());
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
        std::string metricStr;
        if (vector_index_->getMetric() == themis::VectorIndexManager::Metric::L2) {
            metricStr = "L2";
        } else if (vector_index_->getMetric() == themis::VectorIndexManager::Metric::DOT) {
            metricStr = "DOT";
        } else {
            metricStr = "COSINE";
        }
        
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
        std::string metricStr;
        if (vector_index_->getMetric() == themis::VectorIndexManager::Metric::L2) {
            metricStr = "L2";
        } else if (vector_index_->getMetric() == themis::VectorIndexManager::Metric::DOT) {
            metricStr = "DOT";
        } else {
            metricStr = "COSINE";
        }
        
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

http::response<http::string_body> HttpServer::handleVectorBatchInsert(
    const http::request<http::string_body>& req
) {
    if (auth_) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "data:write", "vector.write", path_only)) return *resp;
    }
    auto span = Tracer::startSpan("handleVectorBatchInsert");
    span.setAttribute("http.method", "POST");
    span.setAttribute("http.path", "/vector/batch_insert");

    try {
        auto body = json::parse(req.body());

        if (!body.contains("items") || !body["items"].is_array()) {
            span.setStatus(false, "missing_items");
            return makeErrorResponse(http::status::bad_request, "Missing required field: items (array)", req);
        }

        std::string vector_field = body.value("vector_field", std::string("embedding"));
        std::string object_name = vector_index_->getObjectName();
        int configured_dim = vector_index_->getDimension();
        size_t inserted = 0;
        size_t errors = 0;

        // Optionale Auto-Init falls noch nicht konfiguriert
        if (configured_dim <= 0) {
            // Dimension aus dem ersten Element ableiten
            for (const auto& it : body["items"]) {
                if (it.contains("vector") && it["vector"].is_array()) {
                    int dim = static_cast<int>(it["vector"].size());
                    if (dim > 0) {
                        auto st = vector_index_->init("vectors", dim, themis::VectorIndexManager::Metric::COSINE);
                        if (!st.ok) {
                            span.setStatus(false, st.message);
                            return makeErrorResponse(http::status::internal_server_error, std::string("Failed to init vector index: ") + st.message, req);
                        }
                        configured_dim = dim;
                        object_name = vector_index_->getObjectName();
                    }
                    break;
                }
            }
            if (configured_dim <= 0) {
                span.setStatus(false, "cannot_infer_dim");
                return makeErrorResponse(http::status::bad_request, "Cannot infer dimension from items", req);
            }
        }

        // Use a single WriteBatch for higher throughput
        // Load optional encryption schema once (for vector metadata encryption)
        json vector_enc_cfg;
        bool vector_enc_enabled = false;
        std::vector<std::string> vector_enc_fields;
        try {
            if (auto schema_bytes = storage_->get("config:encryption_schema")) {
                std::string s(schema_bytes->begin(), schema_bytes->end());
                auto schema_json = json::parse(s);
                if (schema_json.contains("collections") && schema_json["collections"].is_object()) {
                    // Collection name: use object_name resolved after possible auto-init (e.g. "vectors" or custom)
                    if (schema_json["collections"].contains(object_name)) {
                        auto coll = schema_json["collections"][object_name];
                        if (coll.contains("encryption") && coll["encryption"].is_object()) {
                            auto ecfg = coll["encryption"];
                            vector_enc_enabled = ecfg.value("enabled", false);
                            if (ecfg.contains("fields") && ecfg["fields"].is_array()) {
                                for (const auto& f : ecfg["fields"]) if (f.is_string()) vector_enc_fields.push_back(f.get<std::string>());
                            }
                        }
                        // Backward-compatible schema: { collections: { name: { fields: { fld: { encrypt: true } } } } }
                        if (!vector_enc_enabled && coll.contains("fields") && coll["fields"].is_object()) {
                            for (auto itf = coll["fields"].begin(); itf != coll["fields"].end(); ++itf) {
                                try {
                                    if (itf.value().is_object() && itf.value().value("encrypt", false)) {
                                        vector_enc_fields.push_back(itf.key());
                                    }
                                } catch (...) { /* ignore */ }
                            }
                            vector_enc_enabled = !vector_enc_fields.empty();
                        }
                    }
                }
            }
        } catch (...) {
            vector_enc_enabled = false; // fail-safe
        }

        // Extract auth context for user-based HKDF (salt = user_id) if encryption active
        std::string enc_user_ctx;
        if (vector_enc_enabled) {
            auto auth_ctx = extractAuthContext(req);
            enc_user_ctx = auth_ctx.user_id.empty() ? "anonymous" : auth_ctx.user_id;
        }

        auto batch = storage_->createWriteBatch();
        for (const auto& it : body["items"]) {
            try {
                if (!it.contains("pk") || !it["pk"].is_string()) { ++errors; continue; }
                if (!it.contains("vector") || !it["vector"].is_array()) { ++errors; continue; }

                std::string pk = it["pk"].get<std::string>();
                std::vector<float> vec;
                vec.reserve(it["vector"].size());
                for (const auto& v : it["vector"]) {
                    if (!v.is_number()) { vec.clear(); break; }
                    vec.push_back(v.get<float>());
                }
                if (vec.empty() || static_cast<int>(vec.size()) != configured_dim) { ++errors; continue; }

                // Build entity
                BaseEntity e(pk);
                e.setField(vector_field, vec);
                if (it.contains("fields") && it["fields"].is_object()) {
                    for (auto fit = it["fields"].begin(); fit != it["fields"].end(); ++fit) {
                        const auto& val = fit.value();
                        const std::string key = fit.key();
                        if (val.is_string()) e.setField(key, val.get<std::string>());
                        else if (val.is_number_integer()) e.setField(key, static_cast<int64_t>(val.get<int64_t>()));
                        else if (val.is_number_float()) e.setField(key, val.get<double>());
                        else if (val.is_boolean()) e.setField(key, val.get<bool>());
                    }
                }

                // Vector metadata encryption (schema-driven) - do NOT encrypt actual embedding
                if (vector_enc_enabled && !vector_enc_fields.empty() && field_encryption_) {
                    for (const auto& mf : vector_enc_fields) {
                        if (mf == vector_field) continue; // never encrypt embedding itself
                        if (!e.hasField(mf)) continue;
                        auto valOpt = e.getField(mf);
                        if (!valOpt.has_value()) continue;
                        // Serialize value to string (reuse logic similar to handlePutEntity for primitives)
                        std::string plain_str;
                        const auto& v = *valOpt;
                        if (std::holds_alternative<std::string>(v)) plain_str = std::get<std::string>(v);
                        else if (std::holds_alternative<int64_t>(v)) plain_str = std::to_string(std::get<int64_t>(v));
                        else if (std::holds_alternative<double>(v)) plain_str = std::to_string(std::get<double>(v));
                        else if (std::holds_alternative<bool>(v)) plain_str = std::get<bool>(v) ? "true" : "false";
                        else {
                            // skip unsupported complex types in metadata for now
                            continue;
                        }
                        try {
                            // Derive field key: HKDF(DEK, user_id, "field:"+mf)
                            auto dek = key_provider_->getKey("dek");
                            std::vector<uint8_t> salt;
                            if (!enc_user_ctx.empty()) salt.assign(enc_user_ctx.begin(), enc_user_ctx.end());
                            std::string info = std::string("field:") + mf;
                            auto raw_key = utils::HKDFHelper::derive(dek, salt, info, 32);
                            auto blob = field_encryption_->encryptWithKey(plain_str, "vector_meta:" + mf, 1, raw_key);
                            auto j = blob.toJson();
                            e.setField(mf + "_encrypted", j.dump());
                            e.setField(mf + "_enc", true);
                            // remove plaintext
                            e.setField(mf, std::monostate{});
                        } catch (const std::exception& ex) {
                            THEMIS_WARN("Vector metadata encryption failed for {}: {}", mf, ex.what());
                        }
                    }
                }

                auto st = vector_index_->addEntity(e, *batch, vector_field);
                if (st.ok) ++inserted; else { ++errors; }
            } catch (...) {
                ++errors;
            }
        }
        // Commit once for the whole batch
        if (!batch->commit()) {
            span.setStatus(false, "batch_commit_failed");
            return makeErrorResponse(http::status::internal_server_error, "Vector batch commit failed", req);
        }

        json response = {
            {"inserted", inserted},
            {"errors", errors},
            {"objectName", vector_index_->getObjectName()},
            {"dimension", vector_index_->getDimension()}
        };
        span.setAttribute("batch.inserted", static_cast<int64_t>(inserted));
        span.setAttribute("batch.errors", static_cast<int64_t>(errors));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);

    } catch (const json::exception& e) {
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::bad_request, std::string("Invalid JSON: ") + e.what(), req);
    } catch (const std::exception& e) {
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleVectorDeleteByFilter(
    const http::request<http::string_body>& req
) {
    if (auth_) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "data:write", "vector.write", path_only)) return *resp;
    }
    auto span = Tracer::startSpan("handleVectorDeleteByFilter");
    span.setAttribute("http.method", "DELETE");
    span.setAttribute("http.path", "/vector/by-filter");

    try {
        if (req.body().empty()) {
            return makeErrorResponse(http::status::bad_request, "Empty body; expected { pks: [...]} or { prefix: '...' }", req);
        }
        auto body = json::parse(req.body());

        size_t deleted = 0;
        if (body.contains("pks") && body["pks"].is_array()) {
            for (const auto& v : body["pks"]) {
                if (!v.is_string()) continue;
                auto st = vector_index_->removeByPk(v.get<std::string>());
                if (st.ok) ++deleted;
            }
            json resp = {{"deleted", deleted}, {"method", "pks"}};
            span.setAttribute("deleted", static_cast<int64_t>(deleted));
            span.setStatus(true);
            return makeResponse(http::status::ok, resp.dump(), req);
        }

        if (body.contains("prefix") && body["prefix"].is_string()) {
            std::string prefix = body["prefix"].get<std::string>();
            // Scan RocksDB for keys starting with objectName:prefix
            std::string fullPrefix = vector_index_->getObjectName() + ":" + prefix;
            storage_->scanPrefix(fullPrefix, [&](std::string_view key, std::string_view /*value*/){
                try {
                    std::string pk = KeySchema::extractPrimaryKey(key);
                    auto st = vector_index_->removeByPk(pk);
                    if (st.ok) ++deleted;
                } catch (...) {}
                return true; // continue
            });
            json resp = {{"deleted", deleted}, {"method", "prefix"}, {"prefix", prefix}};
            span.setAttribute("deleted", static_cast<int64_t>(deleted));
            span.setStatus(true);
            return makeResponse(http::status::ok, resp.dump(), req);
        }

        return makeErrorResponse(http::status::bad_request, "Provide either 'pks' array or 'prefix' string", req);
    } catch (const json::exception& e) {
        span.setStatus(false, e.what());
        return makeErrorResponse(http::status::bad_request, std::string("Invalid JSON: ") + e.what(), req);
    } catch (const std::exception& e) {
        span.setStatus(false, e.what());
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
    auto auth_ctx = extractAuthContext(req);
    std::string user_ctx = auth_ctx.user_id;
    auto status = content_manager_->importContent(body, blob, user_ctx);
        
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
    auto auth_ctx = extractAuthContext(req);
    std::string user_ctx = auth_ctx.user_id;
    auto blob = content_manager_->getContentBlob(id, user_ctx);
        if (!blob) return makeErrorResponse(http::status::not_found, "Blob not found", req);
        auto meta = content_manager_->getContentMeta(id);
        std::string mime = (meta ? meta->mime_type : std::string("application/octet-stream"));

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "THEMIS/0.1.0");
        res.set(http::field::content_type, mime);
        res.keep_alive(req.keep_alive());
        res.body() = *blob; // may contain binary data
    // Apply governance headers also for blob responses
    applyGovernanceHeaders(req, res);
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

http::response<http::string_body> HttpServer::handleFulltextSearch(
    const http::request<http::string_body>& req
) {
    try {
        if (!secondary_index_) return makeErrorResponse(http::status::service_unavailable, "IndexManager not initialized", req);
        
        json body = json::parse(req.body());
        
        // Required fields
        if (!body.contains("table") || !body["table"].is_string()) {
            return makeErrorResponse(http::status::bad_request, "Missing or invalid 'table' field", req);
        }
        if (!body.contains("column") || !body["column"].is_string()) {
            return makeErrorResponse(http::status::bad_request, "Missing or invalid 'column' field", req);
        }
        if (!body.contains("query") || !body["query"].is_string()) {
            return makeErrorResponse(http::status::bad_request, "Missing or invalid 'query' field", req);
        }
        
        std::string table = body["table"];
        std::string column = body["column"];
        std::string query = body["query"];
        size_t limit = body.value("limit", 1000);
        
        // Check if fulltext index exists
        if (!secondary_index_->hasFulltextIndex(table, column)) {
            return makeErrorResponse(http::status::bad_request, 
                "No fulltext index on " + table + "." + column, req);
        }
        
        // Perform BM25-scored fulltext search
        auto [status, results] = secondary_index_->scanFulltextWithScores(table, column, query, limit);
        
        if (!status.ok) {
            return makeErrorResponse(http::status::internal_server_error, status.message, req);
        }
        
        // Build response with scores
        json resp = json::array();
        for (const auto& result : results) {
            resp.push_back({
                {"pk", result.pk},
                {"score", result.score}
            });
        }
        
        json out = {
            {"count", resp.size()},
            {"results", resp},
            {"table", table},
            {"column", column},
            {"query", query}
        };
        
        return makeResponse(http::status::ok, out.dump(), req);
        
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("JSON parse error: ") + e.what(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, std::string("Fulltext search error: ") + e.what(), req);
    } catch (...) {
        return makeErrorResponse(http::status::internal_server_error, "Unknown fulltext search error", req);
    }
}

http::response<http::string_body> HttpServer::handleFusionSearch(
    const http::request<http::string_body>& req
) {
    try {
        if (!secondary_index_) return makeErrorResponse(http::status::service_unavailable, "SecondaryIndexManager not initialized", req);
        if (!vector_index_) return makeErrorResponse(http::status::service_unavailable, "VectorIndexManager not initialized", req);
        
        json body = json::parse(req.body());
        
        // Validate required fields
        if (!body.contains("table") || !body["table"].is_string()) {
            return makeErrorResponse(http::status::bad_request, "Missing or invalid 'table' field", req);
        }
        
        std::string table = body["table"];
        int k = body.value("k", 10);
        std::string fusionMode = body.value("fusion_mode", "rrf"); // "rrf" or "weighted"
        
        // Text search parameters (optional)
        std::vector<SecondaryIndexManager::FulltextResult> textResults;
        bool hasTextQuery = body.contains("text_query") && body.contains("text_column");
        
        if (hasTextQuery) {
            std::string textColumn = body["text_column"];
            std::string textQuery = body["text_query"];
            int textLimit = body.value("text_limit", 1000);
            
            if (!secondary_index_->hasFulltextIndex(table, textColumn)) {
                return makeErrorResponse(http::status::bad_request, 
                    "No fulltext index on " + table + "." + textColumn, req);
            }
            
            auto [textStatus, textRes] = secondary_index_->scanFulltextWithScores(table, textColumn, textQuery, textLimit);
            if (!textStatus.ok) {
                return makeErrorResponse(http::status::internal_server_error, "Text search failed: " + textStatus.message, req);
            }
            textResults = std::move(textRes);
        }
        
        // Vector search parameters (optional)
        std::vector<VectorIndexManager::Result> vectorResults;
        bool hasVectorQuery = body.contains("vector_query");
        
        if (hasVectorQuery) {
            if (!body["vector_query"].is_array()) {
                return makeErrorResponse(http::status::bad_request, "vector_query must be array of floats", req);
            }
            
            std::vector<float> vectorQuery;
            for (const auto& val : body["vector_query"]) {
                if (val.is_number()) {
                    vectorQuery.push_back(val.get<float>());
                }
            }
            
            if (vectorQuery.empty()) {
                return makeErrorResponse(http::status::bad_request, "vector_query array is empty", req);
            }
            
            int vectorLimit = body.value("vector_limit", 1000);
            auto [vecStatus, vecRes] = vector_index_->searchKnn(vectorQuery, vectorLimit);
            if (!vecStatus.ok) {
                return makeErrorResponse(http::status::internal_server_error, "Vector search failed: " + vecStatus.message, req);
            }
            vectorResults = std::move(vecRes);
        }
        
        // Require at least one query type
        if (!hasTextQuery && !hasVectorQuery) {
            return makeErrorResponse(http::status::bad_request, "At least one of text_query or vector_query required", req);
        }
        
        // Fusion logic
        std::vector<std::pair<std::string, double>> fusedResults;
        
        if (fusionMode == "rrf") {
            // Reciprocal Rank Fusion: score = sum(1 / (k + rank))
            int kRrf = body.value("k_rrf", 60);
            std::unordered_map<std::string, double> scores;
            
            // Text contributions
            for (size_t i = 0; i < textResults.size(); ++i) {
                scores[textResults[i].pk] += 1.0 / (kRrf + i + 1);
            }
            
            // Vector contributions
            for (size_t i = 0; i < vectorResults.size(); ++i) {
                scores[vectorResults[i].pk] += 1.0 / (kRrf + i + 1);
            }
            
            // Convert to vector and sort
            fusedResults.reserve(scores.size());
            for (const auto& [pk, score] : scores) {
                fusedResults.emplace_back(pk, score);
            }
            std::sort(fusedResults.begin(), fusedResults.end(), 
                [](const auto& a, const auto& b) { return a.second > b.second; });
            
        } else if (fusionMode == "weighted") {
            // Weighted fusion: alpha * normalize(text_score) + (1 - alpha) * normalize(vector_sim)
            double alpha = body.value("weight_text", 0.5);
            alpha = std::clamp(alpha, 0.0, 1.0);
            
            // Normalize text scores (min-max)
            double textMin = textResults.empty() ? 0.0 : textResults.back().score;
            double textMax = textResults.empty() ? 1.0 : textResults.front().score;
            double textRange = (textMax - textMin) > 1e-9 ? (textMax - textMin) : 1.0;
            
            // Normalize vector distances (convert to similarity: 1 - normalized_dist)
            // Assuming L2 or COSINE metric; smaller distance = better
            double vecMin = vectorResults.empty() ? 0.0 : vectorResults.front().distance;
            double vecMax = vectorResults.empty() ? 1.0 : vectorResults.back().distance;
            double vecRange = (vecMax - vecMin) > 1e-9 ? (vecMax - vecMin) : 1.0;
            
            std::unordered_map<std::string, double> scores;
            
            // Text contributions
            for (const auto& res : textResults) {
                double normScore = (res.score - textMin) / textRange;
                scores[res.pk] += alpha * normScore;
            }
            
            // Vector contributions (convert distance to similarity)
            for (const auto& res : vectorResults) {
                double normDist = (res.distance - vecMin) / vecRange;
                double similarity = 1.0 - normDist;
                scores[res.pk] += (1.0 - alpha) * similarity;
            }
            
            // Convert to vector and sort
            fusedResults.reserve(scores.size());
            for (const auto& [pk, score] : scores) {
                fusedResults.emplace_back(pk, score);
            }
            std::sort(fusedResults.begin(), fusedResults.end(), 
                [](const auto& a, const auto& b) { return a.second > b.second; });
            
        } else {
            return makeErrorResponse(http::status::bad_request, 
                "Invalid fusion_mode: " + fusionMode + " (must be 'rrf' or 'weighted')", req);
        }
        
        // Limit to top-k
        if (fusedResults.size() > static_cast<size_t>(k)) {
            fusedResults.resize(k);
        }
        
        // Build response
        json resp = json::array();
        for (const auto& [pk, score] : fusedResults) {
            resp.push_back({
                {"pk", pk},
                {"score", score}
            });
        }
        
        json out = {
            {"count", resp.size()},
            {"fusion_mode", fusionMode},
            {"table", table},
            {"results", resp}
        };
        
        if (hasTextQuery) {
            out["text_count"] = textResults.size();
        }
        if (hasVectorQuery) {
            out["vector_count"] = vectorResults.size();
        }
        
        return makeResponse(http::status::ok, out.dump(), req);
        
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, std::string("JSON parse error: ") + e.what(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, std::string("Fusion search error: ") + e.what(), req);
    } catch (...) {
        return makeErrorResponse(http::status::internal_server_error, "Unknown fusion search error", req);
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

http::response<http::string_body> HttpServer::handleContentConfigGet(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleContentConfigGet");
    
    try {
        auto v = storage_->get("config:content");
        json resp;
        if (v) {
            std::string s(v->begin(), v->end());
            resp = json::parse(s);
        } else {
            // Return defaults
            resp = {
                {"compress_blobs", false},
                {"compression_level", 19},
                {"skip_compressed_mimes", json::array({"image/", "video/", "application/zip", "application/gzip"})}
            };
        }
        
        span.setStatus(true);
        return makeResponse(http::status::ok, resp.dump(), req);
        
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, 
            std::string("config read error: ") + e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleContentConfigPut(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleContentConfigPut");
    
    try {
        json body = json::parse(req.body());
        
        // Get current config or defaults
        json config;
        auto v = storage_->get("config:content");
        if (v) {
            std::string s(v->begin(), v->end());
            config = json::parse(s);
        } else {
            config = {
                {"compress_blobs", false},
                {"compression_level", 19},
                {"skip_compressed_mimes", json::array({"image/", "video/", "application/zip", "application/gzip"})}
            };
        }
        
        // Update with provided values
        if (body.contains("compress_blobs")) {
            if (!body["compress_blobs"].is_boolean()) {
                span.setStatus(false, "invalid_compress_blobs");
                return makeErrorResponse(http::status::bad_request, 
                    "compress_blobs must be boolean", req);
            }
            config["compress_blobs"] = body["compress_blobs"];
        }
        
        if (body.contains("compression_level")) {
            if (!body["compression_level"].is_number_integer()) {
                span.setStatus(false, "invalid_compression_level");
                return makeErrorResponse(http::status::bad_request, 
                    "compression_level must be an integer", req);
            }
            int level = body["compression_level"];
            if (level < 1 || level > 22) {
                span.setStatus(false, "compression_level_out_of_range");
                return makeErrorResponse(http::status::bad_request, 
                    "compression_level must be between 1 and 22", req);
            }
            config["compression_level"] = level;
        }
        
        if (body.contains("skip_compressed_mimes")) {
            if (!body["skip_compressed_mimes"].is_array()) {
                span.setStatus(false, "invalid_skip_mimes");
                return makeErrorResponse(http::status::bad_request, 
                    "skip_compressed_mimes must be an array of strings", req);
            }
            // Validate all elements are strings
            for (const auto& item : body["skip_compressed_mimes"]) {
                if (!item.is_string()) {
                    span.setStatus(false, "invalid_skip_mimes_element");
                    return makeErrorResponse(http::status::bad_request, 
                        "All elements in skip_compressed_mimes must be strings", req);
                }
            }
            config["skip_compressed_mimes"] = body["skip_compressed_mimes"];
        }
        
        // Store updated config
        std::string config_str = config.dump();
        std::vector<uint8_t> bytes(config_str.begin(), config_str.end());
        bool ok = storage_->put("config:content", bytes);
        
        if (!ok) {
            span.setStatus(false, "storage_error");
            return makeErrorResponse(http::status::internal_server_error, 
                "Failed to store content config", req);
        }
        
        json response = config;
        response["status"] = "ok";
        response["note"] = "Configuration updated. Changes apply to new content imports only.";
        
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const json::exception& e) {
        span.setStatus(false, "json_error");
        return makeErrorResponse(http::status::bad_request, 
            std::string("JSON error: ") + e.what(), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, 
            std::string("config write error: ") + e.what(), req);
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

// ===================== Encryption Schema Management =====================

http::response<http::string_body> HttpServer::handleEncryptionSchemaGet(
    const http::request<http::string_body>& req
) {
    // Require config:read scope
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "config:read", "config.read", path_only)) return *resp;
    }
    
    try {
        auto schema_bytes = storage_->get("config:encryption_schema");
        if (!schema_bytes) {
            // Return empty schema if not configured
            json empty_schema = {
                {"collections", json::object()}
            };
            return makeResponse(http::status::ok, empty_schema.dump(2), req);
        }
        
        std::string schema_json(schema_bytes->begin(), schema_bytes->end());
        // Validate JSON before returning
        try {
            auto parsed = json::parse(schema_json);
            return makeResponse(http::status::ok, parsed.dump(2), req);
        } catch (const json::exception& e) {
            return makeErrorResponse(http::status::internal_server_error, 
                std::string("Stored schema is invalid JSON: ") + e.what(), req);
        }
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleEncryptionSchemaPut(
    const http::request<http::string_body>& req
) {
    // Require config:write scope
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "config:write", "config.write", path_only)) return *resp;
    }
    
    try {
        json body = json::parse(req.body());
        
        // Validate schema structure
        if (!body.contains("collections") || !body["collections"].is_object()) {
            return makeErrorResponse(http::status::bad_request, 
                "Schema must contain 'collections' object", req);
        }
        
        // Validate each collection
        for (auto& [collection_name, collection_config] : body["collections"].items()) {
            if (!collection_config.is_object()) {
                return makeErrorResponse(http::status::bad_request, 
                    "Collection config for '" + collection_name + "' must be an object", req);
            }
            
            if (!collection_config.contains("encryption")) continue;
            
            auto& enc = collection_config["encryption"];
            if (!enc.is_object()) {
                return makeErrorResponse(http::status::bad_request, 
                    "Encryption config for '" + collection_name + "' must be an object", req);
            }
            
            // Validate required fields
            if (!enc.contains("enabled") || !enc["enabled"].is_boolean()) {
                return makeErrorResponse(http::status::bad_request, 
                    "Encryption 'enabled' must be boolean for collection '" + collection_name + "'", req);
            }
            
            if (enc["enabled"].get<bool>()) {
                // If enabled, require fields array
                if (!enc.contains("fields") || !enc["fields"].is_array()) {
                    return makeErrorResponse(http::status::bad_request, 
                        "Encryption 'fields' must be array for collection '" + collection_name + "'", req);
                }
                
                // Validate fields are strings
                for (auto& field : enc["fields"]) {
                    if (!field.is_string()) {
                        return makeErrorResponse(http::status::bad_request, 
                            "All fields must be strings for collection '" + collection_name + "'", req);
                    }
                }
                
                // Validate context_type if present
                if (enc.contains("context_type")) {
                    std::string ctx = enc["context_type"].get<std::string>();
                    if (ctx != "user" && ctx != "group") {
                        return makeErrorResponse(http::status::bad_request, 
                            "context_type must be 'user' or 'group' for collection '" + collection_name + "'", req);
                    }
                    
                    // If group context, allowed_groups is optional but should be array if present
                    if (ctx == "group" && enc.contains("allowed_groups")) {
                        if (!enc["allowed_groups"].is_array()) {
                            return makeErrorResponse(http::status::bad_request, 
                                "allowed_groups must be array for collection '" + collection_name + "'", req);
                        }
                    }
                }
            }
        }
        
        // Store validated schema
        std::string schema_str = body.dump();
        std::vector<uint8_t> bytes(schema_str.begin(), schema_str.end());
        bool ok = storage_->put("config:encryption_schema", bytes);
        
        if (!ok) {
            return makeErrorResponse(http::status::internal_server_error, 
                "Failed to store encryption schema", req);
        }
        
        THEMIS_INFO("Encryption schema updated: {} collections configured", 
            body["collections"].size());
        
        json response = {
            {"status", "ok"},
            {"collections_configured", body["collections"].size()}
        };
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const json::exception& e) {
        return makeErrorResponse(http::status::bad_request, 
            std::string("Invalid JSON: ") + e.what(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

// ===================== Policies: Ranger Import/Export =====================

http::response<http::string_body> HttpServer::handlePoliciesImportRanger(
    const http::request<http::string_body>& req
) {
    // Require admin scope + policy action
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "admin", "admin", path_only)) return *resp;
    }
    if (!ranger_client_) {
        return makeErrorResponse(http::status::service_unavailable, "Ranger client not configured", req);
    }
    try {
        std::string err;
        auto jsonOpt = ranger_client_->fetchPolicies(&err);
        if (!jsonOpt) {
            return makeErrorResponse(http::status::bad_gateway, std::string("Ranger fetch failed: ") + err, req);
        }
        auto internal = themis::server::RangerClient::convertFromRanger(*jsonOpt);
        if (internal.empty()) {
            return makeErrorResponse(http::status::bad_request, "No policies converted from Ranger response", req);
        }
        if (!policy_engine_) policy_engine_ = std::make_unique<themis::PolicyEngine>();
        policy_engine_->setPolicies(internal);
        // Persist to local file
        std::string save_err;
        bool saved = policy_engine_->saveToFile("config/policies.json", &save_err);
        nlohmann::json resp = {
            {"imported", internal.size()},
            {"saved", saved}
        };
        if (!saved) resp["save_error"] = save_err;
        return makeResponse(http::status::ok, resp.dump(), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handlePoliciesExportRanger(
    const http::request<http::string_body>& req
) {
    if (auth_ && auth_->isEnabled()) {
        std::string path_only = std::string(req.target());
        auto qpos = path_only.find('?');
        if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);
        if (auto resp = requireAccess(req, "admin", "admin", path_only)) return *resp;
    }
    try {
        if (!policy_engine_) {
            return makeErrorResponse(http::status::service_unavailable, "Policy engine not initialized", req);
        }
        auto list = policy_engine_->listPolicies();
        std::string service = "themisdb";
        auto out = themis::server::RangerClient::convertToRanger(list, service);
        return makeResponse(http::status::ok, out.dump(2), req);
    } catch (const std::exception& e) {
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
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
            } else if (type == "fulltext") {
                if (!body.contains("column")) {
                    return makeErrorResponse(http::status::bad_request, "Missing 'column' for fulltext index", req);
                }
                std::string column = body["column"].get<std::string>();
                
                // Parse optional config
                SecondaryIndexManager::FulltextConfig config;
                if (body.contains("config") && body["config"].is_object()) {
                    auto configObj = body["config"];
                    config.stemming_enabled = configObj.value("stemming_enabled", false);
                    config.language = configObj.value("language", "none");
                    config.stopwords_enabled = configObj.value("stopwords_enabled", false);
                    if (configObj.contains("stopwords") && configObj["stopwords"].is_array()) {
                        for (const auto& s : configObj["stopwords"]) {
                            if (s.is_string()) config.stopwords.push_back(s.get<std::string>());
                        }
                    }
                    config.normalize_umlauts = configObj.value("normalize_umlauts", false);
                } else {
                    config.stemming_enabled = false;
                    config.language = "none";
                    config.stopwords_enabled = false;
                    config.normalize_umlauts = false;
                }
                
                auto st = secondary_index_->createFulltextIndex(table, column, config);
                if (!st.ok) {
                    return makeErrorResponse(http::status::bad_request, st.message, req);
                }
                
                json resp = {
                    {"success", true}, 
                    {"table", table}, 
                    {"column", column}, 
                    {"type", "fulltext"},
                    {"config", {
                        {"stemming_enabled", config.stemming_enabled},
                        {"language", config.language},
                        {"stopwords_enabled", config.stopwords_enabled},
                        {"stopwords", config.stopwords},
                        {"normalize_umlauts", config.normalize_umlauts}
                    }}
                };
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
    // Inject governance headers consistently
    applyGovernanceHeaders(req, res);
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

void HttpServer::applyGovernanceHeaders(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res
) {
    // Derive governance from request headers and path
    auto to_lower = [](std::string s){ for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c))); return s; };
    std::string path_only = std::string(req.target());
    auto qpos = path_only.find('?');
    if (qpos != std::string::npos) path_only = path_only.substr(0, qpos);

    // Read incoming hints
    std::string classification = ""; // offen | geheim | streng-geheim | vs-nfd
    std::string mode = "observe";    // observe (default) | enforce
    bool encrypt_logs = false;
    for (const auto& h : req) {
        auto name = h.name_string();
        if (beast::iequals(name, "X-Classification")) {
            classification = to_lower(std::string(h.value()));
        } else if (beast::iequals(name, "X-Governance-Mode")) {
            mode = to_lower(std::string(h.value()));
        } else if (beast::iequals(name, "X-Encrypt-Logs")) {
            std::string v = to_lower(std::string(h.value()));
            encrypt_logs = (v == "true" || v == "1" || v == "yes");
        }
    }
    // Resource-based default classification if none provided
    if (classification.empty()) {
        if (path_only.rfind("/admin", 0) == 0) classification = "vs-nfd"; else classification = "offen";
    }
    // Normalize/validate known values
    if (classification != "offen" && classification != "geheim" && classification != "streng-geheim" && classification != "vs-nfd") {
        // Unknown classification -> leave policy header but choose restrictive defaults
        classification = classification; // keep text in summary if provided
    }
    if (mode != "observe" && mode != "enforce") mode = "observe";

    // Derive header values from classification
    std::string ann = (vector_index_ ? std::string("allowed") : std::string("disabled"));
    std::string content_enc = "optional";
    std::string export_perm = "allowed";
    std::string cache_perm = (config_.feature_semantic_cache ? std::string("allowed") : std::string("disabled"));
    std::string retention_days = "365";
    std::string redaction = "none";

    if (classification == "geheim") {
        ann = "disabled";
        cache_perm = "disabled";
        // keep enc optional for geheim per tests (only vs-nfd/streng-geheim require)
    } else if (classification == "streng-geheim") {
        ann = "disabled";
        content_enc = "required";
        export_perm = "forbidden";
        cache_perm = "disabled";
        redaction = "strict";
        retention_days = "1095"; // 3 Jahre, nicht getestet aber plausibel
    } else if (classification == "vs-nfd") {
        content_enc = "required";
        retention_days = "730"; // 2 Jahre
    } else if (classification == "offen") {
        // defaults ok
    }

    // Compose policy summary
    std::string policy_summary = "classification=" + classification + ";mode=" + mode + ";encrypt_logs=" + (encrypt_logs?"true":"false") + ";redaction=" + redaction;

    // Write headers
    res.set("X-Themis-Policy", policy_summary);
    res.set("X-Themis-ANN", ann);
    res.set("X-Themis-Content-Enc", content_enc);
    res.set("X-Themis-Export", export_perm);
    res.set("X-Themis-Cache", cache_perm);
    res.set("X-Themis-Retention-Days", retention_days);
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

// Lazy initialization for PIIPseudonymizer (deferred from constructor to avoid RocksDB deadlock)
void HttpServer::ensurePIIPseudonymizer() {
    std::lock_guard<std::mutex> lock(pii_init_mutex_);
    if (pii_pseudonymizer_) {
        return; // Already initialized
    }
    
    try {
        // Failure injection for tests: set THEMIS_PII_FORCE_INIT_FAIL to
        //   "1"   -> throw exception (simulate hard init failure)
        //   "503" -> return without initializing (caller interprets as service unavailable)
        if (const char* fail_env = std::getenv("THEMIS_PII_FORCE_INIT_FAIL")) {
            std::string val = fail_env;
            if (val == "1") {
                spdlog::error("Forced test failure (throw) due to THEMIS_PII_FORCE_INIT_FAIL=1");
                throw std::runtime_error("Forced test failure (THEMIS_PII_FORCE_INIT_FAIL=1)");
            } else if (val == "503") {
                spdlog::error("Forced service unavailable for PII init (THEMIS_PII_FORCE_INIT_FAIL=503)");
                return; // Leave pii_pseudonymizer_ null
            }
        }
        auto pii_detector = std::make_shared<themis::utils::PIIDetector>();
        pii_pseudonymizer_ = std::make_shared<themis::utils::PIIPseudonymizer>(
            storage_,
            field_encryption_,
            pii_detector,
            audit_logger_
        );
        spdlog::info("PIIPseudonymizer lazy-initialized successfully");
    } catch (const std::exception& ex) {
        spdlog::error("Failed to lazy-initialize PII Pseudonymizer: {}", ex.what());
        throw; // Re-throw to allow caller to handle as service unavailable
    }
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

// ============================================================================
// Sprint B: Time-Series Endpoints
// ============================================================================

http::response<http::string_body> HttpServer::handleTimeSeriesPut(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleTimeSeriesPut");
    
    if (!timeseries_) {
        span.setStatus(false, "feature_disabled");
        return makeErrorResponse(http::status::not_implemented, "Time-series feature not enabled", req);
    }
    
    try {
        json body = json::parse(req.body());
        
        if (!body.contains("metric") || !body.contains("entity") || !body.contains("value")) {
            span.setStatus(false, "invalid_request");
            return makeErrorResponse(http::status::bad_request, 
                "Missing required fields: metric, entity, value", req);
        }
        
        std::string metric = body["metric"];
        std::string entity = body["entity"];
        
        // Build TSStore DataPoint directly from request
        TSStore::DataPoint ts_point;
        ts_point.metric = metric;
        ts_point.entity = entity;
        ts_point.value = body["value"].get<double>();
        ts_point.timestamp_ms = body.value("timestamp_ms", 
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count());
        ts_point.tags = body.value("tags", json::object());
        ts_point.metadata = body.value("metadata", json::object());
        
        auto status = timeseries_->putDataPoint(ts_point);
        
        if (!status.ok) {
            span.setStatus(false, "put_failed");
            return makeErrorResponse(http::status::internal_server_error, 
                status.message.empty() ? "Failed to store data point" : status.message, req);
        }
        
        json response = {
            {"success", true},
            {"metric", metric},
            {"entity", entity},
            {"timestamp_ms", ts_point.timestamp_ms}
        };
        
        span.setStatus(true);
        return makeResponse(http::status::created, response.dump(), req);
        
    } catch (const json::exception& e) {
        span.setStatus(false, "json_error");
        return makeErrorResponse(http::status::bad_request, "JSON error: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleTimeSeriesQuery(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleTimeSeriesQuery");
    
    if (!timeseries_) {
        span.setStatus(false, "feature_disabled");
        return makeErrorResponse(http::status::not_implemented, "Time-series feature not enabled", req);
    }
    
    try {
        json body = json::parse(req.body());
        
        // Only 'metric' is required; 'entity' is optional per tests
        if (!body.contains("metric")) {
            span.setStatus(false, "invalid_request");
            return makeErrorResponse(http::status::bad_request, 
                "Missing required field: metric", req);
        }
        
        std::string metric = body["metric"];
        
        TSStore::QueryOptions query_opts;
        query_opts.metric = metric;
        // Optional entity filter
        if (body.contains("entity") && !body["entity"].is_null()) {
            query_opts.entity = body["entity"].get<std::string>();
        }
        query_opts.from_timestamp_ms = body.value("from_ms", int64_t(0));
        query_opts.to_timestamp_ms = body.value("to_ms", INT64_MAX);
        query_opts.limit = body.value("limit", size_t(1000));
        if (body.contains("tags")) {
            query_opts.tag_filter = body["tags"];
        }
        
        auto [status, points] = timeseries_->query(query_opts);
        
        if (!status.ok) {
            span.setStatus(false, "query_failed");
            return makeErrorResponse(http::status::internal_server_error, 
                status.message.empty() ? "Query failed" : status.message, req);
        }
        
        json response = {
            {"metric", metric},
            {"count", points.size()},
            {"data", json::array()}
        };
        
        for (const auto& p : points) {
            json point_json = {
                {"entity", p.entity},
                {"timestamp_ms", p.timestamp_ms},
                {"value", p.value},
                {"tags", p.tags}
            };
            response["data"].push_back(point_json);
        }
        
        span.setAttribute("points_count", static_cast<int64_t>(points.size()));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const json::exception& e) {
        span.setStatus(false, "json_error");
        return makeErrorResponse(http::status::bad_request, "JSON error: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleTimeSeriesAggregate(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleTimeSeriesAggregate");
    
    if (!timeseries_) {
        span.setStatus(false, "feature_disabled");
        return makeErrorResponse(http::status::not_implemented, "Time-series feature not enabled", req);
    }
    
    try {
        json body = json::parse(req.body());
        
        // Only 'metric' is required; 'entity' optional
        if (!body.contains("metric")) {
            span.setStatus(false, "invalid_request");
            return makeErrorResponse(http::status::bad_request, 
                "Missing required field: metric", req);
        }
        
        std::string metric = body["metric"];
        
        TSStore::QueryOptions query_opts;
        query_opts.metric = metric;
        if (body.contains("entity") && !body["entity"].is_null()) {
            query_opts.entity = body["entity"].get<std::string>();
        }
        query_opts.from_timestamp_ms = body.value("from_ms", int64_t(0));
        query_opts.to_timestamp_ms = body.value("to_ms", INT64_MAX);
        query_opts.limit = body.value("limit", size_t(1000000)); // No limit for aggregation
        if (body.contains("tags")) {
            query_opts.tag_filter = body["tags"];
        }
        
        auto [status, agg] = timeseries_->aggregate(query_opts);
        
        if (!status.ok) {
            span.setStatus(false, "aggregate_failed");
            return makeErrorResponse(http::status::internal_server_error, 
                status.message.empty() ? "Aggregation failed" : status.message, req);
        }
        
        json response = {
            {"metric", metric},
            {"aggregation", {
                {"min", agg.min},
                {"max", agg.max},
                {"avg", agg.avg},
                {"sum", agg.sum},
                {"count", agg.count},
                {"first_timestamp_ms", agg.first_timestamp_ms},
                {"last_timestamp_ms", agg.last_timestamp_ms}
            }}
        };
        
        span.setAttribute("agg_count", static_cast<int64_t>(agg.count));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const json::exception& e) {
        span.setStatus(false, "json_error");
        return makeErrorResponse(http::status::bad_request, "JSON error: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleTimeSeriesAggregatesGet(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleTimeSeriesAggregatesGet");
    try {
        // Minimal placeholder: list supported aggregate functions
        json response = {
            {"aggregates", json::array({"min","max","avg","sum","count"})}
        };
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleTimeSeriesRetentionGet(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleTimeSeriesRetentionGet");
    try {
        // Minimal placeholder: empty list of retention policies
        json response = {
            {"policies", json::array()}
        };
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleTimeSeriesConfigGet(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleTimeSeriesConfigGet");
    
    if (!timeseries_) {
        span.setStatus(false, "feature_disabled");
        return makeErrorResponse(http::status::not_implemented, "Time-series feature not enabled", req);
    }
    
    try {
        // Prefer persisted config if present so settings survive restarts
        auto stored = storage_->get("config:timeseries");
        json response;
        if (stored) {
            std::string s(stored->begin(), stored->end());
            response = json::parse(s);
        } else {
            const auto& config = timeseries_->getConfig();
            response = {
                {"compression", config.compression == TSStore::CompressionType::Gorilla ? "gorilla" : "none"},
                {"chunk_size_hours", config.chunk_size_hours}
            };
        }

        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleTimeSeriesConfigPut(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleTimeSeriesConfigPut");
    
    if (!timeseries_) {
        span.setStatus(false, "feature_disabled");
        return makeErrorResponse(http::status::not_implemented, "Time-series feature not enabled", req);
    }
    
    try {
        json body = json::parse(req.body());
        // Load current persisted config if present, otherwise use in-memory defaults
        json persisted;
        auto v = storage_->get("config:timeseries");
        if (v) {
            std::string s(v->begin(), v->end());
            persisted = json::parse(s);
        } else {
            const auto& cur = timeseries_->getConfig();
            persisted = {
                {"compression", cur.compression == TSStore::CompressionType::Gorilla ? "gorilla" : "none"},
                {"chunk_size_hours", cur.chunk_size_hours}
            };
        }

        // Apply updates from request body into persisted JSON
        if (body.contains("compression")) {
            if (!body["compression"].is_string()) {
                span.setStatus(false, "invalid_compression_type");
                return makeErrorResponse(http::status::bad_request, "compression must be a string", req);
            }
            std::string compression_str = body["compression"];
            if (compression_str != "gorilla" && compression_str != "none") {
                span.setStatus(false, "invalid_compression");
                return makeErrorResponse(http::status::bad_request, 
                    "Invalid compression type. Must be 'gorilla' or 'none'", req);
            }
            persisted["compression"] = compression_str;
        }

        if (body.contains("chunk_size_hours")) {
            if (!body["chunk_size_hours"].is_number_integer()) {
                span.setStatus(false, "invalid_chunk_size_type");
                return makeErrorResponse(http::status::bad_request, "chunk_size_hours must be an integer", req);
            }
            int chunk_size = body["chunk_size_hours"];
            if (chunk_size <= 0 || chunk_size > 168) { // Max 1 week
                span.setStatus(false, "invalid_chunk_size");
                return makeErrorResponse(http::status::bad_request, 
                    "chunk_size_hours must be between 1 and 168 (1 week)", req);
            }
            persisted["chunk_size_hours"] = chunk_size;
        }

        // Persist to storage
        std::string config_str = persisted.dump();
        std::vector<uint8_t> bytes(config_str.begin(), config_str.end());
        bool ok = storage_->put("config:timeseries", bytes);
        if (!ok) {
            span.setStatus(false, "storage_error");
            return makeErrorResponse(http::status::internal_server_error, "Failed to store timeseries config", req);
        }

        // Apply to in-memory TSStore (affects new data points only)
        TSStore::Config new_config = timeseries_->getConfig();
        if (persisted.contains("compression")) {
            std::string compression_str = persisted["compression"];
            new_config.compression = (compression_str == "gorilla") ? TSStore::CompressionType::Gorilla : TSStore::CompressionType::None;
        }
        if (persisted.contains("chunk_size_hours")) {
            new_config.chunk_size_hours = persisted["chunk_size_hours"];
        }
        timeseries_->setConfig(new_config);

        json response = persisted;
        response["status"] = "ok";
        response["note"] = "Configuration updated. Changes apply to new data points only.";

        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const json::exception& e) {
        span.setStatus(false, "json_error");
        return makeErrorResponse(http::status::bad_request, "JSON error: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

// ============================================================================
// Sprint C: Adaptive Indexing Endpoints
// ============================================================================

http::response<http::string_body> HttpServer::handleIndexSuggestions(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleIndexSuggestions");
    
    try {
        auto target = std::string(req.target());
        
        // Parse query parameters
        std::string collection;
        double min_score = 0.5;
        size_t limit = 10;
        
        // Extract query params from URL
        auto query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            std::string query_string = target.substr(query_pos + 1);
            std::istringstream iss(query_string);
            std::string param;
            
            while (std::getline(iss, param, '&')) {
                auto eq_pos = param.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = param.substr(0, eq_pos);
                    std::string value = param.substr(eq_pos + 1);
                    
                    if (key == "collection") {
                        collection = value;
                    } else if (key == "min_score") {
                        min_score = std::stod(value);
                    } else if (key == "limit") {
                        limit = std::stoull(value);
                    }
                }
            }
        }
        
        span.setAttribute("collection", collection);
        span.setAttribute("min_score", min_score);
        span.setAttribute("limit", static_cast<int64_t>(limit));
        
        auto suggestions = adaptive_index_->getSuggestions(collection, min_score, limit);
        
        json response = json::array();
        for (const auto& suggestion : suggestions) {
            response.push_back(suggestion.toJson());
        }
        
        span.setAttribute("suggestions.count", static_cast<int64_t>(suggestions.size()));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleIndexPatterns(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleIndexPatterns");
    
    try {
        auto target = std::string(req.target());
        
        // Parse collection from query params
        std::string collection;
        auto query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            std::string query_string = target.substr(query_pos + 1);
            auto coll_pos = query_string.find("collection=");
            if (coll_pos != std::string::npos) {
                collection = query_string.substr(coll_pos + 11);
                auto amp_pos = collection.find('&');
                if (amp_pos != std::string::npos) {
                    collection = collection.substr(0, amp_pos);
                }
            }
        }
        
        span.setAttribute("collection", collection);
        
        auto patterns = adaptive_index_->getPatterns(collection);
        
        json response = json::array();
        for (const auto& pattern : patterns) {
            response.push_back(pattern.toJson());
        }
        
        span.setAttribute("patterns.count", static_cast<int64_t>(patterns.size()));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleIndexRecordPattern(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleIndexRecordPattern");
    
    try {
        json body = json::parse(req.body());
        
        std::string collection = body.value("collection", "");
        std::string field = body.value("field", "");
        std::string operation = body.value("operation", "eq");
        int64_t execution_time_ms = body.value("execution_time_ms", int64_t(0));
        
        // Validate required fields
        if (collection.empty()) {
            return makeErrorResponse(http::status::bad_request, "collection is required", req);
        }
        if (field.empty()) {
            return makeErrorResponse(http::status::bad_request, "field is required", req);
        }
        
        if (collection.empty() || field.empty()) {
            span.setStatus(false, "missing_fields");
            return makeErrorResponse(http::status::bad_request, 
                "Missing required fields: collection, field", req);
        }
        
        span.setAttribute("collection", collection);
        span.setAttribute("field", field);
        span.setAttribute("operation", operation);
        
        adaptive_index_->getPatternTracker()->recordPattern(
            collection, field, operation, execution_time_ms
        );
        
        json response = {
            {"status", "recorded"},
            {"collection", collection},
            {"field", field},
            {"operation", operation}
        };
        
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const json::exception& e) {
        span.setStatus(false, "json_error");
        return makeErrorResponse(http::status::bad_request, "JSON error: " + std::string(e.what()), req);
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

http::response<http::string_body> HttpServer::handleIndexClearPatterns(
    const http::request<http::string_body>& req
) {
    auto span = Tracer::startSpan("handleIndexClearPatterns");
    
    try {
        size_t count_before = adaptive_index_->getPatternTracker()->size();
        
        adaptive_index_->getPatternTracker()->clear();
        
        json response = {
            {"status", "cleared"},
            {"patterns_removed", count_before}
        };
        
        span.setAttribute("patterns.removed", static_cast<int64_t>(count_before));
        span.setStatus(true);
        return makeResponse(http::status::ok, response.dump(), req);
        
    } catch (const std::exception& e) {
        span.setStatus(false, "error");
        return makeErrorResponse(http::status::internal_server_error, e.what(), req);
    }
}

} // namespace server
} // namespace themis
