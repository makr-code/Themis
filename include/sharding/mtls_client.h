#pragma once

#include <string>
#include <optional>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>

// Forward declarations to avoid pulling in Boost headers
namespace boost {
namespace asio {
    class io_context;
    namespace ssl {
        class context;
    }
}
namespace beast {
    namespace http {
        enum class verb;
    }
}
}

namespace themis::sharding {

/**
 * mTLS Client for Secure Shard-to-Shard Communication
 * 
 * Implements mutual TLS (mTLS) for authenticated and encrypted
 * communication between shards. Both client and server present
 * certificates during the TLS handshake.
 * 
 * Features:
 * - Mutual TLS authentication
 * - Certificate verification against Root CA
 * - CRL (Certificate Revocation List) checking
 * - Connection pooling and reuse
 * - Automatic retry with exponential backoff
 */
class MTLSClient {
public:
    /**
     * Configuration for mTLS Client
     */
    struct Config {
        std::string cert_path;          // Path to client certificate (PEM)
        std::string key_path;           // Path to private key (PEM)
        std::string key_passphrase;     // Optional: key passphrase
        std::string ca_cert_path;       // Path to Root CA certificate (PEM)
        std::string crl_path;           // Optional: Path to CRL file (PEM)
        
        // TLS configuration
        std::string tls_version = "TLSv1.3"; // TLS version (TLSv1.2, TLSv1.3)
        bool verify_peer = true;        // Verify peer certificate
        bool verify_hostname = true;    // Verify hostname against certificate
        
        // Connection settings
        uint32_t connect_timeout_ms = 5000;   // Connection timeout
        uint32_t request_timeout_ms = 30000;  // Request timeout
        uint32_t max_retries = 3;             // Maximum retry attempts
        uint32_t retry_delay_ms = 1000;       // Initial retry delay (exponential backoff)
        
        // Connection pooling
        bool enable_pooling = true;     // Enable connection pooling
        uint32_t max_connections = 10;  // Max connections per endpoint
        uint32_t idle_timeout_ms = 60000; // Idle connection timeout
    };
    
    /**
     * HTTP request result
     */
    struct Response {
        int status_code;                // HTTP status code (200, 404, 500, etc.)
        std::string status_message;     // HTTP status message
        nlohmann::json body;            // Response body (JSON)
        std::string raw_body;           // Raw response body
        bool success;                   // true if status 2xx
        std::string error;              // Error message if failed
    };
    
    /**
     * Construct mTLS client with configuration
     * @param config Client configuration
     */
    explicit MTLSClient(const Config& config);
    
    /**
     * Destructor - cleanup SSL context and connections
     */
    ~MTLSClient();
    
    /**
     * Perform HTTP GET request with mTLS
     * @param endpoint Server endpoint (e.g., "https://shard-001.dc1:8080")
     * @param path Request path (e.g., "/api/v1/status")
     * @return Response with JSON body, or error
     */
    Response get(const std::string& endpoint, const std::string& path);
    
    /**
     * Perform HTTP POST request with mTLS
     * @param endpoint Server endpoint
     * @param path Request path
     * @param body Request body (JSON)
     * @return Response with JSON body, or error
     */
    Response post(const std::string& endpoint, 
                  const std::string& path,
                  const nlohmann::json& body);
    
    /**
     * Perform HTTP PUT request with mTLS
     * @param endpoint Server endpoint
     * @param path Request path
     * @param body Request body (JSON)
     * @return Response with JSON body, or error
     */
    Response put(const std::string& endpoint,
                 const std::string& path,
                 const nlohmann::json& body);
    
    /**
     * Perform HTTP DELETE request with mTLS
     * @param endpoint Server endpoint
     * @param path Request path
     * @return Response with JSON body, or error
     */
    Response del(const std::string& endpoint, const std::string& path);
    
    /**
     * Check if client is configured and ready
     * @return true if client is ready to make requests
     */
    bool isReady() const;
    
    /**
     * Get current configuration
     * @return Client configuration
     */
    const Config& getConfig() const { return config_; }
    
    /**
     * Close all connections and reset connection pool
     */
    void reset();

private:
    Config config_;
    
    // Boost.Asio and SSL context (PIMPL to hide Boost headers)
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    /**
     * Perform HTTP request with retry logic
     */
    Response request(const std::string& method,
                    const std::string& endpoint,
                    const std::string& path,
                    const std::optional<nlohmann::json>& body = std::nullopt);
    
    /**
     * Initialize SSL context with certificates
     */
    bool initSSLContext();
    
    /**
     * Verify peer certificate (called during TLS handshake)
     */
    bool verifyPeerCertificate(bool preverified, void* ctx);
    
    /**
     * Parse endpoint into host and port
     */
    static std::pair<std::string, std::string> parseEndpoint(const std::string& endpoint);
};

} // namespace themis::sharding
