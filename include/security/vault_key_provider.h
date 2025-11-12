#pragma once

#include "key_provider.h"
#include "security/signing_provider.h"
#include <curl/curl.h>
#include <chrono>
#include <memory>

namespace themis {

/**
 * @brief HashiCorp Vault integration for production key management
 * 
 * Features:
 * - Vault KV v2 secrets engine support
 * - Token authentication (extensible to AppRole/AWS/etc)
 * - Automatic key caching with TTL
 * - Thread-safe operations
 * - Automatic token refresh (if provided)
 * 
 * Vault Setup:
 * 1. Enable KV v2 secrets engine:
 *    vault secrets enable -version=2 -path=themis kv
 * 
 * 2. Store encryption key:
 *    vault kv put themis/keys/user_pii \
 *      key=$(openssl rand -base64 32) \
 *      algorithm="AES-256-GCM" \
 *      version=1
 * 
 * 3. Create policy:
 *    path "themis/data/keys/*" {
 *      capabilities = ["read", "list"]
 *    }
 *    path "themis/metadata/keys/*" {
 *      capabilities = ["read", "list"]
 *    }
 * 
 * Example Usage:
 * @code
 * auto provider = std::make_shared<VaultKeyProvider>(
 *     "http://localhost:8200",
 *     "s.abc123...",
 *     "themis"  // KV mount path
 * );
 * 
 * // Retrieve key (cached automatically)
 * auto key = provider->getKey("user_pii");
 * 
 * // Rotate key (creates new version in Vault)
 * provider->rotateKey("user_pii");
 * @endcode
 * 
 * Performance:
 * - Cache TTL: 1 hour (configurable)
 * - Cache capacity: 1000 keys
 * - Cold fetch: ~50-100ms (network latency)
 * - Cached fetch: <0.1ms
 * 
 * Error Handling:
 * - Network errors: KeyOperationException with retry hint
 * - 403 Forbidden: KeyOperationException (auth issue)
 * - 404 Not Found: KeyNotFoundException
 * - 5xx errors: KeyOperationException with transient flag
 */
class VaultKeyProvider : public KeyProvider, public SigningProvider {
public:
    /**
     * @brief Configuration for Vault connection
     */
    struct Config {
        std::string vault_addr;      // e.g., "http://localhost:8200"
        std::string vault_token;     // Authentication token
    std::string kv_mount_path;   // KV secrets engine mount (default: "themis")
    // Transit mount for signing (optional)
    std::string transit_mount;   // Transit mount path (default: "transit")
        std::string kv_version;      // "v1" or "v2" (default: "v2")
        int cache_ttl_seconds;       // Cache TTL (default: 3600)
        int cache_capacity;          // Max cached keys (default: 1000)
        int request_timeout_ms;      // HTTP timeout (default: 5000)
        bool verify_ssl;             // SSL verification (default: true)
    // Optional retry settings for transit calls
    int transit_max_retries;
    int transit_backoff_ms;
        
        Config()
            : kv_mount_path("themis")
            , kv_version("v2")
            , cache_ttl_seconds(3600)
            , cache_capacity(1000)
            , request_timeout_ms(5000)
            , verify_ssl(true)
            , transit_mount("transit")
            , transit_max_retries(3)
            , transit_backoff_ms(200)
        {}
    };
    
    /**
     * @brief Construct VaultKeyProvider with configuration
     * 
     * @param config Vault connection configuration
     * @throws KeyOperationException if libcurl initialization fails
     */
    explicit VaultKeyProvider(const Config& config);
    
    /**
     * @brief Convenience constructor with default settings
     * 
     * @param vault_addr Vault address (e.g., "http://localhost:8200")
     * @param vault_token Authentication token
     * @param kv_mount_path KV mount path (default: "themis")
     */
    VaultKeyProvider(
        const std::string& vault_addr,
        const std::string& vault_token,
        const std::string& kv_mount_path = "themis"
    );
    
    ~VaultKeyProvider() override;
    
    // KeyProvider interface implementation
    std::vector<uint8_t> getKey(const std::string& key_id) override;
    std::vector<uint8_t> getKey(const std::string& key_id, uint32_t version) override;
    uint32_t rotateKey(const std::string& key_id) override;
    std::vector<KeyMetadata> listKeys() override;
    KeyMetadata getKeyMetadata(const std::string& key_id, uint32_t version = 0) override;
    void deleteKey(const std::string& key_id, uint32_t version) override;
    bool hasKey(const std::string& key_id, uint32_t version = 0) override;
    uint32_t createKeyFromBytes(
        const std::string& key_id,
        const std::vector<uint8_t>& key_bytes,
        const KeyMetadata& metadata = KeyMetadata()) override;

    // SigningProvider interface: perform a sign operation via Vault Transit
    SigningResult sign(const std::string& key_id, const std::vector<uint8_t>& data) override;
    
    /**
     * @brief Clear all cached keys
     * 
     * Forces next getKey() to fetch from Vault.
     * Useful for testing or after token refresh.
     */
    void clearCache();
    
    /**
     * @brief Get cache statistics
     * 
     * @return {hit_rate, total_requests, cache_hits, cache_size}
     */
    struct CacheStats {
        double hit_rate;
        size_t total_requests;
        size_t cache_hits;
        size_t cache_size;
    };
    CacheStats getCacheStats() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
protected:
    // HTTP helpers - made virtual/protected so tests can override http behaviour
    virtual std::string httpGet(const std::string& path);
    virtual std::string httpPost(const std::string& path, const std::string& body);
    virtual std::string httpList(const std::string& path);
    
    // Vault API wrappers
    std::string readSecret(const std::string& key_id, uint32_t version = 0);
    std::string readSecretMetadata(const std::string& key_id);
    void writeSecret(const std::string& key_id, const std::string& key_b64, uint32_t version);
    std::vector<std::string> listSecrets();
    
    // Key parsing
    std::vector<uint8_t> parseKeyFromVaultResponse(const std::string& json_response);
    KeyMetadata parseMetadataFromVaultResponse(const std::string& json_response);
    
    // Cache key generation
    std::string makeCacheKey(const std::string& key_id, uint32_t version) const;

    // Testing: override HTTP behavior (url, method, body) -> response
    void setTestRequestOverride(std::function<std::string(const std::string&, const std::string&, const std::string&)> fn);
};

} // namespace themis
