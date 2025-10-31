#pragma once

#include "security/key_provider.h"
#include <map>
#include <mutex>
#include <random>

namespace themis {

/**
 * @brief In-memory key provider for testing and development
 * 
 * MockKeyProvider stores encryption keys in memory and provides
 * a simple implementation of the KeyProvider interface for testing
 * purposes. Keys are NOT persisted and will be lost on restart.
 * 
 * Features:
 * - Random 256-bit key generation
 * - Thread-safe operations
 * - Key versioning support
 * - In-memory storage only
 * 
 * Usage:
 * @code
 * auto provider = std::make_shared<MockKeyProvider>();
 * provider->createKey("user_pii", 1);
 * 
 * auto key = provider->getKey("user_pii");  // Returns v1
 * provider->rotateKey("user_pii");          // Creates v2
 * @endcode
 * 
 * NOT for production use - keys stored in process memory only!
 */
class MockKeyProvider : public KeyProvider {
public:
    MockKeyProvider();
    ~MockKeyProvider() override = default;
    
    /**
     * @brief Create a new key with random bytes
     * 
     * @param key_id Key identifier
     * @param version Key version
     * @throws KeyOperationException if key already exists
     */
    void createKey(const std::string& key_id, uint32_t version);
    
    /**
     * @brief Create a key with specific bytes (for testing)
     * 
     * @param key_id Key identifier
     * @param version Key version
     * @param key_bytes Exact key bytes (must be 32 bytes)
     * @throws std::invalid_argument if key_bytes.size() != 32
     */
    void createKeyWithBytes(const std::string& key_id, 
                           uint32_t version,
                           const std::vector<uint8_t>& key_bytes);
    
    // KeyProvider interface implementation
    std::vector<uint8_t> getKey(const std::string& key_id) override;
    
    std::vector<uint8_t> getKey(const std::string& key_id, uint32_t version) override;
    
    uint32_t rotateKey(const std::string& key_id) override;
    
    std::vector<KeyMetadata> listKeys() override;
    
    KeyMetadata getKeyMetadata(const std::string& key_id, uint32_t version = 0) override;
    
    void deleteKey(const std::string& key_id, uint32_t version) override;
    
    /**
     * @brief Get the latest version number for a key
     * 
     * @param key_id Key identifier
     * @return Latest version number (0 if key doesn't exist)
     */
    uint32_t getLatestVersion(const std::string& key_id) const;
    
    /**
     * @brief Clear all keys (for testing)
     */
    void clear();

private:
    struct KeyEntry {
        std::vector<uint8_t> key;
        KeyMetadata metadata;
    };
    
    // key_id -> (version -> KeyEntry)
    std::map<std::string, std::map<uint32_t, KeyEntry>> keys_;
    
    mutable std::mutex mutex_;
    std::mt19937 rng_;
    
    std::vector<uint8_t> generateRandomKey();
    std::string makeKeyPath(const std::string& key_id, uint32_t version) const;
    int64_t getCurrentTimeMs() const;
};

}  // namespace themis
