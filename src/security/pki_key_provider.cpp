#include "security/pki_key_provider.h"
#include "storage/rocksdb_wrapper.h"
#include "utils/hkdf_helper.h"

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <stdexcept>

namespace themis {
namespace security {

PKIKeyProvider::PKIKeyProvider(std::shared_ptr<utils::VCCPKIClient> pki,
                               std::shared_ptr<themis::RocksDBWrapper> db,
                               const std::string& service_id)
    : pki_(std::move(pki))
    , db_(std::move(db))
    , service_id_(service_id) {
    
    // Derive KEK from PKI certificate
    kek_ = deriveKEK();
    
    // Load or create initial DEK
    loadOrCreateDEK(current_dek_version_);
}

std::vector<uint8_t> PKIKeyProvider::deriveKEK() {
    // Persistente KEK-Ableitung:
    // Wir speichern ein zufälliges IKM (Initial Key Material) einmalig in RocksDB.
    // Dadurch bleibt der KEK über Neustarts stabil, ohne ein Zertifikat parsen zu müssen.
    // Format: Hex-codierte 32 Bytes unter Key "kek:ikm:{service_id}".

    const std::string ikm_db_key = "kek:ikm:" + service_id_;
    std::vector<uint8_t> ikm_raw;
    auto existing_opt = db_->get(ikm_db_key);
    if (existing_opt.has_value()) {
        // Decode hex
        const std::string hex(existing_opt->begin(), existing_opt->end());
        if (hex.size() != 64) {
            throw std::runtime_error("Persisted IKM hat unerwartete Länge (" + std::to_string(hex.size()) + ")");
        }
        ikm_raw.reserve(32);
        for (size_t i = 0; i < hex.size(); i += 2) {
            auto byte_str = hex.substr(i, 2);
            uint8_t b = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            ikm_raw.push_back(b);
        }
    } else {
        // Generiere neues IKM (32 zufällige Bytes) und speichere hex-codiert
        ikm_raw.resize(32);
        if (RAND_bytes(ikm_raw.data(), static_cast<int>(ikm_raw.size())) != 1) {
            throw std::runtime_error("RAND_bytes für IKM fehlgeschlagen");
        }
        static const char* hex_chars = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (uint8_t b : ikm_raw) {
            hex.push_back(hex_chars[(b >> 4) & 0xF]);
            hex.push_back(hex_chars[b & 0xF]);
        }
        db_->put(ikm_db_key, std::vector<uint8_t>(hex.begin(), hex.end()));
    }

    // HKDF zur Ableitung des KEK aus IKM + service_id Kontext
    std::string info = "KEK derivation:" + service_id_;
    std::vector<uint8_t> salt; // leerer Salt
    auto kek = utils::HKDFHelper::derive(ikm_raw, salt, info, 32);
    return kek;
}

std::string PKIKeyProvider::dekDbKey(uint32_t version) const {
    return "dek:encrypted:v" + std::to_string(version);
}

std::vector<uint8_t> PKIKeyProvider::loadOrCreateDEK(uint32_t version) {
    // Check cache
    auto it = dek_cache_.find(version);
    if (it != dek_cache_.end()) {
        return it->second;
    }
    
    // Try load from DB
    auto db_key_str = dekDbKey(version);
    auto encrypted_dek_opt = db_->get(db_key_str);
    
    if (encrypted_dek_opt) {
        // Decrypt DEK with KEK using AES-GCM
        try {
            // First, try to parse as EncryptedBlob JSON as produced by EncryptedBlob::toJson()
            themis::EncryptedBlob blob;
            bool parsed = false;
            try {
                auto encrypted_json = nlohmann::json::parse(*encrypted_dek_opt);
                blob = themis::EncryptedBlob::fromJson(encrypted_json);
                parsed = true;
            } catch (...) {
                parsed = false;
            }
            
            // If JSON parsing failed, try legacy/binary format: iv||ciphertext||tag
            if (!parsed) {
                const auto& enc = *encrypted_dek_opt;
                if (enc.size() < 12 + 16) {
                    throw std::runtime_error("Invalid encrypted DEK format");
                }
                blob.iv.assign(enc.begin(), enc.begin() + 12);
                blob.ciphertext.assign(enc.begin() + 12, enc.end() - 16);
                blob.tag.assign(enc.end() - 16, enc.end());
            }
            
            // Decrypt manually with KEK
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            if (!ctx) throw std::runtime_error("Failed to create cipher context");
            
            if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, kek_.data(), blob.iv.data()) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                throw std::runtime_error("DecryptInit failed");
            }
            
            std::vector<uint8_t> dek(blob.ciphertext.size());
            int len = 0;
            
            if (EVP_DecryptUpdate(ctx, dek.data(), &len, blob.ciphertext.data(), static_cast<int>(blob.ciphertext.size())) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                throw std::runtime_error("DecryptUpdate failed");
            }
            
            if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(blob.tag.size()), blob.tag.data()) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                throw std::runtime_error("Set tag failed");
            }
            
            int final_len = 0;
            if (EVP_DecryptFinal_ex(ctx, dek.data() + len, &final_len) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                throw std::runtime_error("DecryptFinal failed (tag mismatch)");
            }
            
            EVP_CIPHER_CTX_free(ctx);
            dek.resize(len + final_len);
            
            dek_cache_[version] = dek;
            return dek;
            
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to decrypt DEK v" + std::to_string(version) + ": " + e.what());
        }
    } else {
        // Generate new DEK
        std::vector<uint8_t> dek(32); // 256-bit
        if (RAND_bytes(dek.data(), dek.size()) != 1) {
            throw std::runtime_error("Failed to generate random DEK");
        }
        
        // Encrypt DEK with KEK using AES-GCM
        std::vector<uint8_t> iv(12);
        if (RAND_bytes(iv.data(), iv.size()) != 1) {
            throw std::runtime_error("Failed to generate IV for DEK encryption");
        }
        
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create cipher context");
        
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, kek_.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EncryptInit failed");
        }
        
        std::vector<uint8_t> ciphertext(dek.size() + 16);
        int len = 0;
        
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, dek.data(), dek.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EncryptUpdate failed");
        }
        
        int final_len = 0;
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &final_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EncryptFinal failed");
        }
        
        ciphertext.resize(len + final_len);
        
        std::vector<uint8_t> tag(16);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Get tag failed");
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Store encrypted DEK
        themis::EncryptedBlob blob;
        blob.iv = iv;
        blob.ciphertext = ciphertext;
        blob.tag = tag;
        
        auto encrypted_json = blob.toJson();
        std::string json_str = encrypted_json.dump();
        std::vector<uint8_t> json_bytes(json_str.begin(), json_str.end());
        db_->put(db_key_str, json_bytes);
        
        dek_cache_[version] = dek;
        return dek;
    }
}

std::vector<uint8_t> PKIKeyProvider::deriveFieldKey(const std::string& field_context) {
    // Check cache
    auto it = field_key_cache_.find(field_context);
    if (it != field_key_cache_.end()) {
        return it->second;
    }
    
    // Derive from current DEK using HKDF
    auto dek = loadOrCreateDEK(current_dek_version_);
    
    std::string info = "field:" + field_context;
    std::vector<uint8_t> salt;  // Empty salt
    auto field_key = utils::HKDFHelper::derive(dek, salt, info, 32);
    
    field_key_cache_[field_context] = field_key;
    return field_key;
}

std::vector<uint8_t> PKIKeyProvider::getKey(const std::string& key_id) {
    return getKey(key_id, 0);
}

std::vector<uint8_t> PKIKeyProvider::getKey(const std::string& key_id, uint32_t version) {
    std::scoped_lock lk(mu_);
    
    // Special handling for DEK
    if (key_id == "dek" || key_id.rfind("dek_v", 0) == 0) {
        uint32_t v = version > 0 ? version : current_dek_version_;
        return loadOrCreateDEK(v);
    }
    
    // Otherwise derive field key
    return deriveFieldKey(key_id);
}

uint32_t PKIKeyProvider::rotateKey(const std::string& key_id) {
    if (key_id == "dek") {
        return rotateDEK();
    }
    
    // For field keys, just regenerate
    std::scoped_lock lk(mu_);
    field_key_cache_.erase(key_id);
    return 1;
}

std::vector<KeyMetadata> PKIKeyProvider::listKeys() {
    std::scoped_lock lk(mu_);
    
    std::vector<KeyMetadata> keys;
    
    // Add DEK
    KeyMetadata dek_meta;
    dek_meta.key_id = "dek";
    dek_meta.version = current_dek_version_;
    dek_meta.algorithm = "AES-256-GCM";
    dek_meta.status = KeyStatus::ACTIVE;
    keys.push_back(dek_meta);
    
    // Add cached field keys
    for (const auto& [key_id, _] : field_key_cache_) {
        KeyMetadata meta;
        meta.key_id = key_id;
        meta.version = 1;
        meta.algorithm = "AES-256-GCM";
        meta.status = KeyStatus::ACTIVE;
        keys.push_back(meta);
    }
    
    return keys;
}

KeyMetadata PKIKeyProvider::getKeyMetadata(const std::string& key_id, uint32_t version) {
    std::scoped_lock lk(mu_);
    
    KeyMetadata meta;
    meta.key_id = key_id;
    meta.version = version > 0 ? version : (key_id == "dek" ? current_dek_version_ : 1);
    meta.algorithm = "AES-256-GCM";
    meta.status = KeyStatus::ACTIVE;
    
    return meta;
}

void PKIKeyProvider::deleteKey(const std::string& key_id, uint32_t version) {
    std::scoped_lock lk(mu_);
    
    if (key_id == "dek") {
        throw std::runtime_error("Cannot delete DEK");
    }
    
    field_key_cache_.erase(key_id);
}

bool PKIKeyProvider::hasKey(const std::string& key_id, uint32_t version) {
    std::scoped_lock lk(mu_);
    
    if (key_id == "dek") return true; // DEK always available
    
    if (version == 0) {
        return field_key_cache_.find(key_id) != field_key_cache_.end();
    }
    
    // For specific version, check if it exists
    return field_key_cache_.find(key_id) != field_key_cache_.end();
}

uint32_t PKIKeyProvider::createKeyFromBytes(
    const std::string& key_id,
    const std::vector<uint8_t>& key_bytes,
    const KeyMetadata& metadata) {
    
    std::scoped_lock lk(mu_);
    field_key_cache_[key_id] = key_bytes;
    return 1;
}

uint32_t PKIKeyProvider::rotateDEK() {
    std::scoped_lock lk(mu_);
    
    ++current_dek_version_;
    loadOrCreateDEK(current_dek_version_);
    
    // Clear field key cache (will be re-derived with new DEK)
    field_key_cache_.clear();
    
    return current_dek_version_;
}

uint32_t PKIKeyProvider::getCurrentDEKVersion() const {
    std::scoped_lock lk(mu_);
    return current_dek_version_;
}

// ============================================================================
// Group DEK Management
// ============================================================================

std::string PKIKeyProvider::groupDekDbKey(const std::string& group_name, uint32_t version) const {
    return "key:group:" + group_name + ":v" + std::to_string(version);
}

std::string PKIKeyProvider::groupMetadataDbKey(const std::string& group_name) const {
    return "key:group:" + group_name + ":meta";
}

std::vector<uint8_t> PKIKeyProvider::loadOrCreateGroupDEK(const std::string& group_name, uint32_t version) {
    // Try load from DB
    auto db_key_str = groupDekDbKey(group_name, version);
    auto encrypted_dek_opt = db_->get(db_key_str);
    
    std::vector<uint8_t> dek;
    
    if (encrypted_dek_opt.has_value()) {
        // Decrypt with KEK
        auto encrypted = *encrypted_dek_opt;
        
        // Extract nonce (first 12 bytes)
        if (encrypted.size() < 12 + 16) {
            throw std::runtime_error("Invalid encrypted Group DEK format");
        }
        
        std::vector<uint8_t> nonce(encrypted.begin(), encrypted.begin() + 12);
        std::vector<uint8_t> ciphertext(encrypted.begin() + 12, encrypted.end() - 16);
        std::vector<uint8_t> tag(encrypted.end() - 16, encrypted.end());
        
        // Decrypt
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
        
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, kek_.data(), nonce.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_DecryptInit_ex failed");
        }
        
        dek.resize(ciphertext.size());
        int len = 0;
        if (EVP_DecryptUpdate(ctx, dek.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_DecryptUpdate failed");
        }
        
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_CIPHER_CTX_ctrl (set tag) failed");
        }
        
        int final_len = 0;
        if (EVP_DecryptFinal_ex(ctx, dek.data() + len, &final_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Group DEK decryption failed (authentication failed)");
        }
        
        dek.resize(len + final_len);
        EVP_CIPHER_CTX_free(ctx);
        
    } else {
        // Generate new Group DEK
        dek.resize(32); // AES-256
        if (RAND_bytes(dek.data(), static_cast<int>(dek.size())) != 1) {
            throw std::runtime_error("RAND_bytes failed for Group DEK");
        }
        
        // Encrypt with KEK before storing
        std::vector<uint8_t> nonce(12);
        if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
            throw std::runtime_error("RAND_bytes failed for nonce");
        }
        
        std::vector<uint8_t> ciphertext(dek.size());
        std::vector<uint8_t> tag(16);
        
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
        
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, kek_.data(), nonce.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptInit_ex failed");
        }
        
        int len = 0;
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, dek.data(), static_cast<int>(dek.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptUpdate failed");
        }
        
        int final_len = 0;
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &final_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptFinal_ex failed");
        }
        
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_CIPHER_CTX_ctrl (get tag) failed");
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Store: nonce + ciphertext + tag
        std::vector<uint8_t> encrypted;
        encrypted.reserve(nonce.size() + len + final_len + tag.size());
        encrypted.insert(encrypted.end(), nonce.begin(), nonce.end());
        encrypted.insert(encrypted.end(), ciphertext.begin(), ciphertext.begin() + len + final_len);
        encrypted.insert(encrypted.end(), tag.begin(), tag.end());
        
        db_->put(db_key_str, encrypted);
        
        // Update metadata
        auto meta_key = groupMetadataDbKey(group_name);
        std::string meta_value = std::to_string(version) + "|" + std::to_string(std::time(nullptr));
        db_->put(meta_key, std::vector<uint8_t>(meta_value.begin(), meta_value.end()));
    }
    
    return dek;
}

std::vector<uint8_t> PKIKeyProvider::getGroupDEK(const std::string& group_name) {
    std::scoped_lock lk(mu_);
    
    // Get current version for this group
    uint32_t version = 1;
    auto ver_it = group_versions_.find(group_name);
    if (ver_it != group_versions_.end()) {
        version = ver_it->second;
    } else {
        // Try load from metadata
        auto meta_key = groupMetadataDbKey(group_name);
        auto meta_opt = db_->get(meta_key);
        if (meta_opt.has_value()) {
            std::string meta_str(meta_opt->begin(), meta_opt->end());
            size_t pos = meta_str.find('|');
            if (pos != std::string::npos) {
                version = std::stoul(meta_str.substr(0, pos));
            }
        }
        group_versions_[group_name] = version;
    }
    
    // Check cache
    auto& group_cache = group_dek_cache_[group_name];
    auto it = group_cache.find(version);
    if (it != group_cache.end()) {
        return it->second;
    }
    
    // Load or create
    auto dek = loadOrCreateGroupDEK(group_name, version);
    group_cache[version] = dek;
    
    return dek;
}

uint32_t PKIKeyProvider::rotateGroupDEK(const std::string& group_name) {
    std::scoped_lock lk(mu_);
    
    // Get current version
    uint32_t current_version = 1;
    auto ver_it = group_versions_.find(group_name);
    if (ver_it != group_versions_.end()) {
        current_version = ver_it->second;
    }
    
    // Increment version
    uint32_t new_version = current_version + 1;
    group_versions_[group_name] = new_version;
    
    // Create new DEK
    auto dek = loadOrCreateGroupDEK(group_name, new_version);
    group_dek_cache_[group_name][new_version] = dek;
    
    // Mark old version as deprecated in metadata
    auto meta_key = groupMetadataDbKey(group_name);
    std::string meta_value = std::to_string(new_version) + "|" + std::to_string(std::time(nullptr)) + "|rotated";
    db_->put(meta_key, std::vector<uint8_t>(meta_value.begin(), meta_value.end()));
    
    return new_version;
}

uint32_t PKIKeyProvider::getGroupDEKVersion(const std::string& group_name) const {
    std::scoped_lock lk(mu_);
    
    auto ver_it = group_versions_.find(group_name);
    if (ver_it != group_versions_.end()) {
        return ver_it->second;
    }
    
    // Try load from DB
    auto meta_key = groupMetadataDbKey(group_name);
    auto meta_opt = db_->get(meta_key);
    if (meta_opt.has_value()) {
        std::string meta_str(meta_opt->begin(), meta_opt->end());
        size_t pos = meta_str.find('|');
        if (pos != std::string::npos) {
            return std::stoul(meta_str.substr(0, pos));
        }
    }
    
    return 0; // Group doesn't exist
}

std::vector<std::string> PKIKeyProvider::listGroups() const {
    std::scoped_lock lk(mu_);
    
    std::vector<std::string> groups;
    groups.reserve(group_versions_.size());
    
    for (const auto& [group_name, version] : group_versions_) {
        groups.push_back(group_name);
    }
    
    return groups;
}

} // namespace security
} // namespace themis
