#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include <set>
#include <mutex>
#include <nlohmann/json.hpp>

namespace themis::sharding {

/**
 * Signed Request Protocol
 * 
 * Provides defense-in-depth security layer on top of mTLS.
 * Requests are signed with the sender's private key and include:
 * - Timestamp for freshness validation
 * - Nonce for replay protection
 * - Certificate serial for identity verification
 * 
 * This protects against:
 * - Replay attacks
 * - Man-in-the-middle (additional layer beyond mTLS)
 * - Request tampering
 */

/**
 * Signed Request Structure
 */
struct SignedRequest {
    std::string shard_id;       // Sender shard ID
    std::string operation;      // HTTP method (GET, POST, PUT, DELETE)
    std::string path;           // Request path (e.g., URN or API endpoint)
    nlohmann::json body;        // Request body (empty for GET/DELETE)
    uint64_t timestamp_ms;      // Unix timestamp in milliseconds
    uint64_t nonce;             // Random nonce for uniqueness
    
    std::string signature_b64;  // RSA-SHA256 signature (base64 encoded)
    std::string cert_serial;    // Certificate serial number (hex)
    
    /**
     * Serialize to JSON for transmission
     */
    nlohmann::json toJSON() const;
    
    /**
     * Deserialize from JSON
     */
    static std::optional<SignedRequest> fromJSON(const nlohmann::json& j);
    
    /**
     * Get canonical string representation for signing
     * Format: shard_id|operation|path|body_json|timestamp_ms|nonce
     */
    std::string getCanonicalString() const;
};

/**
 * Signed Request Signer
 * 
 * Signs requests using the shard's private key
 */
class SignedRequestSigner {
public:
    /**
     * Configuration for request signing
     */
    struct Config {
        std::string shard_id;       // This shard's ID
        std::string cert_path;      // Certificate path (for serial extraction)
        std::string key_path;       // Private key path
        std::string key_passphrase; // Optional: key passphrase
    };
    
    /**
     * Construct signer with configuration
     */
    explicit SignedRequestSigner(const Config& config);
    
    /**
     * Sign a request
     * Adds timestamp, nonce, and signature to the request
     * @param request Request to sign (will be modified)
     * @return true if signing succeeded
     */
    bool sign(SignedRequest& request);
    
    /**
     * Create and sign a request
     */
    SignedRequest createSignedRequest(const std::string& operation,
                                     const std::string& path,
                                     const nlohmann::json& body = nlohmann::json{});

private:
    Config config_;
    std::string cert_serial_;
    
    /**
     * Generate cryptographically secure random nonce
     */
    uint64_t generateNonce() const;
    
    /**
     * Get current timestamp in milliseconds
     */
    uint64_t getCurrentTimestampMs() const;
    
    /**
     * Sign data with private key
     */
    std::optional<std::string> signData(const std::string& data);
};

/**
 * Signed Request Verifier
 * 
 * Verifies signed requests and prevents replay attacks
 */
class SignedRequestVerifier {
public:
    /**
     * Configuration for request verification
     */
    struct Config {
        std::string ca_cert_path;   // Root CA certificate path
        uint64_t max_time_skew_ms = 60000;  // Max timestamp deviation (60s default)
        size_t max_nonce_cache = 10000;     // Max nonces to track
        uint64_t nonce_expiry_ms = 300000;  // Nonce expiry time (5 min default)
    };
    
    /**
     * Construct verifier with configuration
     */
    explicit SignedRequestVerifier(const Config& config);
    
    /**
     * Verify a signed request
     * Checks:
     * 1. Timestamp freshness
     * 2. Nonce uniqueness (replay protection)
     * 3. Signature validity
     * 4. Certificate validity
     * 
     * @param request Signed request to verify
     * @param expected_shard_id Optional: expected sender shard ID
     * @return true if request is valid and not replayed
     */
    bool verify(const SignedRequest& request,
                const std::string& expected_shard_id = "");
    
    /**
     * Clear expired nonces from cache
     * Should be called periodically
     */
    void cleanupExpiredNonces();

private:
    Config config_;
    
    // Nonce tracking for replay protection
    struct NonceEntry {
        uint64_t nonce;
        uint64_t timestamp_ms;
    };
    std::set<uint64_t> seen_nonces_;
    mutable std::mutex nonce_mutex_;
    
    /**
     * Check if timestamp is within acceptable range
     */
    bool verifyTimestamp(uint64_t timestamp_ms) const;
    
    /**
     * Check if nonce has been seen before (replay detection)
     */
    bool verifyNonce(uint64_t nonce, uint64_t timestamp_ms);
    
    /**
     * Verify signature using certificate
     */
    bool verifySignature(const SignedRequest& request);
    
    /**
     * Get current timestamp in milliseconds
     */
    uint64_t getCurrentTimestampMs() const;
};

} // namespace themis::sharding
