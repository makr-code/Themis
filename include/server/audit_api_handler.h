#pragma once

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include "utils/audit_logger.h"
#include "security/encryption.h"
#include "utils/pki_client.h"
#include "utils/lek_manager.h"

namespace themis {
namespace server {

struct AuditLogEntry {
    int64_t id;
    int64_t timestamp_ms;
    std::string user;
    std::string action;
    std::string entity_type;
    std::string entity_id;
    std::string old_value;
    std::string new_value;
    bool success;
    std::string ip_address;
    std::string session_id;
    std::string error_message;
    
    nlohmann::json toJson() const;
};

struct AuditQueryFilter {
    int64_t start_ts_ms = 0;
    int64_t end_ts_ms = LLONG_MAX;
    std::string user;
    std::string action;
    std::string entity_type;
    std::string entity_id;
    bool success_only = false;
    int page = 1;
    int page_size = 100;
};

class AuditApiHandler {
public:
    AuditApiHandler(std::shared_ptr<themis::FieldEncryption> enc,
                    std::shared_ptr<themis::utils::VCCPKIClient> pki,
                    const std::string& log_path,
                    std::shared_ptr<themis::utils::LEKManager> lek_manager = nullptr);

    // Query audit logs with filtering and pagination
    nlohmann::json queryAuditLogs(const AuditQueryFilter& filter);
    
    // Export audit logs as CSV
    std::string exportAuditLogsCsv(const AuditQueryFilter& filter);

private:
    std::shared_ptr<themis::FieldEncryption> enc_;
    std::shared_ptr<themis::utils::VCCPKIClient> pki_;
    std::string log_path_;
    std::shared_ptr<themis::utils::LEKManager> lek_manager_;

    // Read and decrypt audit log entries from JSONL file
    std::vector<AuditLogEntry> readAuditLogs(const AuditQueryFilter& filter);
    
    // Parse single JSON line to AuditLogEntry
    AuditLogEntry parseLogLine(const nlohmann::json& j, int64_t line_id);
    
    // Decrypt encrypted audit payload
    std::string decryptPayload(const nlohmann::json& payload);
};

} // namespace server
} // namespace themis
