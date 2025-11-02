#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace themis {
class PolicyEngine; // forward
}

namespace themis { namespace server {

struct RangerClientConfig {
    std::string base_url;           // e.g. https://ranger.example.com
    std::string policies_path;      // e.g. /service/public/v2/api/policy
    std::string service_name;       // e.g. themisdb-prod
    std::string bearer_token;       // Authorization: Bearer <token>
    bool tls_verify = true;         // verify peer
    std::optional<std::string> ca_cert_path;       // optional custom CA
    std::optional<std::string> client_cert_path;   // optional mTLS
    std::optional<std::string> client_key_path;    // optional mTLS
    // Timeouts (milliseconds)
    long connect_timeout_ms = 5000; // default 5s connect timeout
    long request_timeout_ms = 15000; // default 15s total timeout
    // Retry policy
    int max_retries = 2;            // number of retries on transient errors (in addition to first try)
    long retry_backoff_ms = 500;    // initial backoff between retries, exponential
};

class RangerClient {
public:
    explicit RangerClient(RangerClientConfig cfg);

    // Fetch policies for configured service from Ranger REST API
    // Returns parsed JSON array/object on success.
    std::optional<nlohmann::json> fetchPolicies(std::string* err = nullptr) const;

    // Convert Ranger policies JSON to internal PolicyEngine::Policy vector
    static std::vector<themis::PolicyEngine::Policy> convertFromRanger(const nlohmann::json& rangerJson);

    // Convert internal policies to a minimal Ranger-like JSON
    static nlohmann::json convertToRanger(const std::vector<themis::PolicyEngine::Policy>& policies,
                                          const std::string& service_name);

private:
    RangerClientConfig cfg_;
};

}} // namespace themis::server
