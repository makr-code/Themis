# Column-Level Encryption Design

**Status:** Design Phase (Sprint C.3)  
**Datum:** 30. Oktober 2025  
**Autor:** Themis Development Team

---

## 1. Überblick

Column-Level Encryption ermöglicht die Verschlüsselung sensibler Datenfelder at-rest in der Datenbank. Dies erfüllt Compliance-Anforderungen (DSGVO, HIPAA) und schützt vor Insider-Threats und Storage-Compromise-Szenarien.

### 1.1 Ziele

- **Data-at-Rest Protection**: Sensible Felder (Email, SSN, Kreditkarten) verschlüsselt speichern
- **Transparent Usage**: Entwickler arbeiten mit `EncryptedField<T>`, Verschlüsselung automatisch
- **Key Rotation**: Unterstützung für periodischen Schlüsselwechsel ohne Downtime
- **Minimal Performance Impact**: <10ms Overhead für Encrypt/Decrypt Operationen
- **Pluggable Key Management**: Interface für HashiCorp Vault, AWS KMS, Azure Key Vault

### 1.2 Nicht-Ziele (v1)

- ❌ Encryption-in-Transit (wird durch TLS abgedeckt)
- ❌ Homomorphic Encryption (zu langsam für produktive Nutzung)
- ❌ Searchable Encryption (zukünftiger Sprint)
- ❌ Database-Level Encryption (alternative Strategie via RocksDB encryption)

---

## 2. Threat Model

### 2.1 Bedrohungsszenarien

| Threat | Beschreibung | Mitigation durch Column Encryption |
|--------|--------------|-----------------------------------|
| **Storage Compromise** | Angreifer erhält Zugriff auf RocksDB SST-Files | ✅ Daten sind verschlüsselt, Keys separat gespeichert |
| **Backup Leakage** | Backup-Files werden versehentlich öffentlich | ✅ Verschlüsselte Daten unlesbar ohne Keys |
| **Insider Threat** | DB-Admin mit Disk-Zugriff | ✅ Keys nur in Key Management System, nicht auf Disk |
| **Memory Dump** | Angreifer liest RAM-Inhalte | ⚠️ Teilweise - entschlüsselte Daten temporär im RAM |
| **SQL Injection** | Angreifer extrahiert Daten via Query | ❌ Nicht geschützt - AppSec Verantwortung |

### 2.2 Trust Boundaries

```
┌─────────────────────────────────────────────────────────┐
│ Application Layer (Trusted)                             │
│ - EncryptedField<T> Templates                           │
│ - Plaintext briefly in memory during operations         │
└───────────────────┬─────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────┐
│ Encryption Layer (Trusted)                              │
│ - FieldEncryption: AES-256-GCM Encrypt/Decrypt          │
│ - Key Cache: In-memory cache (max 1000 keys, 1h TTL)   │
└───────────────────┬─────────────────────────────────────┘
                    │
         ┌──────────┴──────────┐
         ▼                     ▼
┌──────────────────┐  ┌──────────────────────┐
│ KeyProvider      │  │ Storage Layer        │
│ (External/Vault) │  │ (Untrusted)          │
│ - Stores KEKs    │  │ - Encrypted Data     │
│ - Access Control │  │ - Metadata (key ID)  │
└──────────────────┘  └──────────────────────┘
```

**Annahmen:**
- Application-Memory ist vertrauenswürdig (OS-Level Security)
- Key Management System (Vault/KMS) ist extern und gehärtet
- Netzwerk zwischen App und KMS ist TLS-gesichert

---

## 3. Architektur

### 3.1 Komponenten-Übersicht

```
┌──────────────────────────────────────────────────────────┐
│                   Application Code                       │
│  User user;                                              │
│  user.email = EncryptedField<std::string>("foo@bar.com");│
│  std::string plaintext = user.email.decrypt();          │
└────────────────────┬─────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────┐
│              EncryptedField<T> Template                  │
│  - Stores: {ciphertext, key_id, key_version, iv, tag}   │
│  - Methods: T decrypt(), void encrypt(T plaintext)      │
│  - Serialization: toJson() / fromJson()                 │
└────────────────────┬─────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────┐
│               FieldEncryption Class                      │
│  - encrypt(plaintext, key_id) -> EncryptedBlob          │
│  - decrypt(EncryptedBlob) -> plaintext                  │
│  - Algorithm: AES-256-GCM (AEAD)                        │
│  - IV: Random 12 bytes per encryption                   │
│  - Tag: 16 bytes authentication tag                     │
└────────────────────┬─────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────┐
│              KeyProvider Interface                       │
│  virtual std::vector<uint8_t> getKey(std::string id) = 0│
│  virtual void rotateKey(std::string id) = 0             │
│  virtual std::vector<KeyMetadata> listKeys() = 0        │
└──────────────┬───────────────────────────────────────────┘
               │
      ┌────────┴────────┐
      ▼                 ▼
┌──────────────┐  ┌──────────────────┐
│MockKeyProvider│ │VaultKeyProvider  │
│(In-Memory)    │ │(HashiCorp Vault) │
└───────────────┘ └──────────────────┘
```

### 3.2 Datenstrukturen

#### 3.2.1 EncryptedBlob

```cpp
struct EncryptedBlob {
    std::string key_id;           // "user_pii_v1"
    uint32_t key_version;         // 2 (for rotation)
    std::vector<uint8_t> iv;      // 12 bytes (AES-GCM standard)
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;     // 16 bytes authentication tag
    
    // Serialization: base64(key_id:version:iv:ciphertext:tag)
    std::string toBase64() const;
    static EncryptedBlob fromBase64(const std::string& b64);
};
```

#### 3.2.2 KeyMetadata

```cpp
struct KeyMetadata {
    std::string key_id;
    uint32_t version;
    std::string algorithm;        // "AES-256-GCM"
    int64_t created_at_ms;
    int64_t expires_at_ms;        // 0 = never
    KeyStatus status;             // ACTIVE, ROTATING, DEPRECATED
};
```

---

## 4. Encryption Flow

### 4.1 Encryption (Write Path)

```
1. Application sets value:
   user.email = EncryptedField<string>("alice@example.com");

2. EncryptedField<string>::operator=()
   ├─> FieldEncryption::encrypt("alice@example.com", "user_pii")
   │   ├─> KeyProvider::getKey("user_pii") -> 32 bytes DEK
   │   ├─> Generate random IV (12 bytes)
   │   ├─> OpenSSL EVP_EncryptInit_ex(AES-256-GCM)
   │   ├─> EVP_EncryptUpdate(plaintext)
   │   ├─> EVP_EncryptFinal_ex() -> ciphertext + tag
   │   └─> return EncryptedBlob{key_id, version, iv, ciphertext, tag}
   └─> Store EncryptedBlob internally

3. Storage Layer:
   ├─> user.toJson() -> {"email": "base64(blob)"}
   └─> RocksDB Put("d:users:123", json_string)
```

**Performance:**
- Key lookup: ~1ms (cached) / ~50ms (Vault API call)
- AES-256-GCM encryption: ~0.5ms for 1KB plaintext
- **Total: ~1.5ms (cached) / ~50ms (cold)**

### 4.2 Decryption (Read Path)

```
1. Application reads value:
   std::string email = user.email.decrypt();

2. EncryptedField<string>::decrypt()
   ├─> FieldEncryption::decrypt(stored_blob)
   │   ├─> KeyProvider::getKey(blob.key_id, blob.key_version)
   │   ├─> OpenSSL EVP_DecryptInit_ex(AES-256-GCM)
   │   ├─> EVP_DecryptUpdate(ciphertext)
   │   ├─> EVP_CIPHER_CTX_ctrl(EVP_CTRL_GCM_SET_TAG, tag)
   │   ├─> EVP_DecryptFinal_ex() -> plaintext (or AUTH_FAILED)
   │   └─> return plaintext
   └─> return std::string

3. Error Handling:
   ├─> Authentication failure -> throw DecryptionException
   ├─> Key not found -> throw KeyNotFoundException
   └─> Invalid base64 -> throw DecodingException
```

---

## 5. Key Management

### 5.1 Key Hierarchy

```
┌─────────────────────────────────────────────────┐
│ Key Encryption Key (KEK)                        │
│ - Stored in: Vault/KMS                          │
│ - Rotation: Annually                            │
│ - Access: Restricted to App Service Principal   │
└────────────────┬────────────────────────────────┘
                 │ (encrypts)
                 ▼
┌─────────────────────────────────────────────────┐
│ Data Encryption Keys (DEK)                      │
│ - Per field type: "user_pii", "payment_info"   │
│ - Versioned: v1, v2, ... (for rotation)         │
│ - Size: 256 bits (32 bytes)                     │
│ - Cache: In-memory, 1h TTL, max 1000 keys       │
└─────────────────────────────────────────────────┘
```

**Rationale:**
- KEK in Vault ermöglicht zentrale Kontrolle und Auditing
- DEKs gecached für Performance (1ms statt 50ms)
- Versionierung erlaubt sanfte Key Rotation ohne Re-Encryption

### 5.2 Key Rotation Process

**Scenario:** Rotate "user_pii" key from v2 to v3

```
Phase 1: Dual-Write (Week 1-2)
1. Admin: vault_client.createKey("user_pii", version=3)
2. Config: Set write_key_version=3, read_key_versions=[2,3]
3. New data: Encrypted with v3
4. Old data: Still readable with v2

Phase 2: Background Re-Encryption (Week 3-4)
1. Job: SELECT id FROM users WHERE email_key_version = 2
2. For each row:
   ├─> plaintext = decrypt(email, key_v2)
   ├─> encrypted = encrypt(plaintext, key_v3)
   └─> UPDATE users SET email = encrypted WHERE id = ?
3. Progress tracking: "23,456 / 1,000,000 rows (2.3%)"

Phase 3: Deprecation (Week 5)
1. Config: read_key_versions=[3]
2. Admin: vault_client.deprecateKey("user_pii", version=2)
3. Monitoring: Alert if v2 decrypt attempts > 0

Phase 4: Deletion (Week 8+)
1. Admin: vault_client.deleteKey("user_pii", version=2)
2. Audit log: "user_pii_v2 deleted by admin@example.com"
```

**Rollback Safety:**
- Alle Key-Versionen bleiben 30 Tage nach Deprecation verfügbar
- Re-Encryption ist idempotent (kann wiederholt werden)
- Config-Changes sind Feature-Flagged (sofort revertierbar)

### 5.3 KeyProvider Implementations

#### MockKeyProvider (Testing)

```cpp
class MockKeyProvider : public KeyProvider {
private:
    std::map<std::string, std::map<uint32_t, std::vector<uint8_t>>> keys_;
    std::mutex mutex_;
    
public:
    // Generates random 256-bit key
    void createKey(const std::string& key_id, uint32_t version);
    
    // Returns key or throws KeyNotFoundException
    std::vector<uint8_t> getKey(const std::string& key_id, 
                                 uint32_t version) override;
    
    // Not implemented (testing only)
    void rotateKey(const std::string& key_id) override { 
        throw NotImplementedException();
    }
};
```

#### VaultKeyProvider (Production - Interface Only)

```cpp
class VaultKeyProvider : public KeyProvider {
private:
    std::string vault_addr_;      // "https://vault.example.com:8200"
    std::string vault_token_;     // Service principal token
    std::unique_ptr<KeyCache> cache_;
    
public:
    // Authenticates via AppRole or K8s Service Account
    void authenticate();
    
    // GET /v1/secret/data/encryption/{key_id}/v{version}
    std::vector<uint8_t> getKey(const std::string& key_id, 
                                 uint32_t version) override;
    
    // POST /v1/secret/data/encryption/{key_id}/v{next_version}
    void rotateKey(const std::string& key_id) override;
    
    // Cache hit ratio metric
    double getCacheHitRate() const;
};
```

---

## 6. Performance Considerations

### 6.1 Benchmarks (Target)

| Operation | Latency (p50) | Latency (p99) | Throughput |
|-----------|---------------|---------------|------------|
| Encrypt (1KB) | 0.5ms | 2ms | 2000 ops/sec |
| Decrypt (1KB) | 0.5ms | 2ms | 2000 ops/sec |
| Key Lookup (cached) | 0.01ms | 0.1ms | 100k ops/sec |
| Key Lookup (Vault) | 50ms | 200ms | 20 ops/sec |

### 6.2 Optimizations

**1. Key Caching Strategy**
```cpp
class KeyCache {
    struct Entry {
        std::vector<uint8_t> key;
        int64_t expires_at_ms;
        uint64_t access_count;
    };
    
    std::map<std::string, Entry> cache_;  // key_id:version -> Entry
    size_t max_size_ = 1000;
    int64_t ttl_ms_ = 3600000;  // 1 hour
    
    // LRU eviction when cache full
    void evictLRU();
};
```

**2. Batch Encryption**
```cpp
// Instead of:
for (auto& user : users) {
    user.email.encrypt();  // 1000 key lookups!
}

// Use:
auto key = key_provider->getKey("user_pii");
for (auto& user : users) {
    user.email.encryptWithKey(key);  // 1 key lookup
}
```

**3. Lazy Decryption**
```cpp
template<typename T>
class EncryptedField {
    mutable std::optional<T> cached_plaintext_;
    
    T decrypt() const {
        if (!cached_plaintext_) {
            cached_plaintext_ = field_encryption_->decrypt(blob_);
        }
        return *cached_plaintext_;
    }
};
```

### 6.3 Monitoring Metrics

```cpp
// Prometheus metrics
encryption_operations_total{operation="encrypt",key_id="user_pii"} 45234
encryption_operations_total{operation="decrypt",key_id="user_pii"} 128956
encryption_duration_seconds{operation="encrypt",quantile="0.5"} 0.0005
encryption_duration_seconds{operation="encrypt",quantile="0.99"} 0.002
key_cache_hit_rate{key_id="user_pii"} 0.98
key_lookup_errors_total{error="key_not_found"} 12
```

---

## 7. Security Best Practices

### 7.1 Dos

✅ **Use authenticated encryption (AES-GCM)** - Prevents tampering  
✅ **Generate random IV per encryption** - Prevents pattern analysis  
✅ **Cache keys with TTL** - Balance performance and key rotation  
✅ **Log all key accesses** - Audit trail for compliance  
✅ **Encrypt in application layer** - Database never sees plaintext  
✅ **Use separate keys per field type** - Limits blast radius  
✅ **Implement key versioning** - Enables rotation without downtime  

### 7.2 Don'ts

❌ **Never store keys with encrypted data** - Defeats purpose  
❌ **Don't use ECB mode** - Vulnerable to pattern attacks  
❌ **Don't reuse IVs** - Breaks GCM security guarantees  
❌ **Don't skip authentication tags** - Allows tampering  
❌ **Don't log plaintext** - Audit logs must be redacted  
❌ **Don't use hardcoded keys** - Security nightmare  
❌ **Don't encrypt everything** - Performance & operational overhead  

### 7.3 Field Selection Criteria

**Encrypt:**
- Personally Identifiable Information (PII): Email, Phone, SSN, Address
- Financial Data: Credit Cards, Bank Accounts, Salaries
- Health Records: Medical IDs, Diagnoses, Prescriptions
- Secrets: API Keys, Passwords (already hashed), OAuth Tokens

**Don't Encrypt:**
- Primary Keys / Foreign Keys (needed for joins/indexes)
- Timestamps (used in range queries)
- Status Flags (frequent filtering)
- Non-sensitive Metadata (created_by, updated_at)

---

## 8. Example Usage

### 8.1 Schema Definition

```cpp
struct User {
    std::string id;                              // Plaintext (PK)
    std::string username;                        // Plaintext (indexed)
    EncryptedField<std::string> email;          // Encrypted (PII)
    EncryptedField<std::string> phone;          // Encrypted (PII)
    std::string country;                         // Plaintext (indexed)
    int64_t created_at;                         // Plaintext (range queries)
    
    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"username", username},
            {"email", email.toBase64()},        // Serialized encrypted
            {"phone", phone.toBase64()},
            {"country", country},
            {"created_at", created_at}
        };
    }
    
    static User fromJson(const nlohmann::json& j) {
        User u;
        u.id = j["id"];
        u.username = j["username"];
        u.email = EncryptedField<std::string>::fromBase64(j["email"]);
        u.phone = EncryptedField<std::string>::fromBase64(j["phone"]);
        u.country = j["country"];
        u.created_at = j["created_at"];
        return u;
    }
};
```

### 8.2 Write Path

```cpp
// Initialize encryption system
auto key_provider = std::make_shared<VaultKeyProvider>(
    "https://vault.prod.example.com:8200",
    vault_token
);
auto field_encryption = std::make_shared<FieldEncryption>(key_provider);
EncryptedField<std::string>::setFieldEncryption(field_encryption);

// Create user with encrypted fields
User user;
user.id = "u_12345";
user.username = "alice";
user.email = EncryptedField<std::string>("alice@example.com", "user_pii");
user.phone = EncryptedField<std::string>("+1-555-0123", "user_pii");
user.country = "US";
user.created_at = getCurrentTimeMs();

// Store in database
auto json_str = user.toJson().dump();
db->put("d:users:" + user.id, json_str);
```

### 8.3 Read Path

```cpp
// Retrieve from database
auto json_str = db->get("d:users:u_12345");
User user = User::fromJson(nlohmann::json::parse(json_str));

// Access encrypted fields (automatic decryption)
std::string email = user.email.decrypt();  // "alice@example.com"
std::string phone = user.phone.decrypt();  // "+1-555-0123"

// Plaintext fields accessible directly
std::cout << user.username;  // "alice"
```

---

## 9. Testing Strategy

### 9.1 Unit Tests (tests/test_encryption.cpp)

```cpp
TEST(KeyProviderTest, MockProvider_StoresAndRetrievesKeys) {
    auto provider = std::make_shared<MockKeyProvider>();
    provider->createKey("test_key", 1);
    
    auto key = provider->getKey("test_key", 1);
    EXPECT_EQ(key.size(), 32);  // 256 bits
}

TEST(FieldEncryptionTest, EncryptDecrypt_Roundtrip) {
    auto provider = std::make_shared<MockKeyProvider>();
    provider->createKey("test", 1);
    
    FieldEncryption enc(provider);
    std::string plaintext = "sensitive data";
    
    auto blob = enc.encrypt(plaintext, "test");
    auto decrypted = enc.decrypt(blob);
    
    EXPECT_EQ(plaintext, decrypted);
}

TEST(FieldEncryptionTest, Decrypt_WithWrongKey_ThrowsException) {
    auto provider = std::make_shared<MockKeyProvider>();
    provider->createKey("key1", 1);
    provider->createKey("key2", 1);
    
    FieldEncryption enc(provider);
    auto blob = enc.encrypt("data", "key1");
    blob.key_id = "key2";  // Tamper
    
    EXPECT_THROW(enc.decrypt(blob), DecryptionException);
}

TEST(EncryptedFieldTest, StringField_SerializeDeserialize) {
    auto provider = std::make_shared<MockKeyProvider>();
    provider->createKey("test", 1);
    auto enc = std::make_shared<FieldEncryption>(provider);
    EncryptedField<std::string>::setFieldEncryption(enc);
    
    EncryptedField<std::string> field("alice@example.com", "test");
    std::string b64 = field.toBase64();
    
    auto field2 = EncryptedField<std::string>::fromBase64(b64);
    EXPECT_EQ(field2.decrypt(), "alice@example.com");
}

TEST(KeyRotationTest, DecryptWithOldKey_AfterRotation) {
    auto provider = std::make_shared<MockKeyProvider>();
    provider->createKey("key", 1);
    
    FieldEncryption enc(provider);
    auto blob_v1 = enc.encrypt("data", "key");  // Uses v1
    
    provider->createKey("key", 2);  // Rotate to v2
    
    // Old data still decryptable
    EXPECT_EQ(enc.decrypt(blob_v1), "data");
}
```

### 9.2 Integration Tests

```cpp
TEST(EncryptionIntegrationTest, UserCRUD_WithEncryptedFields) {
    auto db = std::make_shared<RocksDBWrapper>(test_config);
    auto provider = std::make_shared<MockKeyProvider>();
    provider->createKey("user_pii", 1);
    // ... setup encryption ...
    
    // Create
    User user = createTestUser();
    db->put("d:users:" + user.id, user.toJson().dump());
    
    // Read
    auto json = db->get("d:users:" + user.id);
    User loaded = User::fromJson(nlohmann::json::parse(json));
    EXPECT_EQ(loaded.email.decrypt(), user.email.decrypt());
    
    // Update
    loaded.phone = EncryptedField<std::string>("+1-999-9999", "user_pii");
    db->put("d:users:" + loaded.id, loaded.toJson().dump());
    
    // Verify
    auto updated = User::fromJson(nlohmann::json::parse(db->get("d:users:" + user.id)));
    EXPECT_EQ(updated.phone.decrypt(), "+1-999-9999");
}
```

### 9.3 Performance Tests

```cpp
TEST(EncryptionPerformanceTest, EncryptDecrypt_1000Operations) {
    auto provider = std::make_shared<MockKeyProvider>();
    provider->createKey("perf", 1);
    FieldEncryption enc(provider);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; i++) {
        auto blob = enc.encrypt("test data " + std::to_string(i), "perf");
        enc.decrypt(blob);
    }
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start
    ).count();
    
    EXPECT_LT(duration, 2000);  // <2ms per operation
}
```

---

## 10. Rollout Plan

### Phase 1: Core Implementation (Week 1)
- [ ] Implement KeyProvider interface + MockKeyProvider
- [ ] Implement FieldEncryption (AES-256-GCM)
- [ ] Unit tests (15+ tests)
- [ ] Deliverable: `include/security/`, `src/security/`, `tests/test_encryption.cpp`

### Phase 2: Template & Integration (Week 2)
- [ ] Implement EncryptedField<T> template
- [ ] Add serialization (toBase64/fromBase64)
- [ ] Integration tests with User struct
- [ ] Deliverable: Working PoC with 2-3 encrypted fields

### Phase 3: VaultKeyProvider Interface (Week 3)
- [ ] Define VaultKeyProvider interface (no implementation)
- [ ] Document Vault API integration requirements
- [ ] Add key cache implementation
- [ ] Deliverable: `include/security/vault_key_provider.h` (header-only)

### Phase 4: Documentation & Review (Week 4)
- [ ] Performance benchmarks
- [ ] Security review
- [ ] Operator documentation (key rotation playbook)
- [ ] Deliverable: This document + ops runbook

---

## 11. Open Questions

1. **Key Storage Location**: Vault on-premise vs. Cloud KMS vs. Both?
   - Recommendation: Start with MockKeyProvider, add Vault interface for production readiness

2. **Searchable Encryption**: Support for encrypted field queries?
   - Recommendation: Phase 2 feature - use deterministic encryption or bloom filters

3. **Bulk Re-Encryption**: 10M+ rows, acceptable downtime?
   - Recommendation: Online migration with dual-write (see Section 5.2)

4. **Compliance**: FIPS 140-2 certification required?
   - Recommendation: Use OpenSSL FIPS module if needed (build-time flag)

---

## 12. References

- [NIST SP 800-38D](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38d.pdf) - GCM Mode Specification
- [RFC 5084](https://tools.ietf.org/html/rfc5084) - AES-GCM and AES-CCM Algorithms
- [HashiCorp Vault Encryption as a Service](https://www.vaultproject.io/docs/secrets/transit)
- [Google Cloud KMS Best Practices](https://cloud.google.com/kms/docs/key-management-best-practices)
- [AWS KMS Developer Guide](https://docs.aws.amazon.com/kms/latest/developerguide/overview.html)
- [Azure Key Vault Documentation](https://docs.microsoft.com/en-us/azure/key-vault/)

---

**Next Steps:**
1. Review and approval of this design document
2. Create GitHub issues for Phase 1-4 tasks
3. Allocate 2-3 week sprint for implementation
4. Security team review before production deployment
