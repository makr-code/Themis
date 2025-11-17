#pragma once

#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

#include "security/encryption.h"
#include "utils/pki_client.h"

namespace themis {
namespace utils {

/**
 * @brief Security event types for audit logging
 */
enum class SecurityEventType {
    // Authentication & Authorization
    LOGIN_SUCCESS,
    LOGIN_FAILED,
    LOGOUT,
    TOKEN_CREATED,
    TOKEN_REVOKED,
    UNAUTHORIZED_ACCESS,
    PERMISSION_DENIED,
    
    // Privilege Escalation
    PRIVILEGE_ESCALATION_ATTEMPT,
    ROLE_CHANGED,
    SCOPE_GRANTED,
    SCOPE_REVOKED,
    
    // Key Management
    KEY_CREATED,
    KEY_ROTATED,
    KEY_DELETED,
    KEY_ACCESS,
    
    // Data Access
    DATA_READ,
    DATA_WRITE,
    DATA_DELETE,
    BULK_EXPORT,
    
    // PII Operations
    PII_ACCESSED,
    PII_REVEALED,
    PII_ERASED,
    
    // Configuration Changes
    CONFIG_CHANGED,
    POLICY_UPDATED,
    ENCRYPTION_SCHEMA_CHANGED,
    
    // Security Incidents
    BRUTE_FORCE_DETECTED,
    RATE_LIMIT_EXCEEDED,
    SUSPICIOUS_ACTIVITY,
    INTEGRITY_VIOLATION,
    
    // System Events
    SERVER_STARTED,
    SERVER_STOPPED,
    BACKUP_CREATED,
    RESTORE_COMPLETED,
    
    // Generic
    CUSTOM_EVENT
};

struct AuditLoggerConfig {
    bool enabled = true;
    bool encrypt_then_sign = true;
    std::string log_path = "data/logs/audit.jsonl"; // JSON Lines sink
    std::string key_id = "saga_log";               // logical key id for log encryption
    
    // Hash chain for tamper-proofing
    bool enable_hash_chain = true;
    std::string chain_state_file = "data/logs/audit_chain.json";
    
    // SIEM integration
    bool enable_siem = false;
    std::string siem_type = "syslog"; // "syslog" or "splunk"
    std::string siem_host = "localhost";
    uint16_t siem_port = 514; // syslog default
    std::string splunk_token; // Splunk HEC token
};

// Minimal Audit Logger supporting Encrypt-then-Sign batches (single-entry for now)
class AuditLogger {
public:
    AuditLogger(std::shared_ptr<themis::FieldEncryption> enc,
                std::shared_ptr<VCCPKIClient> pki,
                AuditLoggerConfig cfg);

    // Log a generic data access/audit event; if encrypt_then_sign is enabled,
    // encrypts the canonical JSON with FieldEncryption, computes SHA-256 over
    // ciphertext (iv|ciphertext|tag), obtains a signature from PKI client, and
    // appends a JSON record to log_path.
    void logEvent(const nlohmann::json& event);
    
    /**
     * @brief Log a security event with structured data
     * @param event_type Security event type
     * @param user_id User/service account performing the action
     * @param resource Resource being accessed (e.g., entity key, API endpoint)
     * @param details Additional event-specific details
     */
    void logSecurityEvent(
        SecurityEventType event_type,
        const std::string& user_id,
        const std::string& resource,
        const nlohmann::json& details = {}
    );
    
    /**
     * @brief Verify integrity of audit log hash chain
     * @return true if chain is valid, false if tampering detected
     */
    bool verifyChainIntegrity();
    
    /**
     * @brief Get current hash chain state
     * @return JSON with last_hash, entry_count, last_timestamp
     */
    nlohmann::json getChainState() const;

private:
    std::shared_ptr<themis::FieldEncryption> enc_;
    std::shared_ptr<VCCPKIClient> pki_;
    AuditLoggerConfig cfg_;

    std::mutex file_mu_;
    
    // Hash chain state (for tamper-proofing)
    std::string last_hash_;
    uint64_t entry_count_ = 0;
    std::chrono::system_clock::time_point last_timestamp_;
    mutable std::mutex chain_mu_;

    static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
    void appendJsonLine(const nlohmann::json& j);
    void forwardToSiem(const nlohmann::json& event);
    void loadChainState();
    void saveChainState();
    std::string computeEntryHash(const nlohmann::json& entry) const;
    static std::string securityEventTypeToString(SecurityEventType type);
};

} // namespace utils
} // namespace themis
