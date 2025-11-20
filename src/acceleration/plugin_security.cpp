#include "acceleration/plugin_security.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <nlohmann/json.hpp>

namespace themis {
namespace acceleration {

using json = nlohmann::json;

// ============================================================================
// PluginSecurityVerifier Implementation
// ============================================================================

PluginSecurityVerifier::PluginSecurityVerifier(const PluginSecurityPolicy& policy)
    : policy_(policy) {
}

std::string PluginSecurityVerifier::calculateFileHash(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return "";
    }
    
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    const size_t bufferSize = 32768;
    std::vector<char> buffer(bufferSize);
    
    while (file.read(buffer.data(), bufferSize) || file.gcount() > 0) {
        SHA256_Update(&sha256, buffer.data(), file.gcount());
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    
    return ss.str();
}

std::optional<PluginMetadata> PluginSecurityVerifier::loadMetadata(const std::string& pluginPath) {
    // Look for metadata JSON file (pluginPath + ".json")
    std::string metadataPath = pluginPath + ".json";
    
    if (!std::filesystem::exists(metadataPath)) {
        // Try alternative location: same directory, same name
        std::filesystem::path p(pluginPath);
        metadataPath = p.parent_path() / (p.stem().string() + ".json");
        
        if (!std::filesystem::exists(metadataPath)) {
            return std::nullopt;
        }
    }
    
    try {
        std::ifstream file(metadataPath);
        json j;
        file >> j;
        
        PluginMetadata metadata;
        
        if (j.contains("plugin")) {
            auto& plugin = j["plugin"];
            metadata.name = plugin.value("name", "");
            metadata.version = plugin.value("version", "");
            metadata.author = plugin.value("author", "");
            metadata.description = plugin.value("description", "");
            metadata.license = plugin.value("license", "");
            
            if (plugin.contains("signature")) {
                auto& sig = plugin["signature"];
                metadata.signature.sha256Hash = sig.value("sha256", "");
                metadata.signature.signature = sig.value("signature", "");
                metadata.signature.signingCertificate = sig.value("certificate", "");
                metadata.signature.issuer = sig.value("issuer", "");
                metadata.signature.subject = sig.value("subject", "");
                metadata.signature.timestamp = sig.value("timestamp", 0ULL);
            }
            
            if (plugin.contains("permissions")) {
                metadata.permissions = plugin["permissions"].get<std::vector<std::string>>();
            }
        }
        
        return metadata;
        
    } catch (const std::exception& e) {
        // Failed to parse metadata
        return std::nullopt;
    }
}

bool PluginSecurityVerifier::isBlacklisted(const std::string& fileHash) const {
    return std::find(policy_.blacklistedHashes.begin(), 
                    policy_.blacklistedHashes.end(), 
                    fileHash) != policy_.blacklistedHashes.end();
}

bool PluginSecurityVerifier::isWhitelisted(const std::string& fileHash) const {
    return std::find(policy_.whitelistedHashes.begin(),
                    policy_.whitelistedHashes.end(),
                    fileHash) != policy_.whitelistedHashes.end();
}

PluginTrustLevel PluginSecurityVerifier::getTrustLevel(const PluginMetadata& metadata) {
    // Check whitelist first
    if (isWhitelisted(metadata.signature.sha256Hash)) {
        return PluginTrustLevel::TRUSTED;
    }
    
    // Check blacklist
    if (isBlacklisted(metadata.signature.sha256Hash)) {
        return PluginTrustLevel::BLOCKED;
    }
    
    // Check if signature is verified
    if (!metadata.signature.verified) {
        return PluginTrustLevel::UNTRUSTED;
    }
    
    // Check if issuer is trusted
    bool trustedIssuer = false;
    for (const auto& trustedIssuerDN : policy_.trustedIssuers) {
        if (metadata.signature.issuer.find(trustedIssuerDN) != std::string::npos) {
            trustedIssuer = true;
            break;
        }
    }
    
    if (!trustedIssuer) {
        return PluginTrustLevel::UNTRUSTED;
    }
    
    return PluginTrustLevel::TRUSTED;
}

bool PluginSecurityVerifier::verifyPlugin(const std::string& pluginPath, std::string& errorMessage) {
    auto& auditor = PluginSecurityAuditor::instance();
    
    // Step 1: Check if file exists
    if (!std::filesystem::exists(pluginPath)) {
        errorMessage = "Plugin file does not exist: " + pluginPath;
        auditor.logEvent({
            PluginSecurityEvent::EventType::PLUGIN_LOAD_FAILED,
            pluginPath, "", errorMessage,
            static_cast<uint64_t>(std::time(nullptr)),
            "ERROR"
        });
        return false;
    }
    
    // Step 2: Calculate file hash
    std::string fileHash;
    if (policy_.verifyFileHash) {
        fileHash = calculateFileHash(pluginPath);
        if (fileHash.empty()) {
            errorMessage = "Failed to calculate file hash";
            auditor.logEvent({
                PluginSecurityEvent::EventType::PLUGIN_LOAD_FAILED,
                pluginPath, "", errorMessage,
                static_cast<uint64_t>(std::time(nullptr)),
                "ERROR"
            });
            return false;
        }
    }
    
    // Step 3: Check blacklist
    if (isBlacklisted(fileHash)) {
        errorMessage = "Plugin is on blacklist (hash: " + fileHash + ")";
        auditor.logEvent({
            PluginSecurityEvent::EventType::BLACKLISTED,
            pluginPath, fileHash, errorMessage,
            static_cast<uint64_t>(std::time(nullptr)),
            "CRITICAL"
        });
        return false;
    }
    
    // Step 4: Load and verify metadata
    auto metadata = loadMetadata(pluginPath);
    
    if (policy_.requireSignature && !metadata.has_value()) {
        if (!policy_.allowUnsigned) {
            errorMessage = "Plugin metadata not found (signature required)";
            auditor.logEvent({
                PluginSecurityEvent::EventType::SIGNATURE_VERIFICATION_FAILED,
                pluginPath, fileHash, errorMessage,
                static_cast<uint64_t>(std::time(nullptr)),
                "ERROR"
            });
            return false;
        }
    }
    
    // Step 5: Verify hash matches metadata
    if (metadata.has_value() && policy_.verifyFileHash) {
        if (!metadata->signature.sha256Hash.empty() && 
            metadata->signature.sha256Hash != fileHash) {
            errorMessage = "File hash mismatch! Expected: " + metadata->signature.sha256Hash + 
                          ", Got: " + fileHash;
            auditor.logEvent({
                PluginSecurityEvent::EventType::HASH_MISMATCH,
                pluginPath, fileHash, errorMessage,
                static_cast<uint64_t>(std::time(nullptr)),
                "CRITICAL"
            });
            return false;
        }
    }
    
    // Step 6: Verify digital signature (if present)
    if (metadata.has_value() && !metadata->signature.signature.empty()) {
        if (!verifySignature(pluginPath, metadata->signature)) {
            errorMessage = "Digital signature verification failed";
            auditor.logEvent({
                PluginSecurityEvent::EventType::SIGNATURE_VERIFICATION_FAILED,
                pluginPath, fileHash, errorMessage,
                static_cast<uint64_t>(std::time(nullptr)),
                "ERROR"
            });
            
            if (policy_.requireSignature) {
                return false;
            }
        } else {
            // Mark as verified in metadata
            const_cast<PluginMetadata&>(*metadata).signature.verified = true;
            
            auditor.logEvent({
                PluginSecurityEvent::EventType::SIGNATURE_VERIFIED,
                pluginPath, fileHash, "Signature verified successfully",
                static_cast<uint64_t>(std::time(nullptr)),
                "INFO"
            });
        }
    }
    
    // Step 7: Check trust level
    if (metadata.has_value()) {
        auto trustLevel = getTrustLevel(*metadata);
        
        if (trustLevel == PluginTrustLevel::BLOCKED) {
            errorMessage = "Plugin trust level is BLOCKED";
            return false;
        }
        
        if (trustLevel < policy_.minTrustLevel) {
            errorMessage = "Plugin trust level insufficient (required: TRUSTED, got: UNTRUSTED)";
            auditor.logEvent({
                PluginSecurityEvent::EventType::POLICY_VIOLATION,
                pluginPath, fileHash, errorMessage,
                static_cast<uint64_t>(std::time(nullptr)),
                "WARNING"
            });
            return false;
        }
    }
    
    // Step 8: Whitelist check (bypass other checks)
    if (isWhitelisted(fileHash)) {
        auditor.logEvent({
            PluginSecurityEvent::EventType::PLUGIN_LOADED,
            pluginPath, fileHash, "Plugin loaded (whitelisted)",
            static_cast<uint64_t>(std::time(nullptr)),
            "INFO"
        });
        return true;
    }
    
    // All checks passed
    auditor.logEvent({
        PluginSecurityEvent::EventType::PLUGIN_LOADED,
        pluginPath, fileHash, "Plugin loaded successfully",
        static_cast<uint64_t>(std::time(nullptr)),
        "INFO"
    });
    
    return true;
}

bool PluginSecurityVerifier::verifySignature(const std::string& filePath, 
                                             const PluginSignature& signature) {
    // Stub implementation - full implementation would use OpenSSL
    // to verify RSA/ECDSA signature against certificate
    
    if (signature.signature.empty() || signature.signingCertificate.empty()) {
        return false;
    }
    
    // TODO: Full implementation
    // 1. Load certificate
    // 2. Extract public key
    // 3. Verify signature of file hash
    // 4. Verify certificate chain
    // 5. Check certificate expiration
    // 6. Check CRL/OCSP if enabled
    
    return false; // Stub: always fail for now
}

void PluginSecurityVerifier::updatePolicy(const PluginSecurityPolicy& policy) {
    policy_ = policy;
}

// ============================================================================
// PluginSecurityAuditor Implementation
// ============================================================================

PluginSecurityAuditor& PluginSecurityAuditor::instance() {
    static PluginSecurityAuditor instance;
    return instance;
}

void PluginSecurityAuditor::logEvent(const PluginSecurityEvent& event) {
    events_.push_back(event);
    
    // Also log to system logger (spdlog integration)
    // TODO: Integration with existing audit_logger.h
}

std::vector<PluginSecurityEvent> PluginSecurityAuditor::getEventsForPlugin(
    const std::string& pluginPath) const {
    
    std::vector<PluginSecurityEvent> result;
    for (const auto& event : events_) {
        if (event.pluginPath == pluginPath) {
            result.push_back(event);
        }
    }
    return result;
}

void PluginSecurityAuditor::clearEvents() {
    events_.clear();
}

bool PluginSecurityAuditor::exportEvents(const std::string& outputPath) const {
    try {
        json j;
        j["events"] = json::array();
        
        for (const auto& event : events_) {
            json eventJson;
            eventJson["type"] = static_cast<int>(event.type);
            eventJson["pluginPath"] = event.pluginPath;
            eventJson["pluginHash"] = event.pluginHash;
            eventJson["message"] = event.message;
            eventJson["timestamp"] = event.timestamp;
            eventJson["severity"] = event.severity;
            
            j["events"].push_back(eventJson);
        }
        
        std::ofstream file(outputPath);
        file << j.dump(2);
        
        return true;
        
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace acceleration
} // namespace themis
