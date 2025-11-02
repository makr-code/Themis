#include "utils/lek_manager.h"
#include "storage/rocksdb_wrapper.h"
#include "utils/hkdf_helper.h"

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>

namespace themis {
namespace utils {

LEKManager::LEKManager(std::shared_ptr<themis::RocksDBWrapper> db,
                       std::shared_ptr<VCCPKIClient> pki,
                       std::shared_ptr<KeyProvider> key_provider)
    : db_(std::move(db))
    , pki_(std::move(pki))
    , key_provider_(std::move(key_provider)) {
    
    // Ensure KEK exists
    if (!key_provider_->hasKey(kek_key_id_)) {
        auto kek = deriveKEK();
        key_provider_->createKeyFromBytes(kek_key_id_, kek);
    }
}

std::string LEKManager::getCurrentDateString() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

std::string LEKManager::lekKeyId(const std::string& date_str) const {
    return "lek_" + date_str;
}

std::string LEKManager::dbKey(const std::string& date_str) const {
    return "lek:encrypted:" + date_str;
}

std::vector<uint8_t> LEKManager::deriveKEK() {
    // Derive KEK from PKI certificate using HKDF
    // For now, use a deterministic derivation from service ID
    // In production: use actual certificate's public key material
    
    std::string service_id = "themis-lek-kek";
    std::string info = "KEK for ThemisDB LEK";
    
    return HKDFHelper::deriveFromString(service_id, info, 32);
}

void LEKManager::ensureLEKExists(const std::string& date_str) {
    auto key_id = lekKeyId(date_str);
    
    // Check if already in KeyProvider
    if (key_provider_->hasKey(key_id)) {
        return;
    }
    
    // Check if encrypted LEK exists in DB
    auto db_key_str = dbKey(date_str);
    auto encrypted_lek_opt = db_->get(db_key_str);
    
    if (encrypted_lek_opt) {
        // Decrypt and load into KeyProvider
        try {
            auto encrypted_lek_json = nlohmann::json::parse(*encrypted_lek_opt);
            auto blob = themis::EncryptedBlob::fromJson(encrypted_lek_json);
            
            FieldEncryption enc(key_provider_);
            auto lek_bytes = enc.decrypt(blob);
            
            key_provider_->createKeyFromBytes(key_id, 
                std::vector<uint8_t>(lek_bytes.begin(), lek_bytes.end()));
            
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to decrypt LEK for " + date_str + ": " + e.what());
        }
        
    } else {
        // Generate new LEK
        std::vector<uint8_t> lek(32); // 256-bit AES key
        if (RAND_bytes(lek.data(), lek.size()) != 1) {
            throw std::runtime_error("Failed to generate random LEK");
        }
        
        // Encrypt with KEK
        FieldEncryption enc(key_provider_);
        std::string lek_plaintext(lek.begin(), lek.end());
        auto encrypted_lek = enc.encrypt(lek_plaintext, kek_key_id_);
        
        // Store in DB
        auto encrypted_json = themis::EncryptedBlob{encrypted_lek}.toJson();
        std::string json_str = encrypted_json.dump();
        std::vector<uint8_t> json_bytes(json_str.begin(), json_str.end());
        db_->put(db_key_str, json_bytes);
        
        // Load into KeyProvider
        key_provider_->createKeyFromBytes(key_id, lek);
    }
}

std::string LEKManager::getCurrentLEK() {
    std::scoped_lock lk(mu_);
    auto date_str = getCurrentDateString();
    
    // Check cache
    auto it = lek_cache_.find(date_str);
    if (it != lek_cache_.end()) {
        return it->second;
    }
    
    // Ensure exists and load
    ensureLEKExists(date_str);
    auto key_id = lekKeyId(date_str);
    lek_cache_[date_str] = key_id;
    
    return key_id;
}

std::string LEKManager::getLEKForDate(const std::string& date_str) {
    std::scoped_lock lk(mu_);
    
    // Check cache
    auto it = lek_cache_.find(date_str);
    if (it != lek_cache_.end()) {
        return it->second;
    }
    
    // Try to load from DB
    try {
        ensureLEKExists(date_str);
        auto key_id = lekKeyId(date_str);
        lek_cache_[date_str] = key_id;
        return key_id;
    } catch (...) {
        return ""; // LEK not found for this date
    }
}

void LEKManager::rotate() {
    std::scoped_lock lk(mu_);
    auto date_str = getCurrentDateString();
    
    // Remove from cache to force regeneration
    lek_cache_.erase(date_str);
    
    // Delete from DB
    db_->del(dbKey(date_str));
    
    // Regenerate
    ensureLEKExists(date_str);
    lek_cache_[date_str] = lekKeyId(date_str);
}

} // namespace utils
} // namespace themis
