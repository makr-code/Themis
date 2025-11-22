#include "server/hot_reload_api_handler.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>

namespace themis {
namespace server {

using json = nlohmann::json;

HotReloadApiHandler::HotReloadApiHandler(
    std::shared_ptr<updates::ManifestDatabase> manifest_db,
    std::shared_ptr<updates::HotReloadEngine> reload_engine
)
    : manifest_db_(std::move(manifest_db))
    , reload_engine_(std::move(reload_engine)) {
}

http::response<http::string_body> HotReloadApiHandler::handleRequest(
    const http::request<http::string_body>& req
) {
    std::string target = std::string(req.target());
    auto method = req.method();
    
    // Route to appropriate handler
    if (target.find("/api/updates/manifests/") == 0 && method == http::verb::get) {
        std::string version = extractPathParam(target, "/api/updates/manifests/");
        return handleGetManifest(req, version);
    } else if (target.find("/api/updates/download/") == 0 && method == http::verb::post) {
        std::string version = extractPathParam(target, "/api/updates/download/");
        return handleDownload(req, version);
    } else if (target.find("/api/updates/apply/") == 0 && method == http::verb::post) {
        std::string version = extractPathParam(target, "/api/updates/apply/");
        return handleApply(req, version);
    } else if (target.find("/api/updates/rollback/") == 0 && method == http::verb::post) {
        std::string rollback_id = extractPathParam(target, "/api/updates/rollback/");
        return handleRollback(req, rollback_id);
    } else if (target == "/api/updates/rollback" && method == http::verb::get) {
        return handleListRollbacks(req);
    } else {
        return createErrorResponse(
            http::status::not_found,
            "Unknown hot-reload API endpoint",
            req
        );
    }
}

http::response<http::string_body> HotReloadApiHandler::handleGetManifest(
    const http::request<http::string_body>& req,
    const std::string& version
) {
    try {
        auto manifest = manifest_db_->getManifest(version);
        if (!manifest) {
            return createErrorResponse(
                http::status::not_found,
                "Manifest not found for version: " + version,
                req
            );
        }
        
        auto response_json = manifest->toJson();
        return createJsonResponse(http::status::ok, response_json, req);
    } catch (const std::exception& e) {
        LOG_ERROR("Error getting manifest: {}", e.what());
        return createErrorResponse(
            http::status::internal_server_error,
            std::string("Failed to get manifest: ") + e.what(),
            req
        );
    }
}

http::response<http::string_body> HotReloadApiHandler::handleDownload(
    const http::request<http::string_body>& req,
    const std::string& version
) {
    try {
        LOG_INFO("Download request for version: {}", version);
        
        auto result = reload_engine_->downloadRelease(version);
        
        json response_json;
        response_json["success"] = result.success;
        response_json["version"] = version;
        
        if (result.success) {
            response_json["download_path"] = result.download_path;
            response_json["manifest"] = result.manifest.toJson();
        } else {
            response_json["error"] = result.error_message;
        }
        
        auto status_code = result.success ? http::status::ok : http::status::bad_request;
        return createJsonResponse(status_code, response_json, req);
    } catch (const std::exception& e) {
        LOG_ERROR("Error downloading release: {}", e.what());
        return createErrorResponse(
            http::status::internal_server_error,
            std::string("Failed to download release: ") + e.what(),
            req
        );
    }
}

http::response<http::string_body> HotReloadApiHandler::handleApply(
    const http::request<http::string_body>& req,
    const std::string& version
) {
    try {
        LOG_INFO("Apply hot-reload request for version: {}", version);
        
        // Parse request body for options
        bool verify_only = false;
        if (!req.body().empty()) {
            try {
                auto request_json = json::parse(req.body());
                verify_only = request_json.value("verify_only", false);
            } catch (const json::exception&) {
                // Ignore parsing errors, use defaults
            }
        }
        
        auto result = reload_engine_->applyHotReload(version, verify_only);
        
        json response_json;
        response_json["success"] = result.success;
        response_json["version"] = version;
        response_json["verify_only"] = verify_only;
        
        if (result.success) {
            response_json["files_updated"] = result.files_updated;
            if (!result.rollback_id.empty()) {
                response_json["rollback_id"] = result.rollback_id;
            }
        } else {
            response_json["error"] = result.error_message;
        }
        
        auto status_code = result.success ? http::status::ok : http::status::bad_request;
        return createJsonResponse(status_code, response_json, req);
    } catch (const std::exception& e) {
        LOG_ERROR("Error applying hot-reload: {}", e.what());
        return createErrorResponse(
            http::status::internal_server_error,
            std::string("Failed to apply hot-reload: ") + e.what(),
            req
        );
    }
}

http::response<http::string_body> HotReloadApiHandler::handleRollback(
    const http::request<http::string_body>& req,
    const std::string& rollback_id
) {
    try {
        LOG_INFO("Rollback request for ID: {}", rollback_id);
        
        bool success = reload_engine_->rollback(rollback_id);
        
        json response_json;
        response_json["success"] = success;
        response_json["rollback_id"] = rollback_id;
        
        if (!success) {
            response_json["error"] = "Rollback failed";
        }
        
        auto status_code = success ? http::status::ok : http::status::internal_server_error;
        return createJsonResponse(status_code, response_json, req);
    } catch (const std::exception& e) {
        LOG_ERROR("Error during rollback: {}", e.what());
        return createErrorResponse(
            http::status::internal_server_error,
            std::string("Failed to rollback: ") + e.what(),
            req
        );
    }
}

http::response<http::string_body> HotReloadApiHandler::handleListRollbacks(
    const http::request<http::string_body>& req
) {
    try {
        auto rollback_points = reload_engine_->listRollbackPoints();
        
        json response_json = json::array();
        for (const auto& [id, timestamp] : rollback_points) {
            json point;
            point["rollback_id"] = id;
            point["timestamp"] = timestamp;
            response_json.push_back(point);
        }
        
        json result;
        result["rollback_points"] = response_json;
        result["count"] = rollback_points.size();
        
        return createJsonResponse(http::status::ok, result, req);
    } catch (const std::exception& e) {
        LOG_ERROR("Error listing rollbacks: {}", e.what());
        return createErrorResponse(
            http::status::internal_server_error,
            std::string("Failed to list rollbacks: ") + e.what(),
            req
        );
    }
}

http::response<http::string_body> HotReloadApiHandler::createJsonResponse(
    http::status status,
    const json& body,
    const http::request<http::string_body>& req
) {
    http::response<http::string_body> res{status, req.version()};
    res.set(http::field::content_type, "application/json");
    res.set(http::field::server, "ThemisDB");
    res.body() = body.dump(2);
    res.prepare_payload();
    res.keep_alive(req.keep_alive());
    return res;
}

http::response<http::string_body> HotReloadApiHandler::createErrorResponse(
    http::status status,
    const std::string& message,
    const http::request<http::string_body>& req
) {
    json error_json;
    error_json["error"] = message;
    error_json["status"] = static_cast<int>(status);
    
    return createJsonResponse(status, error_json, req);
}

std::string HotReloadApiHandler::extractPathParam(const std::string& path, const std::string& prefix) {
    if (path.find(prefix) == 0) {
        std::string param = path.substr(prefix.length());
        // Remove query string if present
        auto qpos = param.find('?');
        if (qpos != std::string::npos) {
            param = param.substr(0, qpos);
        }
        return param;
    }
    return "";
}

} // namespace server
} // namespace themis
