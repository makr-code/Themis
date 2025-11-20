#pragma once

#include "sharding/mtls_client.h"
#include "sharding/signed_request.h"
#include "sharding/shard_topology.h"
#include <string>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>

namespace themis::sharding {

/**
 * Remote Executor - Execute requests on remote shards
 * 
 * Handles shard-to-shard communication using mTLS and signed requests.
 * Provides high-level interface for executing operations on remote shards
 * with automatic retry, error handling, and connection management.
 */
class RemoteExecutor {
public:
    /**
     * Configuration for Remote Executor
     */
    struct Config {
        // mTLS configuration
        std::string cert_path;          // This shard's certificate
        std::string key_path;           // This shard's private key
        std::string key_passphrase;     // Optional: key passphrase
        std::string ca_cert_path;       // Root CA certificate
        std::string crl_path;           // Optional: CRL file
        
        // This shard's identity
        std::string local_shard_id;     // This shard's ID
        
        // Request signing
        bool enable_signing = true;     // Enable signed requests
        
        // Timeouts and retries (inherited by mTLS client)
        uint32_t connect_timeout_ms = 5000;
        uint32_t request_timeout_ms = 30000;
        uint32_t max_retries = 3;
    };
    
    /**
     * Result from remote execution
     */
    struct Result {
        std::string shard_id;           // Target shard ID
        nlohmann::json data;            // Response data
        bool success;                   // true if request succeeded
        std::string error;              // Error message if failed
        uint64_t execution_time_ms;     // Execution time
        int http_status;                // HTTP status code
    };
    
    /**
     * Construct Remote Executor with configuration
     */
    explicit RemoteExecutor(const Config& config);
    
    /**
     * Execute GET request on remote shard
     * @param shard_info Target shard information
     * @param path Request path (e.g., "/api/v1/data/urn:themis:...")
     * @return Result with response data or error
     */
    Result get(const ShardInfo& shard_info, const std::string& path);
    
    /**
     * Execute POST request on remote shard
     * @param shard_info Target shard information
     * @param path Request path
     * @param body Request body (JSON)
     * @return Result with response data or error
     */
    Result post(const ShardInfo& shard_info,
                const std::string& path,
                const nlohmann::json& body);
    
    /**
     * Execute PUT request on remote shard
     * @param shard_info Target shard information
     * @param path Request path
     * @param body Request body (JSON)
     * @return Result with response data or error
     */
    Result put(const ShardInfo& shard_info,
               const std::string& path,
               const nlohmann::json& body);
    
    /**
     * Execute DELETE request on remote shard
     * @param shard_info Target shard information
     * @param path Request path
     * @return Result with response data or error
     */
    Result del(const ShardInfo& shard_info, const std::string& path);
    
    /**
     * Execute query on remote shard
     * Convenience method for query execution
     * @param shard_info Target shard information
     * @param query Query string (e.g., AQL query)
     * @return Result with query results
     */
    Result executeQuery(const ShardInfo& shard_info, const std::string& query);
    
    /**
     * Check if remote executor is ready
     * @return true if configured and ready to execute requests
     */
    bool isReady() const;
    
    /**
     * Get local shard ID
     * @return This shard's ID
     */
    const std::string& getLocalShardId() const {
        return config_.local_shard_id;
    }

private:
    Config config_;
    std::unique_ptr<MTLSClient> mtls_client_;
    std::unique_ptr<SignedRequestSigner> request_signer_;
    
    /**
     * Convert ShardInfo to endpoint URL
     * @param shard_info Shard information
     * @return Endpoint URL (e.g., "https://shard-001.dc1:8080")
     */
    std::string getEndpointURL(const ShardInfo& shard_info) const;
    
    /**
     * Execute request with optional signing
     * @param method HTTP method
     * @param shard_info Target shard
     * @param path Request path
     * @param body Optional request body
     * @return Result
     */
    Result executeRequest(const std::string& method,
                         const ShardInfo& shard_info,
                         const std::string& path,
                         const std::optional<nlohmann::json>& body = std::nullopt);
    
    /**
     * Convert MTLSClient::Response to RemoteExecutor::Result
     */
    Result convertResponse(const MTLSClient::Response& response,
                          const std::string& shard_id,
                          uint64_t start_time_ms);
};

} // namespace themis::sharding
