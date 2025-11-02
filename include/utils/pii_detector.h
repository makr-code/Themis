#pragma once

#include "pii_detection_engine.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <nlohmann/json.hpp>

namespace themis {
namespace utils {

/**
 * @brief PII (Personally Identifiable Information) Detection Orchestrator
 * 
 * Plugin-based, runtime-reloadable PII detection system.
 * Manages multiple detection engines (regex, NER, embeddings) via YAML configuration.
 * 
 * Architecture:
 * - Orchestrator pattern: PIIDetector coordinates multiple IPIIDetectionEngine instances
 * - Plugin system: Engines are loaded/unloaded based on YAML config
 * - Fallback: Safe defaults when YAML loading fails
 * - Thread-safe: All operations protected for concurrent access
 * 
 * Supported Detection Engines:
 * 1. **RegexDetectionEngine** (default, always available)
 *    - Pattern-based detection for structured PII
 *    - Fast, low overhead
 *    - Email, phone, SSN, credit cards, IBAN, IP, URL
 * 
 * 2. **NERDetectionEngine** (optional, requires MITIE/ONNX)
 *    - Named Entity Recognition
 *    - Person names, locations, organizations
 *    - Higher overhead, more accurate for unstructured text
 * 
 * 3. **EmbeddingDetectionEngine** (optional, requires fastText/word2vec)
 *    - Semantic similarity detection
 *    - Context-based PII (e.g., "salary: 50000" as sensitive)
 *    - Highest overhead, best for complex scenarios
 * 
 * YAML Configuration:
 * @code{.yaml}
 * detection_engines:
 *   - type: "regex"
 *     enabled: true
 *     patterns: [...]
 *   - type: "ner"
 *     enabled: false
 *     model_path: "models/pii_ner.dat"
 * @endcode
 * 
 * Example Usage:
 * @code
 * PIIDetector detector("config/pii_patterns.yaml");
 * std::string text = "Contact alice@example.com or Max Mustermann";
 * auto findings = detector.detectInText(text);
 * for (const auto& f : findings) {
 *     std::cout << "Found " << PIITypeUtils::toString(f.type) 
 *               << " via " << f.engine_name << "\n";
 * }
 * // Runtime reload
 * detector.reload();
 * @endcode
 */

class PIIDetector {
public:
    /**
     * @brief Constructor with optional YAML config path and PKI client
     * 
     * Loads detection engines from YAML file with PKI signature verification.
     * On verification failure, falls back to unsigned embedded RegexDetectionEngine.
     * 
     * @param config_path Path to pii_patterns.yaml (default: "config/pii_patterns.yaml")
     * @param pki_client Optional PKI client for signature verification (default: nullptr = skip verification)
     */
    explicit PIIDetector(
        const std::string& config_path = "config/pii_patterns.yaml",
        std::shared_ptr<VCCPKIClient> pki_client = nullptr);
    
    /**
     * @brief Reload all engines from YAML file with PKI verification
     * 
     * Thread-safe reload with signature validation. On failure, retains existing engines.
     * 
     * @param config_path Optional new config path (default: use constructor path)
     * @return true if reload succeeded, false on validation/verification errors
     */
    bool reload(const std::string& config_path = "");
    
    /**
     * @brief Set PKI client for plugin verification
     * 
     * @param pki_client PKI client instance (nullptr to disable verification)
     */
    void setPKIClient(std::shared_ptr<VCCPKIClient> pki_client);
    
    /**
     * @brief Check if PKI verification is enabled
     * 
     * @return true if PKI client is configured
     */
    bool isPKIVerificationEnabled() const;
    
    /**
     * @brief Get last reload error message
     * 
     * @return Error message from last failed reload, or empty string if last reload succeeded
     */
    std::string getLastError() const;
    
    /**
     * @brief Detect PII patterns in plain text
     * 
     * Runs all enabled detection engines and merges results.
     * 
     * @param text Input text to scan
     * @return Vector of PII findings sorted by start_offset, deduplicated
     */
    std::vector<PIIFinding> detectInText(const std::string& text) const;
    
    /**
     * @brief Detect PII in JSON object (field names and values)
     * 
     * Recursively scans JSON structure for PII in both keys and values.
     * 
     * @param json_obj Input JSON object
     * @return Map of JSON path -> PII findings
     *         Example: {"user.email" -> [EMAIL finding], "payment.card" -> [CREDIT_CARD finding]}
     */
    std::unordered_map<std::string, std::vector<PIIFinding>> detectInJson(
        const nlohmann::json& json_obj) const;
    
    /**
     * @brief Check if a specific field name suggests PII
     * 
     * Queries all enabled engines for field name classification.
     * 
     * @param field_name Field name to check
     * @return PIIType if field name suggests PII (highest confidence engine), PIIType::UNKNOWN otherwise
     */
    PIIType classifyFieldName(const std::string& field_name) const;
    
    /**
     * @brief Get recommended redaction level for PII type
     * 
     * @param type PII type
     * @return "strict" (full redaction), "partial" (show last 4 digits), or "none"
     */
    std::string getRedactionRecommendation(PIIType type) const;
    
    /**
     * @brief Mask a PII value for logging
     * 
     * @param type PII type
     * @param value Original value
     * @return Masked value (e.g., "***@example.com", "**** **** **** 1234")
     */
    std::string maskValue(PIIType type, const std::string& value) const;
    
    /**
     * @brief Get list of enabled engines
     * 
     * @return Vector of enabled engine names (e.g., ["regex", "ner"])
     */
    std::vector<std::string> getEnabledEngines() const;
    
    /**
     * @brief Get metadata for all engines
     * 
     * @return JSON object with per-engine metadata (pattern counts, model info, etc.)
     */
    nlohmann::json getEngineMetadata() const;

private:
    // Configuration
    std::string config_path_;
    mutable std::string last_error_;
    
    // PKI client for plugin verification
    std::shared_ptr<VCCPKIClient> pki_client_;
    
    // Thread safety for reload operations
    mutable std::mutex mutex_;
    
    // Active detection engines (protected by mutex_)
    std::vector<std::unique_ptr<IPIIDetectionEngine>> engines_;
    
    // Default redaction mode
    std::string default_redaction_mode_;
    
    // Initialization and loading
    bool loadFromYaml(const std::string& path);
    void initializeDefaultEngine();
    bool verifyAndLoadEngine(const nlohmann::json& engine_config);
    
    // Helper: Recursive JSON scanning
    void scanJsonRecursive(
        const nlohmann::json& obj,
        const std::string& path,
        std::unordered_map<std::string, std::vector<PIIFinding>>& findings) const;
    
    // Helper: Deduplicate overlapping findings
    std::vector<PIIFinding> deduplicateFindings(std::vector<PIIFinding> findings) const;
};

} // namespace utils
} // namespace themis
