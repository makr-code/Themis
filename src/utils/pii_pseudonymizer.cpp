#include "utils/pii_pseudonymizer.h"
#include "storage/rocksdb_wrapper.h"

#include <openssl/rand.h>
#include <iomanip>
#include <sstream>

namespace themis {
namespace utils {

PIIPseudonymizer::PIIPseudonymizer(std::shared_ptr<themis::RocksDBWrapper> db,
                                  std::shared_ptr<FieldEncryption> enc,
                                  std::shared_ptr<PIIDetector> detector,
                                  std::shared_ptr<AuditLogger> audit_logger)
    : db_(std::move(db))
    , enc_(std::move(enc))
    , detector_(std::move(detector))
    , audit_logger_(std::move(audit_logger)) {
    
    // Ensure encryption key exists
    auto key_provider = enc_->getKeyProvider();
    if (!key_provider->hasKey(key_id_)) {
        // Generate random 256-bit key
        std::vector<uint8_t> key_bytes(32);
        if (RAND_bytes(key_bytes.data(), key_bytes.size()) != 1) {
            throw std::runtime_error("Failed to generate random key for PII mapping");
        }
        key_provider->createKeyFromBytes(key_id_, key_bytes);
    }
}

std::string PIIPseudonymizer::generateUUID() const {
    // Generate UUIDv4
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("Failed to generate random UUID");
    }
    
    // Set version (4) and variant bits
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // Variant 10
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return oss.str();
}

std::string PIIPseudonymizer::dbKey(const std::string& pii_uuid) const {
    return "pii:mapping:" + pii_uuid;
}

std::string PIIPseudonymizer::entityIndexKey(const std::string& entity_pk) const {
    return "pii:entity_index:" + entity_pk;
}

std::pair<nlohmann::json, std::vector<std::string>> PIIPseudonymizer::pseudonymize(
    const nlohmann::json& data
) {
    std::scoped_lock lk(mu_);
    
    auto findings_map = detector_->detectInJson(data);
    if (findings_map.empty()) {
        return {data, {}};
    }
    
    nlohmann::json pseudonymized = data;
    std::vector<std::string> created_uuids;
    
    // Iterate over all JSON paths and their findings
    for (const auto& [json_path, findings] : findings_map) {
        for (const auto& finding : findings) {
            // Generate UUID
            auto pii_uuid = generateUUID();
            created_uuids.push_back(pii_uuid);
            
            // Encrypt original PII value
            auto encrypted = enc_->encrypt(finding.value, key_id_);
            
            // Store mapping: uuid → {original_value, type, json_path, created_at}
            nlohmann::json mapping;
            mapping["original_value_encrypted"] = encrypted.toJson();
            mapping["pii_type"] = PIITypeUtils::toString(finding.type);
            mapping["json_path"] = json_path;
            mapping["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::string mapping_str = mapping.dump();
            std::vector<uint8_t> mapping_bytes(mapping_str.begin(), mapping_str.end());
            db_->put(dbKey(pii_uuid), mapping_bytes);
            
            // Replace in JSON using json_path
            // Simplified: Replace all occurrences of value with UUID
            std::string data_str = pseudonymized.dump();
            size_t pos = data_str.find(finding.value);
            while (pos != std::string::npos) {
                data_str.replace(pos, finding.value.length(), "pii_" + pii_uuid);
                pos = data_str.find(finding.value, pos + pii_uuid.length());
            }
            
            pseudonymized = nlohmann::json::parse(data_str, nullptr, false);
            if (pseudonymized.is_discarded()) {
                // Fallback: JSON replacement failed, keep original
                pseudonymized = data;
            }
        }
    }
    
    return {pseudonymized, created_uuids};
}

std::optional<std::string> PIIPseudonymizer::revealPII(const std::string& pii_uuid,
                                                      const std::string& user_id) {
    std::scoped_lock lk(mu_);
    
    // Load mapping
    auto mapping_str = db_->get(dbKey(pii_uuid));
    if (!mapping_str) {
        return std::nullopt;
    }
    
    try {
        auto mapping = nlohmann::json::parse(*mapping_str);
        // Respect soft-delete flag
        bool active = mapping.value("active", true);
        if (!active) {
            return std::nullopt; // hidden
        }
        
        // Decrypt original value
        auto encrypted_json = mapping["original_value_encrypted"];
        auto blob = themis::EncryptedBlob::fromJson(encrypted_json);
        auto original = enc_->decrypt(blob);
        
        // Audit PII reveal (DSGVO Art. 30 compliance)
        if (audit_logger_) {
            audit_logger_->logEvent({
                {"action", "PII_REVEAL"},
                {"pii_uuid", pii_uuid},
                {"user_id", user_id},
                {"timestamp", std::time(nullptr)}
            });
        }
        
        return original;
        
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

bool PIIPseudonymizer::erasePII(const std::string& pii_uuid) {
    std::scoped_lock lk(mu_);
    
    // Check if exists
    auto mapping_str = db_->get(dbKey(pii_uuid));
    if (!mapping_str) {
        return false;
    }
    
    // Delete mapping
    db_->del(dbKey(pii_uuid));
    
    // Audit PII erasure (DSGVO Art. 17 & Art. 30 compliance)
    if (audit_logger_) {
        audit_logger_->logEvent({
            {"action", "PII_ERASE"},
            {"pii_uuid", pii_uuid},
            {"timestamp", std::time(nullptr)}
        });
    }
    
    return true;
}

bool PIIPseudonymizer::softDeletePII(const std::string& pii_uuid, const std::string& user_id) {
    std::scoped_lock lk(mu_);
    auto mapping_str = db_->get(dbKey(pii_uuid));
    if (!mapping_str) return false;
    try {
        auto mapping = nlohmann::json::parse(*mapping_str);
        mapping["active"] = false;
        mapping["deleted_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch()).count();
        std::string out = mapping.dump();
        std::vector<uint8_t> bytes(out.begin(), out.end());
        db_->put(dbKey(pii_uuid), bytes);
        if (audit_logger_) {
            audit_logger_->logEvent({
                {"action", "PII_SOFT_DELETE"},
                {"pii_uuid", pii_uuid},
                {"user_id", user_id},
                {"timestamp", std::time(nullptr)}
            });
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> PIIPseudonymizer::findPIIForEntity(const std::string& entity_pk) {
    std::scoped_lock lk(mu_);
    
    auto index_str = db_->get(entityIndexKey(entity_pk));
    if (!index_str) {
        return {};
    }
    
    try {
        auto index = nlohmann::json::parse(*index_str);
        return index["pii_uuids"].get<std::vector<std::string>>();
    } catch (...) {
        return {};
    }
}

size_t PIIPseudonymizer::eraseAllPIIForEntity(const std::string& entity_pk) {
    auto pii_uuids = findPIIForEntity(entity_pk);
    
    size_t erased_count = 0;
    for (const auto& uuid : pii_uuids) {
        if (erasePII(uuid)) {
            ++erased_count;
        }
    }
    
    // Delete entity index
    db_->del(entityIndexKey(entity_pk));
    
    return erased_count;
}

} // namespace utils
} // namespace themis
