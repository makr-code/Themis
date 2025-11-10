#pragma once

#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

#include "security/encryption.h"
#include "utils/pki_client.h"
#include "utils/lek_manager.h"

namespace themis {
namespace utils {

struct AuditLoggerConfig {
    bool enabled = true;
    bool encrypt_then_sign = true;
    std::string log_path = "data/logs/audit.jsonl"; // JSON Lines sink
    std::string key_id = "saga_log";               // fallback key id (unused if LEK enabled)
    bool use_lek = false;                          // Use LEKManager for daily rotation
};

// Minimal Audit Logger supporting Encrypt-then-Sign batches (single-entry for now)
class AuditLogger {
public:
    AuditLogger(std::shared_ptr<themis::FieldEncryption> enc,
                std::shared_ptr<VCCPKIClient> pki,
                AuditLoggerConfig cfg,
                std::shared_ptr<LEKManager> lek_manager = nullptr);

    // Log a generic data access/audit event; if encrypt_then_sign is enabled,
    // encrypts the canonical JSON with FieldEncryption, computes SHA-256 over
    // ciphertext (iv|ciphertext|tag), obtains a signature from PKI client, and
    // appends a JSON record to log_path.
    void logEvent(const nlohmann::json& event);

private:
    std::shared_ptr<themis::FieldEncryption> enc_;
    std::shared_ptr<VCCPKIClient> pki_;
    std::shared_ptr<LEKManager> lek_manager_;
    AuditLoggerConfig cfg_;

    std::mutex file_mu_;

    static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
    void appendJsonLine(const nlohmann::json& j);
};

} // namespace utils
} // namespace themis
