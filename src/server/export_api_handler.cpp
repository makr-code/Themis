#include "server/export_api_handler.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>

using json = nlohmann::json;

namespace themis {
namespace server {

ExportApiHandler::ExportApiHandler(
    std::shared_ptr<RocksDBWrapper> storage,
    std::shared_ptr<SecondaryIndexManager> secondary_index)
    : storage_(std::move(storage))
    , secondary_index_(std::move(secondary_index)) {
}

ExportApiHandler::~ExportApiHandler() = default;

http::response<http::string_body> ExportApiHandler::handleExportJsonlLlm(
    const http::request<http::string_body>& req) {
    
    // Validate admin authentication
    if (!validateAdminToken(req)) {
        return errorResponse(http::status::unauthorized, "Unauthorized: Admin token required");
    }

    try {
        // Parse request body
        auto request_json = json::parse(req.body());

        // Build AQL query from parameters
        std::string aql_query = buildAqlQuery(request_json);
        
        THEMIS_INFO("JSONL LLM Export request: query={}", aql_query);
        
        // Load JSONL LLM exporter plugin
        auto& pm = plugins::PluginManager::instance();
        auto* plugin = pm.loadPlugin("jsonl_llm_exporter");
        if (!plugin) {
            return errorResponse(http::status::internal_server_error,
                "JSONL LLM exporter plugin not found");
        }

        auto* exporter = static_cast<exporters::IExporter*>(plugin->getInstance());
        if (!exporter) {
            return errorResponse(http::status::internal_server_error,
                "Failed to get exporter instance");
        }

        // Generate export ID and output path
        std::string export_id = generateExportId();
        std::filesystem::create_directories("/tmp/themis_exports");
        std::string output_path = "/tmp/themis_exports/export_" + export_id + ".jsonl";

        // Configure export options
        exporters::ExportOptions export_options;
        export_options.output_path = output_path;
        
        if (request_json.contains("batch_size")) {
            export_options.batch_size = request_json["batch_size"];
        }

        // TODO: Query database using AQL query
        // For now, return placeholder response indicating implementation is ready
        std::vector<BaseEntity> entities;  // Would be populated from query
        
        // Perform export
        auto stats = exporter->exportEntities(entities, export_options);

        // Read exported file and stream back
        std::ifstream exported_file(output_path);
        std::stringstream buffer;
        buffer << exported_file.rdbuf();
        std::string jsonl_content = buffer.str();
        exported_file.close();

        // Cleanup temp file
        std::filesystem::remove(output_path);

        // Create streaming response
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/x-ndjson");
        
        // Add filename based on theme and date range
        std::string filename = "export_" + export_id;
        if (request_json.contains("theme")) {
            filename += "_" + request_json["theme"].get<std::string>();
        }
        filename += ".jsonl";
        
        res.set(http::field::content_disposition,
            "attachment; filename=\"" + filename + "\"");
        res.body() = jsonl_content;
        res.prepare_payload();
        
        THEMIS_INFO("JSONL LLM Export completed: export_id={}, records={}", 
                    export_id, stats.successful_records);
        
        return res;

    } catch (const json::exception& e) {
        return errorResponse(http::status::bad_request,
            std::string("JSON parsing error: ") + e.what());
    } catch (const std::exception& e) {
        return errorResponse(http::status::internal_server_error,
            std::string("Export error: ") + e.what());
    }
}

http::response<http::string_body> ExportApiHandler::handleExportStatus(
    const http::request<http::string_body>& req) {
    
    // Validate admin authentication
    if (!validateAdminToken(req)) {
        return errorResponse(http::status::unauthorized, "Unauthorized: Admin token required");
    }

    try {
        // Extract export_id from path
        std::string target(req.target().data(), req.target().size());
        auto last_slash = target.find_last_of('/');
        if (last_slash == std::string::npos) {
            return errorResponse(http::status::bad_request, "Invalid export ID");
        }
        
        std::string export_id = target.substr(last_slash + 1);

        // Find export job
        std::lock_guard<std::mutex> lock(export_jobs_mutex_);
        auto it = export_jobs_.find(export_id);
        if (it == export_jobs_.end()) {
            return errorResponse(http::status::not_found, "Export job not found");
        }

        const auto& job = it->second;
        
        // Build response
        json response;
        response["export_id"] = job.export_id;
        response["status"] = job.status;
        response["progress"] = job.progress;
        response["records_processed"] = job.records_processed;
        response["records_total"] = job.records_total;
        
        if (job.status == "completed") {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                job.completed_at - job.started_at);
            response["duration_ms"] = duration.count();
        }
        
        if (!job.error_message.empty()) {
            response["error"] = job.error_message;
        }

        return jsonResponse(http::status::ok, response.dump(2));

    } catch (const std::exception& e) {
        return errorResponse(http::status::internal_server_error,
            std::string("Status check error: ") + e.what());
    }
}

std::string ExportApiHandler::buildAqlQuery(const json& request_json) {
    std::string query;
    std::vector<std::string> conditions;
    
    // Thematic filtering (VCC-Clara use case)
    if (request_json.contains("theme")) {
        std::string theme = request_json["theme"];
        conditions.push_back("category='" + theme + "'");
    }
    
    if (request_json.contains("domain")) {
        std::string domain = request_json["domain"];
        conditions.push_back("domain='" + domain + "'");
    }
    
    if (request_json.contains("subject")) {
        std::string subject = request_json["subject"];
        conditions.push_back("subject='" + subject + "'");
    }
    
    // Temporal boundaries (VCC-Clara use case)
    if (request_json.contains("from_date")) {
        std::string from_date = request_json["from_date"];
        conditions.push_back("created_at>='" + from_date + "'");
    }
    
    if (request_json.contains("to_date")) {
        std::string to_date = request_json["to_date"];
        conditions.push_back("created_at<='" + to_date + "'");
    }
    
    // Quality filters
    if (request_json.contains("min_rating")) {
        double min_rating = request_json["min_rating"];
        conditions.push_back("rating>=" + std::to_string(min_rating));
    }
    
    // Custom AQL query (if provided)
    if (request_json.contains("query")) {
        std::string custom_query = request_json["query"];
        if (!custom_query.empty()) {
            conditions.push_back(custom_query);
        }
    }
    
    // Build final query
    if (!conditions.empty()) {
        query = conditions[0];
        for (size_t i = 1; i < conditions.size(); ++i) {
            query += " AND " + conditions[i];
        }
    }
    
    return query;
}

std::string ExportApiHandler::generateExportId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "exp_";
    
    for (int i = 0; i < 16; i++) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

bool ExportApiHandler::validateAdminToken(
    const http::request<http::string_body>& req) {
    
    // Check Authorization header
    auto auth_header = req[http::field::authorization];
    if (auth_header.empty()) {
        return false;
    }
    
    std::string auth_str(auth_header.data(), auth_header.size());
    
    // Check for "Bearer <token>" format
    if (auth_str.find("Bearer ") != 0) {
        return false;
    }
    
    std::string token = auth_str.substr(7);
    
    // Get admin token from environment
    const char* admin_token = std::getenv("THEMIS_TOKEN_ADMIN");
    if (!admin_token) {
        THEMIS_WARN("THEMIS_TOKEN_ADMIN not set");
        return false;
    }
    
    return token == admin_token;
}

http::response<http::string_body> ExportApiHandler::jsonResponse(
    http::status status,
    const std::string& json_body) {
    
    http::response<http::string_body> res{status, 11};
    res.set(http::field::content_type, "application/json");
    res.body() = json_body;
    res.prepare_payload();
    return res;
}

http::response<http::string_body> ExportApiHandler::errorResponse(
    http::status status,
    const std::string& error_message) {
    
    json error_json;
    error_json["status"] = "error";
    error_json["error"] = error_message;
    
    return jsonResponse(status, error_json.dump(2));
}

} // namespace server
} // namespace themis
