#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

namespace themis {
namespace security {

/**
 * HSM (Hardware Security Module) Provider Interface
 * 
 * Provides secure cryptographic operations using hardware-backed keys.
 * Supports PKCS#11 interface for HSM device communication.
 * 
 * Features:
 * - Hardware-backed key storage
 * - Secure signing operations (never exposes private key)
 * - Certificate management
 * - PIN/password protection
 * - Multi-slot support
 * 
 * Supported HSMs:
 * - Thales/SafeNet Luna HSM
 * - Utimaco CryptoServer
 * - AWS CloudHSM
 * - SoftHSM2 (software emulation for testing)
 * 
 * Example Usage:
 * ```cpp
 * HSMConfig config;
 * config.library_path = "/usr/lib/softhsm/libsofthsm2.so";
 * config.slot_id = 0;
 * config.pin = "1234";
 * 
 * auto hsm = std::make_unique<HSMProvider>(config);
 * if (hsm->initialize()) {
 *     auto signature = hsm->sign(data, "my-key-label");
 * }
 * ```
 */

struct HSMConfig {
    // PKCS#11 library path (e.g., /usr/lib/softhsm/libsofthsm2.so)
    std::string library_path;
    
    // HSM slot ID (default: 0)
    uint32_t slot_id = 0;
    
    // User PIN for authentication
    std::string pin;
    
    // Optional: Token label for filtering
    std::string token_label;
    
    // Signature algorithm (default: RSA-SHA256)
    std::string signature_algorithm = "RSA-SHA256";
    
    // Key label for operations (default: "themis-signing-key")
    std::string key_label = "themis-signing-key";
    
    // Enable verbose logging
    bool verbose = false;
};

struct HSMSignatureResult {
    bool success = false;
    std::string signature_b64;      // Base64-encoded signature
    std::string algorithm;           // Signature algorithm used
    std::string key_id;              // HSM key identifier
    std::string cert_serial;         // Certificate serial number (if available)
    std::string error_message;       // Error details on failure
    uint64_t timestamp_ms = 0;       // Unix timestamp in milliseconds
};

struct HSMKeyInfo {
    std::string label;               // Key label
    std::string id;                  // Key ID (hex)
    std::string algorithm;           // Algorithm (e.g., RSA-2048)
    bool can_sign = false;           // Key can be used for signing
    bool can_verify = false;         // Key can be used for verification
    bool extractable = false;        // Key can be extracted (should be false)
    uint32_t key_size = 0;          // Key size in bits
};

/**
 * HSM Provider Implementation
 * 
 * Wraps PKCS#11 API for HSM operations.
 * Handles session management, login, and cryptographic operations.
 */
class HSMProvider {
public:
    explicit HSMProvider(HSMConfig config);
    ~HSMProvider();

    // Disable copy (HSM sessions are not copyable)
    HSMProvider(const HSMProvider&) = delete;
    HSMProvider& operator=(const HSMProvider&) = delete;

    // Enable move
    HSMProvider(HSMProvider&&) noexcept;
    HSMProvider& operator=(HSMProvider&&) noexcept;

    /**
     * Initialize HSM connection and authenticate
     * @return true on success, false otherwise
     */
    bool initialize();

    /**
     * Finalize HSM session and cleanup
     */
    void finalize();

    /**
     * Sign data using HSM-backed private key
     * @param data: Data to sign (will be hashed internally)
     * @param key_label: Key label in HSM (optional, uses config default if empty)
     * @return Signature result with base64-encoded signature
     */
    HSMSignatureResult sign(const std::vector<uint8_t>& data, 
                            const std::string& key_label = "");

    /**
     * Sign a pre-computed hash using HSM-backed private key
     * @param hash: Pre-computed hash (e.g., SHA-256)
     * @param key_label: Key label in HSM
     * @return Signature result
     */
    HSMSignatureResult signHash(const std::vector<uint8_t>& hash,
                                const std::string& key_label = "");

    /**
     * Verify signature using HSM-backed public key
     * @param data: Original data
     * @param signature_b64: Base64-encoded signature
     * @param key_label: Key label in HSM
     * @return true if signature is valid, false otherwise
     */
    bool verify(const std::vector<uint8_t>& data,
                const std::string& signature_b64,
                const std::string& key_label = "");

    /**
     * List available keys in HSM
     * @return Vector of key information
     */
    std::vector<HSMKeyInfo> listKeys();

    /**
     * Generate new RSA key pair in HSM
     * @param label: Key label
     * @param key_size: Key size in bits (2048, 3072, 4096)
     * @param extractable: Whether key can be extracted (should be false)
     * @return true on success, false otherwise
     */
    bool generateKeyPair(const std::string& label, 
                         uint32_t key_size = 2048,
                         bool extractable = false);

    /**
     * Import certificate for existing key
     * @param key_label: Associated key label
     * @param cert_pem: Certificate in PEM format
     * @return true on success, false otherwise
     */
    bool importCertificate(const std::string& key_label,
                           const std::string& cert_pem);

    /**
     * Get certificate for key
     * @param key_label: Key label
     * @return Certificate in PEM format, or empty optional if not found
     */
    std::optional<std::string> getCertificate(const std::string& key_label);

    /**
     * Check if HSM is initialized and ready
     */
    bool isReady() const;

    /**
     * Get HSM token information
     * @return Token label, serial number, firmware version
     */
    std::string getTokenInfo() const;

    /**
     * Get last error message
     */
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    HSMConfig config_;
    bool initialized_ = false;
    std::string last_error_;
};

/**
 * HSM-Backed PKI Client
 * 
 * High-level wrapper that combines HSM operations with PKI workflows.
 * Compatible with existing VCCPKIClient interface.
 */
class HSMPKIClient {
public:
    explicit HSMPKIClient(HSMConfig config);
    ~HSMPKIClient();

    /**
     * Sign data with HSM and return PKI-compatible result
     */
    HSMSignatureResult sign(const std::vector<uint8_t>& data);

    /**
     * Verify signature
     */
    bool verify(const std::vector<uint8_t>& data, const std::string& signature_b64);

    /**
     * Get certificate serial number
     */
    std::optional<std::string> getCertSerial();

    /**
     * Check if HSM is ready
     */
    bool isReady() const;

private:
    std::unique_ptr<HSMProvider> hsm_;
};

} // namespace security
} // namespace themis
