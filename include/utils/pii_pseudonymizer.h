#pragma once

#include "utils/pii_detector.h"
#include "utils/audit_logger.h"
#include "security/encryption.h"
#include "storage/rocksdb_wrapper.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace themis {
namespace utils {

/**
 * @brief PII Pseudonymization Manager with DSGVO Art. 17 compliance
 * 
 * DSGVO Art. 17 Compliant PII Management:
 * - Replace detected PII with UUIDs in entities
 * - Store encrypted PII→UUID mapping in separate RocksDB Column Family
 * - revealPII(): Decrypt and return original value (with audit log)
 * - erasePII(): Delete mapping → original value irrecoverable
 * 
 * Workflow:
 * 1. Entity import: Detect PII → Replace with UUID → Store mapping
 * 2. Query: Entity contains UUIDs, original values hidden
 * 3. Reveal: Authorized user → decrypt PII mapping → log access
 * 4. Erasure: Delete mapping → UUID remains, but original lost forever
 */
class PIIPseudonymizer {
public:
    /**
     * @brief Initialize with storage and encryption
     * @param db RocksDB wrapper (must have "pii_mapping" column family)
     * @param enc Field encryption for PII values
     * @param detector PII detector for automatic detection
     * @param audit_logger Audit logger for PII access/erasure events (optional)
     */
    PIIPseudonymizer(std::shared_ptr<themis::RocksDBWrapper> db,
                     std::shared_ptr<FieldEncryption> enc,
                     std::shared_ptr<PIIDetector> detector,
                     std::shared_ptr<AuditLogger> audit_logger = nullptr);
    
    /**
     * @brief Pseudonymize detected PII in JSON object
     * @param data Input JSON with potential PII
     * @return JSON with PII replaced by UUIDs, list of mappings created
     */
    std::pair<nlohmann::json, std::vector<std::string>> pseudonymize(
        const nlohmann::json& data
    );
    
    /**
     * @brief Reveal original PII value for authorized user
     * @param pii_uuid UUID from pseudonymized entity
     * @param user_id User requesting reveal (for audit log)
     * @return Original PII value or empty if not found/unauthorized
     */
    std::optional<std::string> revealPII(const std::string& pii_uuid, 
                                        const std::string& user_id);
    
    /**
     * @brief Erase PII mapping (DSGVO Art. 17 - Right to be Forgotten)
     * @param pii_uuid UUID to erase
     * @return true if mapping existed and was deleted
     */
    bool erasePII(const std::string& pii_uuid);

    /**
     * @brief Soft-Delete eines PII-Mappings (ausblenden, aber nicht löschen)
     * Markiert das Mapping als inactive und setzt deleted_at.
     * @param pii_uuid UUID des Mappings
     * @param user_id Benutzer, der die Aktion ausführt (für Audit)
     * @return true, wenn Mapping existierte und aktualisiert wurde
     */
    bool softDeletePII(const std::string& pii_uuid, const std::string& user_id);
    
    /**
     * @brief Find all PII UUIDs for a specific entity
     * @param entity_pk Primary key of entity
     * @return List of PII UUIDs associated with this entity
     */
    std::vector<std::string> findPIIForEntity(const std::string& entity_pk);
    
    /**
     * @brief Erase all PII for entity (DSGVO Art. 17 complete erasure)
     * @param entity_pk Primary key of entity
     * @return Number of PII mappings erased
     */
    size_t eraseAllPIIForEntity(const std::string& entity_pk);

private:
    std::string generateUUID() const;
    std::string dbKey(const std::string& pii_uuid) const;
    std::string entityIndexKey(const std::string& entity_pk) const;
    
    std::shared_ptr<themis::RocksDBWrapper> db_;
    std::shared_ptr<FieldEncryption> enc_;
    std::shared_ptr<PIIDetector> detector_;
    std::shared_ptr<AuditLogger> audit_logger_;
    
    // Recursive to avoid EDEADLK when higher-level helpers call into multiple operations
    // (e.g., eraseAll -> erasePII) within the same thread context.
    std::recursive_mutex mu_;
    std::string key_id_ = "pii_mapping_key";
};

} // namespace utils
} // namespace themis
