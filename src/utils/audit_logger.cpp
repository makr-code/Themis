#include "utils/audit_logger.h"
#include "utils/logger.h"

#include <filesystem>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace themis {
namespace utils {

// Local base64 (kept minimal to avoid new deps here)
static std::string base64_encode_local(const std::vector<uint8_t>& data) {
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | (data[i + 2]);
        out.push_back(b64_table[(n >> 18) & 63]);
        out.push_back(b64_table[(n >> 12) & 63]);
        out.push_back(b64_table[(n >> 6) & 63]);
        out.push_back(b64_table[n & 63]);
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t n = (data[i] << 16);
        out.push_back(b64_table[(n >> 18) & 63]);
        out.push_back(b64_table[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(b64_table[(n >> 18) & 63]);
        out.push_back(b64_table[(n >> 12) & 63]);
        out.push_back(b64_table[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

AuditLogger::AuditLogger(std::shared_ptr<themis::FieldEncryption> enc,
                         std::shared_ptr<VCCPKIClient> pki,
                         AuditLoggerConfig cfg)
    : enc_(std::move(enc)), pki_(std::move(pki)), cfg_(std::move(cfg)) {
    
    // Load hash chain state if tamper-proofing is enabled
    if (cfg_.enable_hash_chain) {
        loadChainState();
        
        // Verify chain integrity on startup
        if (entry_count_ > 0) {
            THEMIS_INFO("Verifying audit log chain integrity...");
            if (!verifyChainIntegrity()) {
                THEMIS_ERROR("Audit log chain integrity check FAILED - possible tampering detected!");
            } else {
                THEMIS_INFO("Audit log chain integrity verified OK ({} entries)", entry_count_);
            }
        }
    }
}

std::vector<uint8_t> AuditLogger::sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    ::SHA256(data.data(), data.size(), out.data());
    return out;
}

void AuditLogger::appendJsonLine(const nlohmann::json& j) {
    std::scoped_lock lk(file_mu_);
    auto path = std::filesystem::path(cfg_.log_path);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(cfg_.log_path, std::ios::app | std::ios::binary);
    ofs << j.dump() << "\n";
}

void AuditLogger::logEvent(const nlohmann::json& event) {
    if (!cfg_.enabled) return;

    // Canonical-ish JSON (nlohmann::json preserves insertion order; for true canonicalization more work is needed)
    std::string plain = event.dump();

    nlohmann::json record;
    record["ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    record["category"] = "AUDIT";
    
    // Add hash chain if enabled
    if (cfg_.enable_hash_chain) {
        std::lock_guard<std::mutex> lock(chain_mu_);
        record["chain_entry"] = entry_count_;
        record["prev_hash"] = last_hash_;
    }

    if (cfg_.encrypt_then_sign && enc_) {
        // Encrypt plaintext JSON with configured key
        auto blob = enc_->encrypt(plain, cfg_.key_id);

        // Build bytes for hashing: iv || ciphertext || tag
        std::vector<uint8_t> to_hash;
        to_hash.reserve(blob.iv.size() + blob.ciphertext.size() + blob.tag.size());
        to_hash.insert(to_hash.end(), blob.iv.begin(), blob.iv.end());
        to_hash.insert(to_hash.end(), blob.ciphertext.begin(), blob.ciphertext.end());
        to_hash.insert(to_hash.end(), blob.tag.begin(), blob.tag.end());

        auto hash = sha256(to_hash);
        auto sig = pki_ ? pki_->signHash(hash) : SignatureResult{};

        auto jblob = themis::EncryptedBlob{blob}.toJson();
        // Persist encrypted payload and signature metadata
        record["payload"] = {
            {"type", "ciphertext"},
            {"key_id", blob.key_id},
            {"key_version", blob.key_version},
            {"iv_b64", jblob["iv"]},
            {"ciphertext_b64", jblob["ciphertext"]},
            {"tag_b64", jblob["tag"]}
        };
        record["signature"] = {
            {"ok", sig.ok},
            {"id", sig.signature_id},
            {"algorithm", sig.algorithm},
            {"sig_b64", sig.signature_b64},
            {"cert_serial", sig.cert_serial}
        };
    } else {
        // No encryption: sign plaintext bytes (if PKI available)
        std::vector<uint8_t> bytes(plain.begin(), plain.end());
        auto hash = sha256(bytes);
        auto sig = pki_ ? pki_->signHash(hash) : SignatureResult{};
        record["payload"] = {
            {"type", "plaintext"},
            {"data_b64", base64_encode_local(bytes)}
        };
        record["signature"] = {
            {"ok", sig.ok},
            {"id", sig.signature_id},
            {"algorithm", sig.algorithm},
            {"sig_b64", sig.signature_b64},
            {"cert_serial", sig.cert_serial}
        };
    }
    
    // Update hash chain
    if (cfg_.enable_hash_chain) {
        std::lock_guard<std::mutex> lock(chain_mu_);
        last_hash_ = computeEntryHash(record);
        entry_count_++;
        last_timestamp_ = std::chrono::system_clock::now();
        saveChainState();
    }

    appendJsonLine(record);
    
    // Forward to SIEM if enabled
    if (cfg_.enable_siem) {
        forwardToSiem(event);
    }
}

// ============================================================================
// Security Event Logging
// ============================================================================

std::string AuditLogger::securityEventTypeToString(SecurityEventType type) {
    switch (type) {
        case SecurityEventType::LOGIN_SUCCESS: return "LOGIN_SUCCESS";
        case SecurityEventType::LOGIN_FAILED: return "LOGIN_FAILED";
        case SecurityEventType::LOGOUT: return "LOGOUT";
        case SecurityEventType::TOKEN_CREATED: return "TOKEN_CREATED";
        case SecurityEventType::TOKEN_REVOKED: return "TOKEN_REVOKED";
        case SecurityEventType::UNAUTHORIZED_ACCESS: return "UNAUTHORIZED_ACCESS";
        case SecurityEventType::PERMISSION_DENIED: return "PERMISSION_DENIED";
        case SecurityEventType::PRIVILEGE_ESCALATION_ATTEMPT: return "PRIVILEGE_ESCALATION_ATTEMPT";
        case SecurityEventType::ROLE_CHANGED: return "ROLE_CHANGED";
        case SecurityEventType::SCOPE_GRANTED: return "SCOPE_GRANTED";
        case SecurityEventType::SCOPE_REVOKED: return "SCOPE_REVOKED";
        case SecurityEventType::KEY_CREATED: return "KEY_CREATED";
        case SecurityEventType::KEY_ROTATED: return "KEY_ROTATED";
        case SecurityEventType::KEY_DELETED: return "KEY_DELETED";
        case SecurityEventType::KEY_ACCESS: return "KEY_ACCESS";
        case SecurityEventType::DATA_READ: return "DATA_READ";
        case SecurityEventType::DATA_WRITE: return "DATA_WRITE";
        case SecurityEventType::DATA_DELETE: return "DATA_DELETE";
        case SecurityEventType::BULK_EXPORT: return "BULK_EXPORT";
        case SecurityEventType::PII_ACCESSED: return "PII_ACCESSED";
        case SecurityEventType::PII_REVEALED: return "PII_REVEALED";
        case SecurityEventType::PII_ERASED: return "PII_ERASED";
        case SecurityEventType::CONFIG_CHANGED: return "CONFIG_CHANGED";
        case SecurityEventType::POLICY_UPDATED: return "POLICY_UPDATED";
        case SecurityEventType::ENCRYPTION_SCHEMA_CHANGED: return "ENCRYPTION_SCHEMA_CHANGED";
        case SecurityEventType::BRUTE_FORCE_DETECTED: return "BRUTE_FORCE_DETECTED";
        case SecurityEventType::RATE_LIMIT_EXCEEDED: return "RATE_LIMIT_EXCEEDED";
        case SecurityEventType::SUSPICIOUS_ACTIVITY: return "SUSPICIOUS_ACTIVITY";
        case SecurityEventType::INTEGRITY_VIOLATION: return "INTEGRITY_VIOLATION";
        case SecurityEventType::SERVER_STARTED: return "SERVER_STARTED";
        case SecurityEventType::SERVER_STOPPED: return "SERVER_STOPPED";
        case SecurityEventType::BACKUP_CREATED: return "BACKUP_CREATED";
        case SecurityEventType::RESTORE_COMPLETED: return "RESTORE_COMPLETED";
        case SecurityEventType::CUSTOM_EVENT: return "CUSTOM_EVENT";
        default: return "UNKNOWN";
    }
}

void AuditLogger::logSecurityEvent(
    SecurityEventType event_type,
    const std::string& user_id,
    const std::string& resource,
    const nlohmann::json& details
) {
    nlohmann::json event = {
        {"event_type", securityEventTypeToString(event_type)},
        {"user_id", user_id},
        {"resource", resource},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    
    if (!details.is_null() && !details.empty()) {
        event["details"] = details;
    }
    
    // Log severity based on event type
    if (event_type == SecurityEventType::LOGIN_FAILED ||
        event_type == SecurityEventType::UNAUTHORIZED_ACCESS ||
        event_type == SecurityEventType::PRIVILEGE_ESCALATION_ATTEMPT ||
        event_type == SecurityEventType::BRUTE_FORCE_DETECTED ||
        event_type == SecurityEventType::INTEGRITY_VIOLATION) {
        event["severity"] = "HIGH";
        THEMIS_WARN("Security event: {} - User: {} - Resource: {}", 
            securityEventTypeToString(event_type), user_id, resource);
    } else if (event_type == SecurityEventType::RATE_LIMIT_EXCEEDED ||
               event_type == SecurityEventType::PERMISSION_DENIED ||
               event_type == SecurityEventType::SUSPICIOUS_ACTIVITY) {
        event["severity"] = "MEDIUM";
    } else {
        event["severity"] = "LOW";
    }
    
    logEvent(event);
}

// ============================================================================
// Hash Chain Methods (Tamper-Proofing)
// ============================================================================

void AuditLogger::loadChainState() {
    std::lock_guard<std::mutex> lock(chain_mu_);
    
    if (!std::filesystem::exists(cfg_.chain_state_file)) {
        // Initialize empty chain
        last_hash_ = std::string(64, '0'); // Genesis hash
        entry_count_ = 0;
        last_timestamp_ = std::chrono::system_clock::now();
        return;
    }
    
    try {
        std::ifstream ifs(cfg_.chain_state_file);
        nlohmann::json state;
        ifs >> state;
        
        last_hash_ = state.value("last_hash", std::string(64, '0'));
        entry_count_ = state.value("entry_count", 0ull);
        
        if (state.contains("last_timestamp_ms")) {
            auto ms = state["last_timestamp_ms"].get<uint64_t>();
            last_timestamp_ = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(ms));
        }
        
        THEMIS_INFO("Loaded audit chain state: {} entries, last_hash={}...", 
            entry_count_, last_hash_.substr(0, 16));
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to load chain state: {}", e.what());
        // Reset to genesis
        last_hash_ = std::string(64, '0');
        entry_count_ = 0;
    }
}

void AuditLogger::saveChainState() {
    // Assumes chain_mu_ is already locked by caller
    nlohmann::json state = {
        {"last_hash", last_hash_},
        {"entry_count", entry_count_},
        {"last_timestamp_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
            last_timestamp_.time_since_epoch()).count()}
    };
    
    try {
        auto path = std::filesystem::path(cfg_.chain_state_file);
        std::filesystem::create_directories(path.parent_path());
        
        std::ofstream ofs(cfg_.chain_state_file);
        ofs << state.dump(2);
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to save chain state: {}", e.what());
    }
}

std::string AuditLogger::computeEntryHash(const nlohmann::json& entry) const {
    // Hash the JSON entry concatenated with previous hash
    std::string to_hash = last_hash_ + entry.dump();
    std::vector<uint8_t> bytes(to_hash.begin(), to_hash.end());
    auto hash_bytes = sha256(bytes);
    
    // Convert to hex string
    std::ostringstream oss;
    for (auto b : hash_bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

bool AuditLogger::verifyChainIntegrity() {
    if (!cfg_.enable_hash_chain) {
        return true; // Chain disabled, nothing to verify
    }
    
    if (!std::filesystem::exists(cfg_.log_path)) {
        return true; // No log file yet, chain is valid
    }
    
    try {
        std::ifstream ifs(cfg_.log_path);
        std::string line;
        std::string expected_prev_hash = std::string(64, '0'); // Genesis
        uint64_t line_num = 0;
        
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            
            line_num++;
            nlohmann::json entry = nlohmann::json::parse(line);
            
            if (!entry.contains("chain_entry") || !entry.contains("prev_hash")) {
                THEMIS_WARN("Entry {} missing chain fields", line_num);
                continue; // Skip entries without chain data (backward compat)
            }
            
            std::string prev_hash = entry["prev_hash"];
            if (prev_hash != expected_prev_hash) {
                THEMIS_ERROR("Chain integrity violation at entry {}: expected prev_hash={}, got={}",
                    line_num, expected_prev_hash.substr(0, 16), prev_hash.substr(0, 16));
                return false;
            }
            
            // Compute hash for this entry
            std::string to_hash = prev_hash + entry.dump();
            std::vector<uint8_t> bytes(to_hash.begin(), to_hash.end());
            auto hash_bytes = sha256(bytes);
            
            std::ostringstream oss;
            for (auto b : hash_bytes) {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            }
            expected_prev_hash = oss.str();
        }
        
        THEMIS_INFO("Audit log chain verified: {} entries, last_hash={}...", 
            line_num, expected_prev_hash.substr(0, 16));
        return true;
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Chain verification failed: {}", e.what());
        return false;
    }
}

nlohmann::json AuditLogger::getChainState() const {
    std::lock_guard<std::mutex> lock(chain_mu_);
    return {
        {"last_hash", last_hash_},
        {"entry_count", entry_count_},
        {"last_timestamp_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
            last_timestamp_.time_since_epoch()).count()},
        {"chain_enabled", cfg_.enable_hash_chain}
    };
}

// ============================================================================
// SIEM Integration
// ============================================================================

void AuditLogger::forwardToSiem(const nlohmann::json& event) {
    if (cfg_.siem_type == "syslog") {
        // RFC 5424 Syslog format
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            THEMIS_ERROR("Failed to create syslog socket");
            return;
        }
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cfg_.siem_port);
        inet_pton(AF_INET, cfg_.siem_host.c_str(), &addr.sin_addr);
        
        // Syslog message: <pri>version timestamp hostname app-name procid msgid structured-data msg
        std::ostringstream syslog_msg;
        syslog_msg << "<134>1 "; // Facility=16 (local use 0), Severity=6 (informational)
        
        // Timestamp (RFC 3339)
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_utc;
        gmtime_r(&time_t_now, &tm_utc);
        syslog_msg << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S") << "Z ";
        
        syslog_msg << "themisdb themis-audit - - - " << event.dump();
        
        std::string msg = syslog_msg.str();
        sendto(sock, msg.c_str(), msg.size(), 0, 
               (struct sockaddr*)&addr, sizeof(addr));
        
        close(sock);
        
    } else if (cfg_.siem_type == "splunk") {
        // Splunk HEC (HTTP Event Collector) - would require libcurl
        THEMIS_WARN("Splunk SIEM forwarding not yet implemented");
        // TODO: HTTP POST to https://<host>:8088/services/collector/event
        // with Authorization: Splunk <token>
    }
}
    
    // Forward to SIEM if enabled
    if (cfg_.enable_siem) {
        forwardToSiem(event);
    }
}

} // namespace utils
} // namespace themis
