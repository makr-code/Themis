#pragma once

// Windows compatibility
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <chrono>

#include "content/content_manager.h"
#include "content/content_processor.h"

namespace themis {
// Forward declarations
class RocksDBWrapper;
class SecondaryIndexManager;
class GraphIndexManager;
class VectorIndexManager;
class TransactionManager;

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

    // Request routing
    void setupRoutes();
    http::response<http::string_body> routeRequest(const http::request<http::string_body>& req);

    // Endpoint handlers
    http::response<http::string_body> handleHealthCheck(const http::request<http::string_body>& req);
    http::response<http::string_body> handleMetrics(const http::request<http::string_body>& req);
    http::response<http::string_body> handleStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleConfig(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetEntity(const http::request<http::string_body>& req);
    http::response<http::string_body> handlePutEntity(const http::request<http::string_body>& req);
    http::response<http::string_body> handleDeleteEntity(const http::request<http::string_body>& req);
    http::response<http::string_body> handleQuery(const http::request<http::string_body>& req);
    http::response<http::string_body> handleQueryAql(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGraphTraverse(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorSearch(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexSave(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexLoad(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexConfigGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexConfigPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleVectorIndexStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransaction(const http::request<http::string_body>& req);
    http::response<http::string_body> handleCreateIndex(const http::request<http::string_body>& req);
    http::response<http::string_body> handleDropIndex(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexStats(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexRebuild(const http::request<http::string_body>& req);
    http::response<http::string_body> handleIndexReindex(const http::request<http::string_body>& req);

    // Content API endpoints
    http::response<http::string_body> handleContentImport(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetContent(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetContentBlob(const http::request<http::string_body>& req);
    http::response<http::string_body> handleGetContentChunks(const http::request<http::string_body>& req);
    http::response<http::string_body> handleHybridSearch(const http::request<http::string_body>& req);
    http::response<http::string_body> handleContentFilterSchemaGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleContentFilterSchemaPut(const http::request<http::string_body>& req);
    http::response<http::string_body> handleEdgeWeightConfigGet(const http::request<http::string_body>& req);
    http::response<http::string_body> handleEdgeWeightConfigPut(const http::request<http::string_body>& req);
    
    // Transaction endpoints
    http::response<http::string_body> handleTransactionBegin(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransactionCommit(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransactionRollback(const http::request<http::string_body>& req);
    http::response<http::string_body> handleTransactionStats(const http::request<http::string_body>& req);

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

    std::string extractPathParam(const std::string& path, const std::string& prefix);

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

    // Content Manager
    std::unique_ptr<themis::content::ContentManager> content_manager_;
    // Built-in processors
    std::unique_ptr<themis::content::TextProcessor> text_processor_;

    // Networking
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    
    // Thread pool
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
    
    // Metrics
    std::atomic<uint64_t> request_count_{0};
    std::atomic<uint64_t> error_count_{0};
    std::chrono::steady_clock::time_point start_time_;
    
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
};

} // namespace server
} // namespace themis
