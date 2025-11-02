#pragma once

#include "pki_client.h"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace themis {
namespace utils {

/**
 * @brief PII Type enumeration
 */
enum class PIIType {
    EMAIL,
    PHONE,
    SSN,
    CREDIT_CARD,
    IBAN,
    IP_ADDRESS,
    URL,
    PERSON_NAME,      // For NER-based detection
    LOCATION,         // For NER-based detection
    ORGANIZATION,     // For NER-based detection
    UNKNOWN
};

/**
 * @brief Single PII detection finding
 */
struct PIIFinding {
    PIIType type;
    std::string value;          // Detected value (may be masked in some contexts)
    size_t start_offset;        // Start position in text
    size_t end_offset;          // End position in text
    double confidence;          // 0.0 to 1.0 (1.0 = certain)
    std::string pattern_name;   // Name of matched pattern/model
    std::string engine_name;    // Detection engine that found this ("regex", "ner", "embedding")
};

/**
 * @brief Plugin signature metadata for verification
 */
struct PluginSignature {
    std::string engine_type;        // Engine type name (e.g., "regex", "ner")
    std::string version;            // Plugin version (e.g., "1.0.0")
    std::string config_hash;        // SHA-256 hash of engine configuration
    std::string signature;          // PKI signature of config_hash
    std::string signature_id;       // PKI signature identifier
    std::string cert_serial;        // Certificate serial number
    std::string signed_at;          // ISO 8601 timestamp
    std::string signer;             // Entity that signed the plugin
    
    /**
     * @brief Verify signature against configuration
     * @param pki_client PKI client for verification
     * @param config Engine configuration to verify
     * @return true if signature is valid
     */
    bool verify(const VCCPKIClient& pki_client, const nlohmann::json& config) const;
    
    /**
     * @brief Compute hash of configuration
     * @param config Engine configuration
     * @return SHA-256 hash (hex string)
     */
    static std::string computeConfigHash(const nlohmann::json& config);
};

/**
 * @brief Abstract base class for PII detection engines
 * 
 * Plugin architecture for different detection methods with PKI-based security:
 * - All engines must be signed with valid PKI signature
 * - Configuration hash is verified against signature on load
 * - Unsigned or tampered engines are rejected
 * 
 * Supported Engine Types:
 * - RegexDetectionEngine: Pattern-based detection (fast, structured PII)
 * - NERDetectionEngine: Named Entity Recognition (names, locations, orgs)
 * - EmbeddingDetectionEngine: Semantic similarity (context-based detection)
 * 
 * Security Flow:
 * 1. Engine config loaded from YAML
 * 2. Config hash computed (SHA-256)
 * 3. Signature verified with PKI client
 * 4. Engine initialized only if signature valid
 * 
 * Each engine can be enabled/disabled via YAML configuration.
 */
class IPIIDetectionEngine {
public:
    virtual ~IPIIDetectionEngine() = default;
    
    /**
     * @brief Get engine name
     * @return Engine identifier (e.g., "regex", "ner", "embedding")
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Get engine version
     * @return Semantic version string (e.g., "1.0.0")
     */
    virtual std::string getVersion() const = 0;
    
    /**
     * @brief Check if engine is enabled
     * @return true if engine is active
     */
    virtual bool isEnabled() const = 0;
    
    /**
     * @brief Get engine's signature metadata
     * @return Plugin signature for PKI verification
     */
    virtual PluginSignature getSignature() const = 0;
    
    /**
     * @brief Initialize engine with signed configuration
     * 
     * SECURITY: This method should only be called after signature verification.
     * The factory is responsible for PKI validation before initialization.
     * 
     * @param config YAML configuration node for this engine
     * @return true on success, false on initialization failure
     */
    virtual bool initialize(const nlohmann::json& config) = 0;
    
    /**
     * @brief Reload engine configuration at runtime
     * 
     * @param config New YAML configuration node
     * @return true on success, false if reload failed (engine retains old config)
     */
    virtual bool reload(const nlohmann::json& config) = 0;
    
    /**
     * @brief Detect PII in plain text
     * 
     * @param text Input text to scan
     * @return Vector of PII findings
     */
    virtual std::vector<PIIFinding> detectInText(const std::string& text) const = 0;
    
    /**
     * @brief Classify field name for PII type
     * 
     * Uses engine-specific heuristics (e.g., regex uses field hints, NER uses embeddings).
     * 
     * @param field_name Field name to classify
     * @return PIIType if field suggests PII, PIIType::UNKNOWN otherwise
     */
    virtual PIIType classifyFieldName(const std::string& field_name) const = 0;
    
    /**
     * @brief Get recommended redaction mode for PII type
     * 
     * @param type PII type
     * @return "strict", "partial", or "none"
     */
    virtual std::string getRedactionRecommendation(PIIType type) const = 0;
    
    /**
     * @brief Get last error message
     * 
     * @return Error message from last failed operation
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * @brief Get engine-specific metadata (pattern count, model info, etc.)
     * 
     * @return JSON object with engine metadata
     */
    virtual nlohmann::json getMetadata() const = 0;
};

/**
 * @brief Factory for creating and verifying detection engines
 * 
 * Enforces PKI-based plugin security:
 * - Verifies plugin signatures before instantiation
 * - Rejects unsigned or tampered configurations
 * - Logs all verification attempts for audit trail
 */
class PIIDetectionEngineFactory {
public:
    /**
     * @brief Create detection engine with PKI verification
     * 
     * Security flow:
     * 1. Extract signature from config
     * 2. Compute config hash
     * 3. Verify signature with PKI client
     * 4. Create and initialize engine only if signature valid
     * 
     * @param engine_type Engine type ("regex", "ner", "embedding")
     * @param config Engine configuration with signature metadata
     * @param pki_client PKI client for signature verification
     * @param error_msg Output parameter for error details
     * @return Unique pointer to engine, or nullptr if verification/creation failed
     */
    static std::unique_ptr<IPIIDetectionEngine> createSigned(
        const std::string& engine_type,
        const nlohmann::json& config,
        const VCCPKIClient& pki_client,
        std::string& error_msg);
    
    /**
     * @brief Create detection engine WITHOUT signature verification
     * 
     * ⚠️  SECURITY WARNING: Only use for embedded/trusted engines!
     * This method bypasses PKI verification and should only be used for:
     * - Built-in engines (e.g., embedded regex patterns)
     * - Development/testing environments
     * - Fallback when PKI is unavailable
     * 
     * @param engine_type Engine type ("regex", "ner", "embedding")
     * @return Unique pointer to engine, or nullptr if type unknown
     */
    static std::unique_ptr<IPIIDetectionEngine> createUnsigned(
        const std::string& engine_type);
    
    /**
     * @brief Get list of available engine types
     * 
     * @return Vector of supported engine type names
     */
    static std::vector<std::string> getAvailableEngines();
    
    /**
     * @brief Verify plugin signature without creating engine
     * 
     * Useful for pre-validation of configuration files.
     * 
     * @param config Engine configuration with signature metadata
     * @param pki_client PKI client for signature verification
     * @param error_msg Output parameter for error details
     * @return true if signature is valid
     */
    static bool verifyPluginSignature(
        const nlohmann::json& config,
        const VCCPKIClient& pki_client,
        std::string& error_msg);
};

/**
 * @brief Utility functions for PII types
 */
class PIITypeUtils {
public:
    /**
     * @brief Convert PIIType enum to string
     * 
     * @param type PII type
     * @return String representation (e.g., "EMAIL", "PHONE")
     */
    static std::string toString(PIIType type);
    
    /**
     * @brief Convert string to PIIType enum
     * 
     * @param name Type name (case-insensitive)
     * @return PIIType or PIIType::UNKNOWN if invalid
     */
    static PIIType fromString(const std::string& name);
    
    /**
     * @brief Mask a PII value for logging
     * 
     * @param type PII type
     * @param value Original value
     * @param mode Redaction mode ("strict", "partial", "none")
     * @return Masked value (e.g., "***@example.com", "**** **** **** 1234")
     */
    static std::string maskValue(PIIType type, const std::string& value, const std::string& mode);
// Forward declaration for engine creation functions
std::unique_ptr<IPIIDetectionEngine> createRegexEngine();

};

} // namespace utils
} // namespace themis
