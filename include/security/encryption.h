#pragma once

#include "security/key_provider.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace themis {

/**
 * @brief Exception thrown when encryption fails
 */
class EncryptionException : public std::runtime_error {
public:
    explicit EncryptionException(const std::string& message)
        : std::runtime_error("Encryption failed: " + message)
    {}
};

/**
 * @brief Exception thrown when decryption fails
 */
class DecryptionException : public std::runtime_error {
public:
    explicit DecryptionException(const std::string& message)
        : std::runtime_error("Decryption failed: " + message)
    {}
};

/**
 * @brief Encrypted data blob with metadata
 * 
 * Structure:
 * - key_id: Logical key identifier ("user_pii", "payment_info")
 * - key_version: Version of the key used for encryption
 * - iv: Initialization Vector (12 bytes for GCM)
 * - ciphertext: Encrypted payload
 * - tag: Authentication tag (16 bytes for GCM)
 * 
 * Serialization Format (Base64):
 * {key_id}:{version}:{base64(iv)}:{base64(ciphertext)}:{base64(tag)}
 * 
 * Example:
 * "user_pii:2:YWJjZGVmZ2hpams=:SGVsbG8gV29ybGQ=:MTIzNDU2Nzg5MEFCQ0RFRg=="
 */
struct EncryptedBlob {
    std::string key_id;
    uint32_t key_version;
    std::vector<uint8_t> iv;          // 12 bytes (AES-GCM standard)
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;         // 16 bytes (AES-GCM authentication tag)
    
    EncryptedBlob() : key_version(0) {}
    
    /**
     * @brief Serialize to base64 string for storage
     * 
     * @return Base64-encoded string representation
     */
    std::string toBase64() const;
    
    /**
     * @brief Deserialize from base64 string
     * 
     * @param b64 Base64-encoded string
     * @return Parsed EncryptedBlob
     * @throws std::runtime_error if format is invalid
     */
    static EncryptedBlob fromBase64(const std::string& b64);
    
    /**
     * @brief Serialize to JSON object
     * 
     * @return JSON representation
     */
    nlohmann::json toJson() const;
    
    /**
     * @brief Deserialize from JSON object
     * 
     * @param j JSON object
     * @return Parsed EncryptedBlob
     */
    static EncryptedBlob fromJson(const nlohmann::json& j);
};

/**
 * @brief Field-level encryption using AES-256-GCM
 * 
 * This class implements authenticated encryption using AES-256 in GCM mode.
 * 
 * Features:
 * - Confidentiality: AES-256 encryption
 * - Integrity: GCM authentication tag prevents tampering
 * - Freshness: Random IV per encryption prevents replay attacks
 * - Key Versioning: Supports key rotation with backward compatibility
 * 
 * Security Properties:
 * - Algorithm: AES-256-GCM (NIST SP 800-38D)
 * - Key Size: 256 bits (32 bytes)
 * - IV Size: 96 bits (12 bytes) - standard for GCM
 * - Tag Size: 128 bits (16 bytes)
 * - Random IV: Generated per encryption using /dev/urandom
 * 
 * Performance:
 * - Encryption: ~0.5ms for 1KB plaintext
 * - Decryption: ~0.5ms for 1KB ciphertext
 * - Key Lookup: ~1ms (cached) / ~50ms (external KMS)
 * 
 * Thread Safety:
 * - All methods are thread-safe
 * - Uses OpenSSL's thread-safe EVP interface
 * 
 * Example Usage:
 * @code
 * auto key_provider = std::make_shared<VaultKeyProvider>(...);
 * FieldEncryption enc(key_provider);
 * 
 * // Encrypt
 * std::string plaintext = "alice@example.com";
 * auto blob = enc.encrypt(plaintext, "user_pii");
 * 
 * // Store
 * std::string stored = blob.toBase64();
 * db->put("email", stored);
 * 
 * // Retrieve
 * auto retrieved_blob = EncryptedBlob::fromBase64(db->get("email"));
 * 
 * // Decrypt
 * std::string decrypted = enc.decrypt(retrieved_blob);
 * assert(decrypted == plaintext);
 * @endcode
 */
class FieldEncryption {
public:
    /**
     * @brief Construct field encryption engine
     * 
     * @param key_provider Key management provider
     * @throws std::invalid_argument if key_provider is null
     */
    explicit FieldEncryption(std::shared_ptr<KeyProvider> key_provider);
    
    ~FieldEncryption();
    
    /**
     * @brief Encrypt a string using AES-256-GCM
     * 
     * Process:
     * 1. Retrieve encryption key from KeyProvider (latest version)
     * 2. Generate random 12-byte IV
     * 3. Encrypt plaintext using AES-256-GCM
     * 4. Produce ciphertext + 16-byte authentication tag
     * 5. Return EncryptedBlob with metadata
     * 
     * @param plaintext Data to encrypt
     * @param key_id Logical key identifier
     * @return Encrypted blob with metadata
     * @throws EncryptionException if encryption fails
     * @throws KeyNotFoundException if key does not exist
     */
    EncryptedBlob encrypt(const std::string& plaintext, const std::string& key_id);
    
    /**
     * @brief Encrypt binary data using AES-256-GCM
     * 
     * @param plaintext Binary data to encrypt
     * @param key_id Logical key identifier
     * @return Encrypted blob with metadata
     * @throws EncryptionException if encryption fails
     * @throws KeyNotFoundException if key does not exist
     */
    EncryptedBlob encrypt(const std::vector<uint8_t>& plaintext, const std::string& key_id);
    
    /**
     * @brief Decrypt an encrypted blob to string
     * 
     * Process:
     * 1. Retrieve decryption key from KeyProvider (using blob's key_version)
     * 2. Initialize AES-256-GCM with IV from blob
     * 3. Verify authentication tag (prevents tampering)
     * 4. Decrypt ciphertext to plaintext
     * 5. Return plaintext string
     * 
     * @param blob Encrypted blob to decrypt
     * @return Decrypted plaintext
     * @throws DecryptionException if decryption fails or authentication fails
     * @throws KeyNotFoundException if key version does not exist
     */
    std::string decryptToString(const EncryptedBlob& blob);
    
    /**
     * @brief Decrypt an encrypted blob to binary data
     * 
     * @param blob Encrypted blob to decrypt
     * @return Decrypted binary data
     * @throws DecryptionException if decryption fails or authentication fails
     * @throws KeyNotFoundException if key version does not exist
     */
    std::vector<uint8_t> decryptToBytes(const EncryptedBlob& blob);
    
    /**
     * @brief Encrypt with a specific key (for batch operations)
     * 
     * Optimization for bulk encryption - reuses key instead of fetching
     * from KeyProvider for each operation.
     * 
     * @param plaintext Data to encrypt
     * @param key_id Key identifier (for metadata)
     * @param key_version Key version (for metadata)
     * @param key Raw key bytes (256 bits)
     * @return Encrypted blob
     * @throws EncryptionException if encryption fails
     */
    EncryptedBlob encryptWithKey(const std::string& plaintext,
                                  const std::string& key_id,
                                  uint32_t key_version,
                                  const std::vector<uint8_t>& key);
    
    /**
     * @brief Decrypt with a specific key (for batch operations)
     * 
     * @param blob Encrypted blob
     * @param key Raw key bytes (256 bits)
     * @return Decrypted plaintext
     * @throws DecryptionException if decryption fails
     */
    std::string decryptWithKey(const EncryptedBlob& blob,
                                const std::vector<uint8_t>& key);

    /**
     * @brief Batch encrypt multiple entity payloads using a per-entity derived key.
     *
     * Each item in `items` is a pair (entity_salt, plaintext). For each entity,
     * the implementation will fetch the base key once and derive a per-entity key
     * using HKDF(entity_salt) and then encrypt the plaintext. The operation is
     * parallelized using TBB.
     *
     * @param items Vector of (entity_salt, plaintext) pairs
     * @param key_id Logical key identifier
     * @return Vector of EncryptedBlob results in the same order as input
     */
    std::vector<EncryptedBlob> encryptEntityBatch(const std::vector<std::pair<std::string,std::string>>& items,
                                                  const std::string& key_id);
    
    /**
     * @brief Decrypt an encrypted blob to string (alias for decryptToString)
     * 
     * @param blob Encrypted blob to decrypt
     * @return Decrypted plaintext
     * @throws DecryptionException if decryption fails or authentication fails
     * @throws KeyNotFoundException if key version does not exist
     */
    std::string decrypt(const EncryptedBlob& blob) { 
        return decryptToString(blob); 
    }
    
    /**
     * @brief Get the underlying key provider
     * 
     * @return Shared pointer to key provider
     */
    std::shared_ptr<KeyProvider> getKeyProvider() const { 
        return key_provider_; 
    }

private:
    std::shared_ptr<KeyProvider> key_provider_;
    
    // Internal helpers
    std::vector<uint8_t> generateIV() const;
    
    EncryptedBlob encryptInternal(const std::vector<uint8_t>& plaintext,
                                   const std::string& key_id,
                                   uint32_t key_version,
                                   const std::vector<uint8_t>& key);
    
    std::vector<uint8_t> decryptInternal(const EncryptedBlob& blob,
                                          const std::vector<uint8_t>& key);
};

/**
 * @brief Template wrapper for transparent field encryption
 * 
 * EncryptedField<T> provides a transparent interface for storing encrypted
 * values. The encryption/decryption happens automatically on assignment
 * and access.
 * 
 * Supported Types:
 * - std::string
 * - int64_t
 * - double
 * 
 * Usage:
 * @code
 * struct User {
 *     std::string id;
 *     EncryptedField<std::string> email;
 *     EncryptedField<std::string> phone;
 * };
 * 
 * User user;
 * user.email = "alice@example.com";  // Automatically encrypted
 * std::string plain = user.email.decrypt();  // Decrypt on demand
 * @endcode
 * 
 * Serialization:
 * @code
 * nlohmann::json j = {
 *     {"email", user.email.toBase64()}
 * };
 * 
 * User loaded;
 * loaded.email = EncryptedField<std::string>::fromBase64(j["email"]);
 * @endcode
 */
template<typename T>
class EncryptedField {
public:
    /**
     * @brief Set global field encryption instance
     * 
     * Must be called before using any EncryptedField instances.
     * 
     * @param encryption Shared FieldEncryption instance
     */
    static void setFieldEncryption(std::shared_ptr<FieldEncryption> encryption);
    
    /**
     * @brief Default constructor (empty field)
     */
    EncryptedField();
    
    /**
     * @brief Construct from plaintext value
     * 
     * @param value Plaintext value to encrypt
     * @param key_id Key identifier for encryption
     */
    EncryptedField(const T& value, const std::string& key_id);
    
    /**
     * @brief Construct from encrypted blob
     * 
     * @param blob Pre-encrypted data
     */
    explicit EncryptedField(const EncryptedBlob& blob);
    
    /**
     * @brief Assign plaintext value (triggers encryption)
     * 
     * @param value Plaintext value
     * @param key_id Key identifier
     */
    void encrypt(const T& value, const std::string& key_id);
    
    /**
     * @brief Decrypt and return plaintext value
     * 
     * @return Decrypted value
     * @throws DecryptionException if decryption fails
     */
    T decrypt() const;
    
    /**
     * @brief Check if field contains encrypted data
     * 
     * @return true if field has encrypted data
     */
    bool isEncrypted() const;
    
    /**
     * @brief Check if field contains encrypted data (alias for isEncrypted)
     * 
     * @return true if field has data
     */
    bool hasValue() const;
    
    /**
     * @brief Serialize to base64 string
     * 
     * @return Base64-encoded encrypted data
     */
    std::string toBase64() const;
    
    /**
     * @brief Deserialize from base64 string
     * 
     * @param b64 Base64-encoded data
     * @return EncryptedField instance
     */
    static EncryptedField<T> fromBase64(const std::string& b64);
    
    /**
     * @brief Serialize to JSON
     * 
     * @return JSON representation
     */
    nlohmann::json toJson() const;
    
    /**
     * @brief Deserialize from JSON
     * 
     * @param j JSON object
     * @return EncryptedField instance
     */
    static EncryptedField<T> fromJson(const nlohmann::json& j);
    
    /**
     * @brief Get underlying encrypted blob
     * 
     * @return Reference to encrypted blob
     */
    const EncryptedBlob& getBlob() const { return blob_; }

private:
    EncryptedBlob blob_;
    static std::shared_ptr<FieldEncryption> field_encryption_;
    
    // Type-specific serialization helpers
    static std::string serialize(const T& value);
    static T deserialize(const std::string& str);
};

// Template method declarations (implementation in encryption.cpp)
template<typename T>
std::shared_ptr<FieldEncryption> EncryptedField<T>::field_encryption_ = nullptr;

}  // namespace themis
