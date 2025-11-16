#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace themis {

/**
 * @brief Status of an encryption key
 */
enum class KeyStatus {
    ACTIVE,      // Key is active and can be used for encryption/decryption
    ROTATING,    // Key rotation in progress (dual-write mode)
    DEPRECATED,  // Key can decrypt old data but not encrypt new data
    DELETED      // Key is deleted, no operations allowed
};

/**
 * @brief Metadata about an encryption key
 */
struct KeyMetadata {
    std::string key_id;      // Logical key identifier (e.g., "user_pii")
    uint32_t version;        // Key version for rotation (1, 2, 3, ...)
    std::string algorithm;   // Encryption algorithm (e.g., "AES-256-GCM")
    int64_t created_at_ms;   // Timestamp when key was created
    int64_t expires_at_ms;   // Expiry timestamp (0 = never expires)
    KeyStatus status;        // Current status of the key
    
    KeyMetadata() 
        : version(0)
        , created_at_ms(0)
        , expires_at_ms(0)
        , status(KeyStatus::ACTIVE) 
    {}
};

/**
 * @brief Exception thrown when a key is not found
 */
class KeyNotFoundException : public std::runtime_error {
public:
    explicit KeyNotFoundException(const std::string& key_id, uint32_t version)
        : std::runtime_error("Key not found: " + key_id + " v" + std::to_string(version))
        , key_id_(key_id)
        , version_(version)
    {}
    
    const std::string& getKeyId() const { return key_id_; }
    uint32_t getVersion() const { return version_; }
    
private:
    std::string key_id_;
    uint32_t version_;
};

/**
 * @brief Exception thrown when key operation is not allowed
 */
class KeyOperationException : public std::runtime_error {
public:
    explicit KeyOperationException(const std::string& message)
        : std::runtime_error(message)
        , http_code_(-1)
        , transient_(false)
    {}

    KeyOperationException(const std::string& message, int http_code, const std::string& vault_message, bool transient)
        : std::runtime_error(message)
        , http_code_(http_code)
        , vault_message_(vault_message)
        , transient_(transient)
    {}

    int httpCode() const { return http_code_; }
    const std::string& vaultMessage() const { return vault_message_; }
    bool transient() const { return transient_; }
private:
    int http_code_;
    std::string vault_message_;
    bool transient_;
};
/**
 * @brief Abstract interface for encryption key management
 * 
 * KeyProvider is responsible for:
 * - Retrieving encryption keys by ID and version
 * - Managing key rotation lifecycle
 * - Providing key metadata for auditing
 * 
 * Implementations:
 * - MockKeyProvider: In-memory provider for testing
 * - VaultKeyProvider: HashiCorp Vault integration
 * - KMSKeyProvider: Cloud KMS (AWS/Azure/GCP) integration
 * 
 * Thread Safety:
 * All implementations must be thread-safe.
 * 
 * Performance Considerations:
 * - Implement caching to avoid repeated external calls
 * - Use TTL-based cache eviction (recommended: 1 hour)
 * - Monitor cache hit rate via metrics
 * 
 * Example Usage:
 * @code
 * auto provider = std::make_shared<VaultKeyProvider>(vault_addr, token);
 * 
 * // Retrieve active key for encryption
 * auto key = provider->getKey("user_pii");
 * 
 * // Retrieve specific version for decryption
 * auto old_key = provider->getKey("user_pii", 2);
 * 
 * // Rotate to new version
 * provider->rotateKey("user_pii");
 * @endcode
 */
class KeyProvider {
public:
    virtual ~KeyProvider() = default;
    
    /**
     * @brief Retrieve an encryption key by ID (latest active version)
     * 
     * @param key_id Logical key identifier (e.g., "user_pii", "payment_info")
     * @return Raw key bytes (256 bits for AES-256)
     * @throws KeyNotFoundException if key does not exist
     * @throws KeyOperationException if key is not in ACTIVE or DEPRECATED status
     */
    virtual std::vector<uint8_t> getKey(const std::string& key_id) = 0;
    
    /**
     * @brief Retrieve a specific version of an encryption key
     * 
     * Used for decrypting old data that was encrypted with a previous key version.
     * 
     * @param key_id Logical key identifier
     * @param version Key version number (1, 2, 3, ...)
     * @return Raw key bytes (256 bits for AES-256)
     * @throws KeyNotFoundException if key version does not exist
     * @throws KeyOperationException if key is DELETED
     */
    virtual std::vector<uint8_t> getKey(const std::string& key_id, uint32_t version) = 0;
    
    /**
     * @brief Create a new version of a key (rotation)
     * 
     * Process:
     * 1. Generate new key version (current_max + 1)
     * 2. Mark new version as ACTIVE
     * 3. Mark previous version as DEPRECATED
     * 4. New encryptions use new version
     * 5. Old data still decryptable with deprecated version
     * 
     * @param key_id Key to rotate
     * @return New key version number
     * @throws KeyOperationException if rotation fails
     */
    virtual uint32_t rotateKey(const std::string& key_id) = 0;
    
    /**
     * @brief List all available keys with metadata
     * 
     * Used for:
     * - Auditing (which keys exist)
     * - Monitoring (key age, rotation schedule)
     * - Cleanup (identify deprecated keys for deletion)
     * 
     * @return Vector of key metadata (all versions)
     */
    virtual std::vector<KeyMetadata> listKeys() = 0;
    
    /**
     * @brief Get metadata for a specific key
     * 
     * @param key_id Key identifier
     * @param version Key version (0 = latest active)
     * @return Key metadata
     * @throws KeyNotFoundException if key does not exist
     */
    virtual KeyMetadata getKeyMetadata(const std::string& key_id, uint32_t version = 0) = 0;
    
    /**
     * @brief Mark a deprecated key for deletion
     * 
     * Preconditions:
     * - Key must be in DEPRECATED status
     * - No data encrypted with this version (verified externally)
     * 
     * @param key_id Key identifier
     * @param version Key version to delete
     * @throws KeyOperationException if key is still ACTIVE or data exists
     */
    virtual void deleteKey(const std::string& key_id, uint32_t version) = 0;
    
    /**
     * @brief Check if a key exists
     * 
     * @param key_id Key identifier
     * @param version Key version (0 = check if any version exists)
     * @return true if key exists, false otherwise
     */
    virtual bool hasKey(const std::string& key_id, uint32_t version = 0) = 0;
    
    /**
     * @brief Create a new key from raw bytes
     * 
     * Used for importing keys or creating derived keys.
     * 
     * @param key_id Key identifier
     * @param key_bytes Raw key material (must be 32 bytes for AES-256)
     * @param metadata Optional metadata (algorithm, created_at, etc.)
     * @return Key version number
     * @throws KeyOperationException if key creation fails
     */
    virtual uint32_t createKeyFromBytes(
        const std::string& key_id,
        const std::vector<uint8_t>& key_bytes,
        const KeyMetadata& metadata = KeyMetadata()) = 0;
};

/**
 * @brief Key cache for performance optimization
 * 
 * Caches recently used keys to avoid repeated calls to external key stores
 * (Vault, KMS, etc.) which can be slow (50-200ms per request).
 * 
 * Thread Safety: All methods are thread-safe
 * 
 * Eviction Policy:
 * - TTL-based: Keys expire after 1 hour
 * - LRU: When cache is full, evict least recently used
 * - Max size: 1000 keys (configurable)
 */
class KeyCache {
public:
    struct CacheEntry {
        std::vector<uint8_t> key;
        int64_t expires_at_ms;
        uint64_t access_count;
        int64_t last_access_ms;
    };
    
    /**
     * @brief Construct key cache
     * 
     * @param max_size Maximum number of keys to cache (default: 1000)
     * @param ttl_ms Time-to-live for cached keys in milliseconds (default: 1 hour)
     */
    explicit KeyCache(size_t max_size = 1000, int64_t ttl_ms = 3600000);
    
    /**
     * @brief Get a key from cache
     * 
     * @param key_id Key identifier
     * @param version Key version
     * @param out_key Output parameter for key bytes
     * @return true if key found in cache, false otherwise
     */
    bool get(const std::string& key_id, uint32_t version, std::vector<uint8_t>& out_key);
    
    /**
     * @brief Store a key in cache
     * 
     * @param key_id Key identifier
     * @param version Key version
     * @param key Key bytes to cache
     */
    void put(const std::string& key_id, uint32_t version, const std::vector<uint8_t>& key);
    
    /**
     * @brief Remove a key from cache
     * 
     * @param key_id Key identifier
     * @param version Key version (0 = all versions)
     */
    void evict(const std::string& key_id, uint32_t version = 0);
    
    /**
     * @brief Clear all cached keys
     */
    void clear();
    
    /**
     * @brief Get cache statistics
     * 
     * @return Cache hit rate (0.0 to 1.0)
     */
    double getHitRate() const;
    
    /**
     * @brief Get current cache size
     * 
     * @return Number of keys currently cached
     */
    size_t size() const;

private:
    std::map<std::string, CacheEntry> cache_;  // "key_id:version" -> Entry
    size_t max_size_;
    int64_t ttl_ms_;
    
    mutable std::mutex mutex_;
    uint64_t total_requests_;
    uint64_t cache_hits_;
    
    std::string makeCacheKey(const std::string& key_id, uint32_t version) const;
    void evictExpired();
    void evictLRU();
    int64_t getCurrentTimeMs() const;
};

}  // namespace themis
