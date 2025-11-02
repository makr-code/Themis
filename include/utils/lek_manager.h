#pragma once

#include "security/encryption.h"
#include "utils/pki_client.h"
#include "storage/rocksdb_wrapper.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace themis {
namespace utils {

/**
 * @brief Log Encryption Key Manager with daily rotation
 * 
 * Key Hierarchy:
 * 1. KEK (Key Encryption Key) - Derived from PKI certificate via HKDF
 * 2. LEK (Log Encryption Key) - Random 256-bit AES key, rotated daily
 * 3. LEK stored encrypted with KEK in RocksDB: lek:<date> = AES-GCM(KEK, LEK)
 * 
 * Usage:
 * - Encrypt logs with current LEK
 * - Decrypt historical logs by loading LEK for specific date
 * - Automatic rotation at midnight (configurable)
 */
class LEKManager {
public:
    /**
     * @brief Initialize LEK Manager
     * @param db RocksDB wrapper for persistent storage
     * @param pki PKI client for KEK derivation
     * @param key_provider For encrypting/decrypting LEKs with KEK
     */
    LEKManager(std::shared_ptr<themis::RocksDBWrapper> db,
               std::shared_ptr<VCCPKIClient> pki,
               std::shared_ptr<KeyProvider> key_provider);
    
    /**
     * @brief Get current LEK (creates if not exists)
     * @return LEK key_id for use with FieldEncryption
     */
    std::string getCurrentLEK();
    
    /**
     * @brief Get LEK for specific date (for decrypting old logs)
     * @param date_str Format: "YYYY-MM-DD"
     * @return LEK key_id or empty if not found
     */
    std::string getLEKForDate(const std::string& date_str);
    
    /**
     * @brief Force rotation (creates new LEK for today)
     */
    void rotate();
    
    /**
     * @brief Get current date string (YYYY-MM-DD)
     */
    static std::string getCurrentDateString();

private:
    void ensureLEKExists(const std::string& date_str);
    std::vector<uint8_t> deriveKEK();
    std::string lekKeyId(const std::string& date_str) const;
    std::string dbKey(const std::string& date_str) const;
    
    std::shared_ptr<themis::RocksDBWrapper> db_;
    std::shared_ptr<VCCPKIClient> pki_;
    std::shared_ptr<KeyProvider> key_provider_;
    
    std::mutex mu_;
    std::unordered_map<std::string, std::string> lek_cache_; // date -> key_id
    std::string kek_key_id_ = "lek_kek";
};

} // namespace utils
} // namespace themis
