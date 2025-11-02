#pragma once

#include "security/encryption.h"
#include "utils/pki_client.h"
#include "storage/rocksdb_wrapper.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace themis {
namespace security {

/**
 * @brief Production KeyProvider with PKI-based 3-tier key hierarchy
 * 
 * Key Hierarchy:
 * 1. KEK (Key Encryption Key) - Derived from VCC-PKI service certificate
 * 2. DEK (Data Encryption Key) - Random 256-bit AES key, encrypted with KEK
 * 3. Field Keys - Derived from DEK using HKDF with field-specific context
 * 
 * Advantages:
 * - KEK rotation: Update certificate, re-encrypt DEK (no data re-encryption)
 * - DEK rotation: Generate new DEK, re-encrypt data (lazy migration possible)
 * - Per-field keys: Derived on-demand, no storage overhead
 */
class PKIKeyProvider : public KeyProvider {
public:
    /**
     * @brief Initialize with PKI client and persistent storage
     * @param pki VCC-PKI client for certificate operations
     * @param db RocksDB for encrypted DEK storage
     * @param service_id Service identifier for certificate lookup
     */
    PKIKeyProvider(std::shared_ptr<utils::VCCPKIClient> pki,
                   std::shared_ptr<themis::RocksDBWrapper> db,
                   const std::string& service_id);
    
    // KeyProvider interface
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
    
    /**
     * @brief Rotate DEK (generates new DEK, marks old as deprecated)
     * @return New DEK version number
     */
    uint32_t rotateDEK();
    
    /**
     * @brief Get current DEK version
     */
    uint32_t getCurrentDEKVersion() const;

private:
    std::vector<uint8_t> deriveKEK();
    std::vector<uint8_t> loadOrCreateDEK(uint32_t version);
    std::vector<uint8_t> deriveFieldKey(const std::string& field_context);
    std::string dekDbKey(uint32_t version) const;
    
    std::shared_ptr<utils::VCCPKIClient> pki_;
    std::shared_ptr<themis::RocksDBWrapper> db_;
    std::string service_id_;
    
    mutable std::mutex mu_;
    std::vector<uint8_t> kek_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> dek_cache_; // version -> DEK
    std::unordered_map<std::string, std::vector<uint8_t>> field_key_cache_;
    uint32_t current_dek_version_ = 1;
};

} // namespace security
} // namespace themis
