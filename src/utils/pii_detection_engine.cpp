#include "utils/pii_detection_engine.h"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace themis {
namespace utils {

// Forward declaration of factory implemented in regex_detection_engine.cpp
std::unique_ptr<IPIIDetectionEngine> createRegexEngine();

// ============================================================================
// PluginSignature Implementation
// ============================================================================

std::string PluginSignature::computeConfigHash(const nlohmann::json& config) {
    // Remove signature block if present
    auto config_copy = config;
    if (config_copy.contains("signature")) {
        config_copy.erase("signature");
    }
    
    // Normalize to deterministic JSON (sorted keys, no whitespace)
    std::string normalized = config_copy.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
    
    // Compute SHA-256
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(normalized.c_str()), 
           normalized.length(), hash);
    
    // Convert to hex string
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') 
            << static_cast<int>(hash[i]);
    }
    
    return oss.str();
}

bool PluginSignature::verify(const VCCPKIClient& pki_client, 
                              const nlohmann::json& config) const {
    // Compute hash of configuration
    std::string computed_hash = computeConfigHash(config);
    
    // Verify hash matches declared hash
    if (computed_hash != config_hash) {
        spdlog::error("PluginSignature: Config hash mismatch. Expected: {}, Computed: {}", 
                      config_hash, computed_hash);
        return false;
    }
    
    // Verify PKI signature
    // Convert hex string to bytes
    std::vector<uint8_t> hash_bytes;
    hash_bytes.reserve(computed_hash.length() / 2);
    for (size_t i = 0; i < computed_hash.length(); i += 2) {
        std::string byte_str = computed_hash.substr(i, 2);
        hash_bytes.push_back(static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16)));
    }
    
    // Create SignatureResult from PluginSignature fields
    SignatureResult sig_result;
    sig_result.ok = true;
    sig_result.signature_id = signature_id;
    sig_result.signature_b64 = signature;
    sig_result.cert_serial = cert_serial;
    
    if (!pki_client.verifyHash(hash_bytes, sig_result)) {
        spdlog::error("PluginSignature: PKI signature verification failed for signature_id: {}", 
                      signature_id);
        return false;
    }
    
    spdlog::info("PluginSignature: Verified signature '{}' signed by '{}' at {}", 
                 signature_id, signer, signed_at);
    
    return true;
}

// ============================================================================
// PIITypeUtils Implementation
// ============================================================================

std::string PIITypeUtils::toString(PIIType type) {
    switch (type) {
        case PIIType::EMAIL: return "EMAIL";
        case PIIType::PHONE: return "PHONE";
        case PIIType::SSN: return "SSN";
        case PIIType::CREDIT_CARD: return "CREDIT_CARD";
        case PIIType::IBAN: return "IBAN";
        case PIIType::IP_ADDRESS: return "IP_ADDRESS";
        case PIIType::URL: return "URL";
        case PIIType::PERSON_NAME: return "PERSON_NAME";
        case PIIType::LOCATION: return "LOCATION";
        case PIIType::ORGANIZATION: return "ORGANIZATION";
        case PIIType::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

PIIType PIITypeUtils::fromString(const std::string& name) {
    if (name == "EMAIL") return PIIType::EMAIL;
    if (name == "PHONE") return PIIType::PHONE;
    if (name == "SSN") return PIIType::SSN;
    if (name == "CREDIT_CARD") return PIIType::CREDIT_CARD;
    if (name == "IBAN") return PIIType::IBAN;
    if (name == "IP_ADDRESS") return PIIType::IP_ADDRESS;
    if (name == "URL") return PIIType::URL;
    if (name == "PERSON_NAME") return PIIType::PERSON_NAME;
    if (name == "LOCATION") return PIIType::LOCATION;
    if (name == "ORGANIZATION") return PIIType::ORGANIZATION;
    return PIIType::UNKNOWN;
}

std::string PIITypeUtils::maskValue(PIIType type, const std::string& value, 
                                     const std::string& mode) {
    if (value.empty()) return value;
    
    if (mode == "strict") {
        // For display, still apply partial formatting for some types
        if (type == PIIType::SSN) {
            // ***-**-1234 style
            std::string out;
            int total_digits = 0;
            for (char c : value) if (std::isdigit(static_cast<unsigned char>(c))) ++total_digits;
            int visible_tail = std::min(4, total_digits);
            int to_mask = total_digits - visible_tail;
            for (char c : value) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    if (to_mask > 0) { out.push_back('*'); --to_mask; }
                    else { out.push_back(c); }
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }
        if (type == PIIType::CREDIT_CARD) {
            // **** **** **** 1234 style
            std::string out;
            int total_digits = 0;
            for (char c : value) if (std::isdigit(static_cast<unsigned char>(c))) ++total_digits;
            int visible_tail = std::min(4, total_digits);
            int to_mask = total_digits - visible_tail;
            for (char c : value) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    if (to_mask > 0) { out.push_back('*'); --to_mask; }
                    else { out.push_back(c); }
                } else if (c == '-' || c == ' ') {
                    out.push_back(' '); // normalize separator to space
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }
        // Full redaction
        return std::string(value.length(), '*');
    } else if (mode == "partial") {
        // Type-specific partial masking
        switch (type) {
            case PIIType::EMAIL: {
                size_t at_pos = value.find('@');
                if (at_pos != std::string::npos && at_pos > 0) {
                    return value.substr(0, 1) + "***" + value.substr(at_pos);
                }
                return std::string(value.length(), '*');
            }
            
            case PIIType::PHONE:
            case PIIType::CREDIT_CARD: {
                // Preserve separators, mask digits except last 4
                // First, count digits
                int total_digits = 0;
                for (char c : value) if (std::isdigit(static_cast<unsigned char>(c))) ++total_digits;
                int visible_tail = std::min(4, total_digits);
                int to_mask = total_digits - visible_tail;
                std::string out;
                out.reserve(value.size());
                for (char c : value) {
                    if (std::isdigit(static_cast<unsigned char>(c))) {
                        if (to_mask > 0) {
                            out.push_back('*');
                            --to_mask;
                        } else {
                            out.push_back(c);
                        }
                    } else {
                        out.push_back(c);
                    }
                }
                return out;
            }
            
            case PIIType::IBAN: {
                // Preserve country code (first 2) and last 4 chars
                if (value.length() > 6) {
                    std::string prefix = value.substr(0, 2);
                    std::string suffix = value.substr(value.length() - 4);
                    return prefix + std::string(value.length() - 6, '*') + suffix;
                }
                return std::string(value.length(), '*');
            }
            
            case PIIType::IP_ADDRESS: {
                size_t first_dot = value.find('.');
                if (first_dot != std::string::npos) {
                    return value.substr(0, first_dot) + ".*.*.*";
                }
                return std::string(value.length(), '*');
            }
            
            case PIIType::PERSON_NAME: {
                // Show first letter of first name: "M. Mustermann"
                size_t space_pos = value.find(' ');
                if (space_pos != std::string::npos && space_pos > 0) {
                    return value.substr(0, 1) + ". " + value.substr(space_pos + 1);
                }
                return value.substr(0, 1) + std::string(value.length() - 1, '*');
            }
            
            default:
                return std::string(value.length(), '*');
        }
    }
    
    // mode == "none" or unknown
    return value;
}

// ============================================================================
// PIIDetectionEngineFactory Implementation
// ============================================================================

std::unique_ptr<IPIIDetectionEngine> PIIDetectionEngineFactory::createSigned(
    const std::string& engine_type,
    const nlohmann::json& config,
    const VCCPKIClient& pki_client,
    std::string& error_msg) {
    
    // Extract signature from config
    if (!config.contains("signature")) {
        error_msg = "No signature block found in engine configuration";
        spdlog::error("PIIDetectionEngineFactory: {}", error_msg);
        return nullptr;
    }
    
    auto sig_node = config["signature"];
    PluginSignature signature;
    
    try {
        signature.engine_type = engine_type;
        signature.version = config.value("version", "unknown");
        signature.config_hash = sig_node.value("config_hash", "");
        signature.signature = sig_node.value("signature", "");
        signature.signature_id = sig_node.value("signature_id", "");
        signature.cert_serial = sig_node.value("cert_serial", "");
        signature.signed_at = sig_node.value("signed_at", "");
        signature.signer = sig_node.value("signer", "");
    } catch (const std::exception& e) {
        error_msg = std::string("Failed to parse signature metadata: ") + e.what();
        spdlog::error("PIIDetectionEngineFactory: {}", error_msg);
        return nullptr;
    }
    
    // Verify signature
    if (!signature.verify(pki_client, config)) {
        error_msg = "PKI signature verification failed for engine '" + engine_type + "'";
        spdlog::error("PIIDetectionEngineFactory: {}", error_msg);
        return nullptr;
    }
    
    spdlog::info("PIIDetectionEngineFactory: PKI signature verified for '{}' v{} (signed by: {})",
                 engine_type, signature.version, signature.signer);
    
    // Create engine (signature is valid)
    auto engine = createUnsigned(engine_type);
    if (!engine) {
        error_msg = "Unknown engine type: " + engine_type;
        return nullptr;
    }
    
    // Initialize engine with verified config
    if (!engine->initialize(config)) {
        error_msg = "Engine initialization failed: " + engine->getLastError();
        spdlog::error("PIIDetectionEngineFactory: {}", error_msg);
        return nullptr;
    }
    
    spdlog::info("PIIDetectionEngineFactory: Engine '{}' loaded successfully", engine_type);
    return engine;
}

std::unique_ptr<IPIIDetectionEngine> PIIDetectionEngineFactory::createUnsigned(
    const std::string& engine_type) {
    
    if (engine_type == "regex") {
        return createRegexEngine();
    }
    
    // Future engines:
    // if (engine_type == "ner") return std::make_unique<NERDetectionEngine>();
    // if (engine_type == "embedding") return std::make_unique<EmbeddingDetectionEngine>();
    
    spdlog::warn("PIIDetectionEngineFactory: Unknown engine type '{}'", engine_type);
    return nullptr;
}

std::vector<std::string> PIIDetectionEngineFactory::getAvailableEngines() {
    std::vector<std::string> engines;
    engines.push_back("regex");  // Always available
    
    // Conditionally compiled engines:
    #ifdef THEMIS_ENABLE_NER
    engines.push_back("ner");
    #endif
    
    #ifdef THEMIS_ENABLE_EMBEDDING
    engines.push_back("embedding");
    #endif
    
    return engines;
}

bool PIIDetectionEngineFactory::verifyPluginSignature(
    const nlohmann::json& config,
    const VCCPKIClient& pki_client,
    std::string& error_msg) {
    
    if (!config.contains("signature")) {
        error_msg = "No signature block found";
        return false;
    }
    
    auto sig_node = config["signature"];
    PluginSignature signature;
    
    try {
        signature.config_hash = sig_node.value("config_hash", "");
        signature.signature = sig_node.value("signature", "");
        signature.signature_id = sig_node.value("signature_id", "");
        signature.signer = sig_node.value("signer", "");
    } catch (const std::exception& e) {
        error_msg = std::string("Failed to parse signature: ") + e.what();
        return false;
    }
    
    if (!signature.verify(pki_client, config)) {
        error_msg = "Signature verification failed";
        return false;
    }
    
    return true;
}

} // namespace utils
} // namespace themis
