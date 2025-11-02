#include "server/audit_api_handler.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace themis {
namespace server {

// Helper: Parse ISO 8601 datetime string to milliseconds since epoch
static int64_t parseIso8601ToMs(const std::string& iso_str) {
    if (iso_str.empty()) return 0;
    
    // Simple parser for "YYYY-MM-DDTHH:MM:SS" format
    // For production, use std::chrono or date library
    std::tm tm = {};
    std::istringstream ss(iso_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    if (ss.fail()) return 0;
    
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

// Helper: Case-insensitive string contains
static bool containsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != haystack.end();
}

AuditApiHandler::AuditApiHandler(std::shared_ptr<themis::FieldEncryption> enc,
                                 std::shared_ptr<themis::utils::VCCPKIClient> pki,
                                 const std::string& log_path)
    : enc_(std::move(enc)), pki_(std::move(pki)), log_path_(log_path) {}

nlohmann::json AuditLogEntry::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["timestamp"] = nlohmann::json(); // Will be converted from timestamp_ms
    
    // Convert milliseconds to ISO 8601
    auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp_ms));
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::gmtime(&time_t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    j["timestamp"] = std::string(buf) + "Z";
    
    j["user"] = user;
    j["action"] = action;
    j["entityType"] = entity_type;
    j["entityId"] = entity_id;
    j["oldValue"] = old_value.empty() ? nullptr : nlohmann::json(old_value);
    j["newValue"] = new_value.empty() ? nullptr : nlohmann::json(new_value);
    j["success"] = success;
    j["ipAddress"] = ip_address.empty() ? nullptr : nlohmann::json(ip_address);
    j["sessionId"] = session_id.empty() ? nullptr : nlohmann::json(session_id);
    j["errorMessage"] = error_message.empty() ? nullptr : nlohmann::json(error_message);
    
    return j;
}

std::string AuditApiHandler::decryptPayload(const nlohmann::json& payload) {
    try {
        if (!payload.contains("ciphertext_b64")) {
            return "";
        }
        
        // Reconstruct EncryptedBlob from JSON
        themis::EncryptedBlob blob;
        blob.key_id = payload.value("key_id", "");
        blob.key_version = payload.value("key_version", 0);
        
        // Convert base64 strings back to bytes (simplified - production needs proper base64 decode)
        auto ciphertext_b64 = payload["ciphertext_b64"].get<std::string>();
        auto iv_b64 = payload.value("iv_b64", std::string());
        auto tag_b64 = payload.value("tag_b64", std::string());
        
        // Use FieldEncryption's decrypt method
        // For now, we'll use a simplified approach - in production, properly reconstruct the blob
        nlohmann::json blob_json = {
            {"key_id", blob.key_id},
            {"key_version", blob.key_version},
            {"iv", iv_b64},
            {"ciphertext", ciphertext_b64},
            {"tag", tag_b64}
        };
        
        auto reconstructed = themis::EncryptedBlob::fromJson(blob_json);
        return enc_->decrypt(reconstructed);
        
    } catch (const std::exception& e) {
        return "[Decryption failed: " + std::string(e.what()) + "]";
    }
}

AuditLogEntry AuditApiHandler::parseLogLine(const nlohmann::json& j, int64_t line_id) {
    AuditLogEntry entry;
    entry.id = line_id;
    entry.timestamp_ms = j.value("ts", 0LL);
    
    // Try to decrypt payload if encrypted
    std::string event_data;
    if (j.contains("payload") && j["payload"].contains("ciphertext_b64")) {
        event_data = decryptPayload(j["payload"]);
    } else if (j.contains("payload") && j["payload"].is_string()) {
        event_data = j["payload"].get<std::string>();
    }
    
    // Parse event data (expected to be JSON)
    nlohmann::json event;
    try {
        if (!event_data.empty()) {
            event = nlohmann::json::parse(event_data);
        }
    } catch (...) {
        // If parsing fails, treat as raw string
    }
    
    // Extract fields from event
    entry.user = event.value("user", "system");
    entry.action = event.value("action", "UNKNOWN");
    entry.entity_type = event.value("entity_type", "");
    entry.entity_id = event.value("entity_id", "");
    entry.old_value = event.value("old_value", "");
    entry.new_value = event.value("new_value", "");
    entry.success = event.value("success", true);
    entry.ip_address = event.value("ip_address", "");
    entry.session_id = event.value("session_id", "");
    entry.error_message = event.value("error", "");
    
    return entry;
}

std::vector<AuditLogEntry> AuditApiHandler::readAuditLogs(const AuditQueryFilter& filter) {
    std::vector<AuditLogEntry> entries;
    
    std::ifstream ifs(log_path_);
    if (!ifs.is_open()) {
        return entries; // Empty if log file doesn't exist
    }
    
    std::string line;
    int64_t line_id = 0;
    
    while (std::getline(ifs, line)) {
        line_id++;
        
        if (line.empty()) continue;
        
        try {
            auto j = nlohmann::json::parse(line);
            auto entry = parseLogLine(j, line_id);
            
            // Apply filters
            if (entry.timestamp_ms < filter.start_ts_ms || entry.timestamp_ms > filter.end_ts_ms) {
                continue;
            }
            
            if (!filter.user.empty() && !containsCaseInsensitive(entry.user, filter.user)) {
                continue;
            }
            
            if (!filter.action.empty() && !containsCaseInsensitive(entry.action, filter.action)) {
                continue;
            }
            
            if (!filter.entity_type.empty() && !containsCaseInsensitive(entry.entity_type, filter.entity_type)) {
                continue;
            }
            
            if (!filter.entity_id.empty() && !containsCaseInsensitive(entry.entity_id, filter.entity_id)) {
                continue;
            }
            
            if (filter.success_only && !entry.success) {
                continue;
            }
            
            entries.push_back(entry);
            
        } catch (const std::exception& e) {
            // Skip malformed lines
            continue;
        }
    }
    
    // Sort by timestamp descending (newest first)
    std::sort(entries.begin(), entries.end(), 
              [](const AuditLogEntry& a, const AuditLogEntry& b) {
                  return a.timestamp_ms > b.timestamp_ms;
              });
    
    return entries;
}

nlohmann::json AuditApiHandler::queryAuditLogs(const AuditQueryFilter& filter) {
    auto all_entries = readAuditLogs(filter);
    
    int total_count = static_cast<int>(all_entries.size());
    int start_idx = (filter.page - 1) * filter.page_size;
    int end_idx = std::min(start_idx + filter.page_size, total_count);
    
    nlohmann::json result;
    result["entries"] = nlohmann::json::array();
    
    if (start_idx < total_count) {
        for (int i = start_idx; i < end_idx; i++) {
            result["entries"].push_back(all_entries[i].toJson());
        }
    }
    
    result["totalCount"] = total_count;
    result["page"] = filter.page;
    result["pageSize"] = filter.page_size;
    result["hasMore"] = end_idx < total_count;
    
    return result;
}

std::string AuditApiHandler::exportAuditLogsCsv(const AuditQueryFilter& filter) {
    auto entries = readAuditLogs(filter);
    
    std::ostringstream csv;
    
    // Header
    csv << "Id,Timestamp,User,Action,EntityType,EntityId,OldValue,NewValue,Success,IpAddress,SessionId,ErrorMessage\n";
    
    // Rows
    for (const auto& entry : entries) {
        // Helper to escape CSV fields
        auto escape = [](const std::string& s) {
            if (s.find(',') != std::string::npos || s.find('"') != std::string::npos || s.find('\n') != std::string::npos) {
                std::string escaped = "\"";
                for (char c : s) {
                    if (c == '"') escaped += "\"\"";
                    else escaped += c;
                }
                escaped += "\"";
                return escaped;
            }
            return s;
        };
        
        // Convert timestamp to ISO 8601
        auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(entry.timestamp_ms));
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time_t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        
        csv << entry.id << ","
            << escape(std::string(buf)) << ","
            << escape(entry.user) << ","
            << escape(entry.action) << ","
            << escape(entry.entity_type) << ","
            << escape(entry.entity_id) << ","
            << escape(entry.old_value) << ","
            << escape(entry.new_value) << ","
            << (entry.success ? "true" : "false") << ","
            << escape(entry.ip_address) << ","
            << escape(entry.session_id) << ","
            << escape(entry.error_message) << "\n";
    }
    
    return csv.str();
}

} // namespace server
} // namespace themis
