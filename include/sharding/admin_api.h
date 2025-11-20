#ifndef THEMIS_SHARDING_ADMIN_API_H
#define THEMIS_SHARDING_ADMIN_API_H

#include <string>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace themis {
namespace sharding {

/**
 * Admin API for cluster management operations.
 * 
 * Provides RESTful HTTP endpoints for:
 * - Topology management (add/remove shards)
 * - Rebalancing operations (trigger/monitor)
 * - Health monitoring
 * - Routing statistics
 * 
 * All endpoints require operator certificate for authorization.
 */
class AdminAPI {
public:
    struct Config {
        int http_port = 8080;
        std::string operator_cert_path;
        std::string ca_cert_path;
        bool require_signatures = true;
        bool enable_audit_log = true;
        std::string audit_log_path = "/var/log/themis/admin_audit.log";
    };

    using RequestHandler = std::function<nlohmann::json(const nlohmann::json&)>;

    explicit AdminAPI(const Config& config);
    ~AdminAPI() = default;

    // Register handlers
    void registerTopologyHandler(RequestHandler handler);
    void registerRebalanceHandler(RequestHandler handler);
    void registerHealthHandler(RequestHandler handler);
    void registerStatsHandler(RequestHandler handler);

    // Handle HTTP request
    nlohmann::json handleRequest(const std::string& method, 
                                  const std::string& path,
                                  const nlohmann::json& body,
                                  const std::string& operator_cert);

    // Endpoints
    struct Endpoints {
        static constexpr const char* TOPOLOGY = "/admin/topology";
        static constexpr const char* SHARD_ADD = "/admin/shard/add";
        static constexpr const char* SHARD_REMOVE = "/admin/shard/";  // + {id}
        static constexpr const char* REBALANCE = "/admin/rebalance";
        static constexpr const char* REBALANCE_STATUS = "/admin/rebalance/";  // + {id}
        static constexpr const char* HEALTH = "/admin/health";
        static constexpr const char* STATS = "/admin/stats";
        static constexpr const char* CERTS = "/admin/certs";
    };

private:
    Config config_;
    RequestHandler topology_handler_;
    RequestHandler rebalance_handler_;
    RequestHandler health_handler_;
    RequestHandler stats_handler_;

    bool authorizeRequest(const std::string& operator_cert);
    void auditLog(const std::string& method, const std::string& path, const std::string& operator_cert);
    nlohmann::json createErrorResponse(int code, const std::string& message);
};

} // namespace sharding
} // namespace themis

#endif // THEMIS_SHARDING_ADMIN_API_H
