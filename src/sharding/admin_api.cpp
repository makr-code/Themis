#include "sharding/admin_api.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace themis {
namespace sharding {

AdminAPI::AdminAPI(const Config& config)
    : config_(config) {
}

void AdminAPI::registerTopologyHandler(RequestHandler handler) {
    topology_handler_ = handler;
}

void AdminAPI::registerRebalanceHandler(RequestHandler handler) {
    rebalance_handler_ = handler;
}

void AdminAPI::registerHealthHandler(RequestHandler handler) {
    health_handler_ = handler;
}

void AdminAPI::registerStatsHandler(RequestHandler handler) {
    stats_handler_ = handler;
}

nlohmann::json AdminAPI::handleRequest(const std::string& method, 
                                         const std::string& path,
                                         const nlohmann::json& body,
                                         const std::string& operator_cert) {
    // Authorize request
    if (!authorizeRequest(operator_cert)) {
        return createErrorResponse(403, "Unauthorized - invalid operator certificate");
    }

    // Audit log
    auditLog(method, path, operator_cert);

    // Route to appropriate handler
    if (path == Endpoints::TOPOLOGY && method == "GET") {
        if (topology_handler_) {
            return topology_handler_(body);
        }
    } else if (path == Endpoints::SHARD_ADD && method == "POST") {
        if (topology_handler_) {
            return topology_handler_(body);
        }
    } else if (path.find(Endpoints::SHARD_REMOVE) == 0 && method == "DELETE") {
        if (topology_handler_) {
            return topology_handler_(body);
        }
    } else if (path == Endpoints::REBALANCE && method == "POST") {
        if (rebalance_handler_) {
            return rebalance_handler_(body);
        }
    } else if (path.find(Endpoints::REBALANCE_STATUS) == 0 && method == "GET") {
        if (rebalance_handler_) {
            return rebalance_handler_(body);
        }
    } else if (path == Endpoints::HEALTH && method == "GET") {
        if (health_handler_) {
            return health_handler_(body);
        }
    } else if (path == Endpoints::STATS && method == "GET") {
        if (stats_handler_) {
            return stats_handler_(body);
        }
    }

    return createErrorResponse(404, "Endpoint not found");
}

bool AdminAPI::authorizeRequest(const std::string& operator_cert) {
    // Placeholder - would validate operator certificate
    // Check certificate has "admin" capability
    // Verify signature if required
    return !operator_cert.empty();
}

void AdminAPI::auditLog(const std::string& method, const std::string& path, const std::string& operator_cert) {
    if (!config_.enable_audit_log) return;

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ofstream log_file(config_.audit_log_path, std::ios::app);
    if (log_file.is_open()) {
        log_file << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                 << " | " << method 
                 << " | " << path
                 << " | " << operator_cert.substr(0, 20) << "..."
                 << std::endl;
    }
}

nlohmann::json AdminAPI::createErrorResponse(int code, const std::string& message) {
    return {
        {"success", false},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
}

} // namespace sharding
} // namespace themis
