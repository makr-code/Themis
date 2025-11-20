#include "sharding/remote_executor.h"
#include <chrono>

namespace themis::sharding {

RemoteExecutor::RemoteExecutor(const Config& config)
    : config_(config) {
    
    // Initialize mTLS client
    MTLSClient::Config mtls_config{
        .cert_path = config.cert_path,
        .key_path = config.key_path,
        .key_passphrase = config.key_passphrase,
        .ca_cert_path = config.ca_cert_path,
        .crl_path = config.crl_path,
        .tls_version = "TLSv1.3",
        .verify_peer = true,
        .verify_hostname = true,
        .connect_timeout_ms = config.connect_timeout_ms,
        .request_timeout_ms = config.request_timeout_ms,
        .max_retries = config.max_retries
    };
    
    mtls_client_ = std::make_unique<MTLSClient>(mtls_config);
    
    // Initialize request signer if signing is enabled
    if (config.enable_signing) {
        SignedRequestSigner::Config signer_config{
            .shard_id = config.local_shard_id,
            .cert_path = config.cert_path,
            .key_path = config.key_path,
            .key_passphrase = config.key_passphrase
        };
        
        request_signer_ = std::make_unique<SignedRequestSigner>(signer_config);
    }
}

RemoteExecutor::Result RemoteExecutor::get(const ShardInfo& shard_info,
                                          const std::string& path) {
    return executeRequest("GET", shard_info, path);
}

RemoteExecutor::Result RemoteExecutor::post(const ShardInfo& shard_info,
                                           const std::string& path,
                                           const nlohmann::json& body) {
    return executeRequest("POST", shard_info, path, std::optional<nlohmann::json>(body));
}

RemoteExecutor::Result RemoteExecutor::put(const ShardInfo& shard_info,
                                          const std::string& path,
                                          const nlohmann::json& body) {
    return executeRequest("PUT", shard_info, path, std::optional<nlohmann::json>(body));
}

RemoteExecutor::Result RemoteExecutor::del(const ShardInfo& shard_info,
                                          const std::string& path) {
    return executeRequest("DELETE", shard_info, path);
}

RemoteExecutor::Result RemoteExecutor::executeQuery(const ShardInfo& shard_info,
                                                    const std::string& query) {
    // Execute query via POST to /api/v1/query endpoint
    nlohmann::json body = {
        {"query", query}
    };
    
    return post(shard_info, "/api/v1/query", body);
}

bool RemoteExecutor::isReady() const {
    return mtls_client_ && mtls_client_->isReady();
}

std::string RemoteExecutor::getEndpointURL(const ShardInfo& shard_info) const {
    // Construct endpoint URL
    // If primary_endpoint already contains protocol, use it as-is
    if (shard_info.primary_endpoint.find("://") != std::string::npos) {
        return shard_info.primary_endpoint;
    }
    
    // Otherwise, prepend https://
    return "https://" + shard_info.primary_endpoint;
}

RemoteExecutor::Result RemoteExecutor::executeRequest(
    const std::string& method,
    const ShardInfo& shard_info,
    const std::string& path,
    const std::optional<nlohmann::json>& body) {
    
    auto start = std::chrono::steady_clock::now();
    
    // Get endpoint URL
    std::string endpoint = getEndpointURL(shard_info);
    
    // Prepare request body (with signing if enabled)
    nlohmann::json request_body;
    if (body) {
        request_body = *body;
    }
    
    // Add signed request envelope if signing is enabled
    if (config_.enable_signing && request_signer_) {
        SignedRequest signed_req = request_signer_->createSignedRequest(
            method, path, request_body
        );
        
        // Wrap in signed envelope
        request_body = signed_req.toJSON();
    }
    
    // Execute request via mTLS client
    MTLSClient::Response response;
    
    if (method == "GET") {
        response = mtls_client_->get(endpoint, path);
    } else if (method == "POST") {
        response = mtls_client_->post(endpoint, path, request_body);
    } else if (method == "PUT") {
        response = mtls_client_->put(endpoint, path, request_body);
    } else if (method == "DELETE") {
        response = mtls_client_->del(endpoint, path);
    }
    
    auto end = std::chrono::steady_clock::now();
    uint64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start
    ).count();
    
    return convertResponse(response, shard_info.shard_id, elapsed_ms);
}

RemoteExecutor::Result RemoteExecutor::convertResponse(
    const MTLSClient::Response& response,
    const std::string& shard_id,
    uint64_t elapsed_ms) {
    
    Result result;
    result.shard_id = shard_id;
    result.success = response.success;
    result.error = response.error;
    result.execution_time_ms = elapsed_ms;
    result.http_status = response.status_code;
    
    if (response.success) {
        result.data = response.body;
    } else {
        // Include error details in data
        result.data = nlohmann::json{
            {"error", response.error},
            {"status_code", response.status_code},
            {"status_message", response.status_message}
        };
    }
    
    return result;
}

} // namespace themis::sharding
