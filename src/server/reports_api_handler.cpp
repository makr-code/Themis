#include "server/reports_api_handler.h"
#include "utils/logger.h"
#include <fstream>
#include <sstream>
#include <chrono>

namespace themis { namespace server {

nlohmann::json ReportsApiHandler::generateComplianceReport(const std::string& report_type) {
    try {
        // Minimal MVP: aggregate basic metrics from audit log JSONL
        const std::string audit_log_path = "data/logs/audit.jsonl";

        std::ifstream in(audit_log_path);
        size_t total_events = 0;
        size_t error_events = 0;
        size_t signed_events = 0;
        size_t encrypted_events = 0;

        // Try capturing time range (best-effort)
        std::string first_ts;
        std::string last_ts;

        if (in.good()) {
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                total_events++;
                try {
                    auto j = nlohmann::json::parse(line);

                    // Heuristics: consider levels and presence of signature/cipher fields
                    if (j.contains("level")) {
                        auto lvl = j["level"].get<std::string>();
                        if (!lvl.empty()) {
                            std::string l = lvl;
                            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                            if (l == "error" || l == "err" || l == "fatal") error_events++;
                        }
                    }

                    if (j.contains("signature") || j.contains("signature_id")) signed_events++;
                    if (j.contains("ciphertext") || j.contains("encrypted") || j.contains("enc")) encrypted_events++;

                    // Timestamps: support ts or timestamp
                    if (j.contains("ts") && j["ts"].is_string()) {
                        const auto ts = j["ts"].get<std::string>();
                        if (first_ts.empty()) first_ts = ts;
                        last_ts = ts;
                    } else if (j.contains("timestamp") && j["timestamp"].is_string()) {
                        const auto ts = j["timestamp"].get<std::string>();
                        if (first_ts.empty()) first_ts = ts;
                        last_ts = ts;
                    }
                } catch (...) {
                    // Ignore parse errors for robustness
                }
            }
        } else {
            THEMIS_WARN("Reports API: audit log not found at {} - returning empty metrics", audit_log_path);
        }

        // Build response
        auto now = std::chrono::system_clock::now();
        std::time_t now_tt = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::tm* gmt = std::gmtime(&now_tt);
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmt);

        nlohmann::json metrics = {
            {"total_events", total_events},
            {"error_events", error_events},
            {"signed_events", signed_events},
            {"encrypted_events", encrypted_events}
        };

        if (!first_ts.empty()) metrics["period_start"] = first_ts;
        if (!last_ts.empty()) metrics["period_end"] = last_ts;

        THEMIS_INFO("Reports API: Generated '{}' compliance report: total={}, errors={}", report_type, total_events, error_events);

        return {
            {"report_type", report_type.empty() ? "overview" : report_type},
            {"generated_at", std::string(buf)},
            {"metrics", metrics}
        };

    } catch (const std::exception& ex) {
        THEMIS_ERROR("Reports API generateComplianceReport failed: {}", ex.what());
        return {
            {"error", "Internal Server Error"},
            {"message", ex.what()},
            {"status_code", 500}
        };
    }
}

}} // namespace themis::server
