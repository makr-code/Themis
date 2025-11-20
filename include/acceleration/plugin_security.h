#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace themis {
namespace acceleration {

// Plugin signature verification
struct PluginSignature {
    std::string sha256Hash;          // SHA-256 Hash der DLL/SO Datei
    std::string signature;           // Digitale Signatur (RSA/ECDSA)
    std::string signingCertificate;  // X.509 Zertifikat des Signierers
    std::string issuer;              // Zertifikat-Aussteller
    std::string subject;             // Zertifikat-Inhaber
    uint64_t timestamp = 0;          // Unix-Timestamp der Signatur
    bool verified = false;
};

// Plugin metadata and security info
struct PluginMetadata {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string license;
    
    // Security
    PluginSignature signature;
    std::vector<std::string> requiredCapabilities;
    std::vector<std::string> permissions;  // z.B. "gpu_access", "network", "filesystem"
    
    // Build info
    std::string buildDate;
    std::string buildCommit;
    std::string compilerVersion;
};

// Plugin security policy
enum class PluginTrustLevel {
    TRUSTED,        // Signiert von vertrauenswürdigem Zertifikat
    UNTRUSTED,      // Nicht signiert oder unbekannter Signatur
    BLOCKED         // Auf Blacklist
};

struct PluginSecurityPolicy {
    // Require signature verification
    bool requireSignature = true;
    
    // Require specific certificate issuer
    std::vector<std::string> trustedIssuers = {
        "CN=ThemisDB Official Plugins, O=ThemisDB, C=DE"
    };
    
    // Allow unsigned plugins (for development)
    bool allowUnsigned = false;
    
    // Verify file hash before loading
    bool verifyFileHash = true;
    
    // Check certificate revocation (CRL/OCSP)
    bool checkRevocation = true;
    
    // Minimum trust level required
    PluginTrustLevel minTrustLevel = PluginTrustLevel::TRUSTED;
    
    // Blacklist of plugin hashes (known malicious)
    std::vector<std::string> blacklistedHashes;
    
    // Whitelist of plugin hashes (explicitly allowed)
    std::vector<std::string> whitelistedHashes;
};

// Plugin security verifier
class PluginSecurityVerifier {
public:
    explicit PluginSecurityVerifier(const PluginSecurityPolicy& policy);
    ~PluginSecurityVerifier() = default;
    
    // Verify a plugin before loading
    // Returns true if plugin is safe to load, false otherwise
    bool verifyPlugin(const std::string& pluginPath, std::string& errorMessage);
    
    // Calculate SHA-256 hash of plugin file
    std::string calculateFileHash(const std::string& filePath);
    
    // Verify digital signature
    bool verifySignature(const std::string& filePath, const PluginSignature& signature);
    
    // Parse plugin metadata from JSON sidecar file
    std::optional<PluginMetadata> loadMetadata(const std::string& pluginPath);
    
    // Verify certificate chain
    bool verifyCertificateChain(const std::string& certificate);
    
    // Check if plugin is on blacklist
    bool isBlacklisted(const std::string& fileHash) const;
    
    // Check if plugin is on whitelist
    bool isWhitelisted(const std::string& fileHash) const;
    
    // Get trust level for plugin
    PluginTrustLevel getTrustLevel(const PluginMetadata& metadata);
    
    // Update security policy at runtime
    void updatePolicy(const PluginSecurityPolicy& policy);
    
    // Get current policy
    const PluginSecurityPolicy& getPolicy() const { return policy_; }
    
private:
    PluginSecurityPolicy policy_;
    
    // OpenSSL integration for signature verification
    bool verifyRSASignature(const std::vector<uint8_t>& data, 
                           const std::vector<uint8_t>& signature,
                           const std::string& publicKey);
    
    bool verifyECDSASignature(const std::vector<uint8_t>& data,
                             const std::vector<uint8_t>& signature, 
                             const std::string& publicKey);
    
    // Load and verify X.509 certificate
    bool loadCertificate(const std::string& certPEM);
    
    // Check certificate revocation list
    bool checkCRL(const std::string& certificate);
    
    // OCSP (Online Certificate Status Protocol) check
    bool checkOCSP(const std::string& certificate);
};

// Audit logging for plugin security events
struct PluginSecurityEvent {
    enum class EventType {
        PLUGIN_LOADED,
        PLUGIN_LOAD_FAILED,
        SIGNATURE_VERIFIED,
        SIGNATURE_VERIFICATION_FAILED,
        HASH_MISMATCH,
        BLACKLISTED,
        UNTRUSTED_ISSUER,
        CERTIFICATE_EXPIRED,
        CERTIFICATE_REVOKED,
        POLICY_VIOLATION
    };
    
    EventType type;
    std::string pluginPath;
    std::string pluginHash;
    std::string message;
    uint64_t timestamp;
    std::string severity;  // INFO, WARNING, ERROR, CRITICAL
};

class PluginSecurityAuditor {
public:
    static PluginSecurityAuditor& instance();
    
    // Log security event
    void logEvent(const PluginSecurityEvent& event);
    
    // Get security events for a specific plugin
    std::vector<PluginSecurityEvent> getEventsForPlugin(const std::string& pluginPath) const;
    
    // Get all security events
    const std::vector<PluginSecurityEvent>& getAllEvents() const { return events_; }
    
    // Clear event log
    void clearEvents();
    
    // Export events to file (for compliance/audit)
    bool exportEvents(const std::string& outputPath) const;
    
private:
    PluginSecurityAuditor() = default;
    ~PluginSecurityAuditor() = default;
    PluginSecurityAuditor(const PluginSecurityAuditor&) = delete;
    PluginSecurityAuditor& operator=(const PluginSecurityAuditor&) = delete;
    
    std::vector<PluginSecurityEvent> events_;
};

} // namespace acceleration
} // namespace themis
