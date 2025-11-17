# Key-Rotation-Strategie für ThemisDB

## 1. Übersicht

Die Key-Rotation-Infrastruktur ist bereits in ThemisDB implementiert und ermöglicht sichere Schlüsselrotation ohne Downtime. Dieses Dokument beschreibt die Strategie und Implementierung.

## 2. Vorhandene Infrastruktur

### 2.1 Komponenten

- **EncryptedBlob.key_version**: Jeder verschlüsselte Blob speichert seine Schlüssel-Version
- **PKIKeyProvider::rotateDEK()**: Erstellt neue DEK-Version, alte bleibt lesbar
- **PKIKeyProvider::rotateGroupDEK(group)**: Rotation für Gruppen-Schlüssel
- **REST-Endpoint**: `POST /keys/rotate?key_id=dek`

### 2.2 Rotation-Ablauf

```cpp
// 1. Neue DEK-Version erstellen
uint32_t new_version = key_provider_->rotateKey("dek");
// → current_dek_version_ = 2
// → dek_cache_[2] = new random 256-bit key
// → alte Version dek_cache_[1] bleibt für Decrypt verfügbar

// 2. Neue Verschlüsselungen nutzen automatisch v2
auto blob = field_encryption_->encryptWithKey(data, "dek", new_version, key_v2);
// blob.key_version = 2

// 3. Alte Daten bleiben mit v1 lesbar
auto plaintext = field_encryption_->decryptWithKey(old_blob, key_v1);
// old_blob.key_version = 1 → nutzt dek_cache_[1]
```

## 3. Lazy Re-Encryption (Write-Back on Read)

### 3.1 Strategie

Anstatt alle verschlüsselten Daten sofort nach Rotation zu re-verschlüsseln, erfolgt die Migration **lazy**:

1. Client liest Daten mit `?decrypt=true`
2. Server erkennt `blob.key_version < current_dek_version_`
3. Server entschlüsselt mit alter Version
4. Server re-verschlüsselt mit neuer Version
5. Server schreibt aktualisierte Entity zurück (**Write-Back**)
6. Response enthält entschlüsselte Daten (transparent für Client)

### 3.2 Implementierung (Geplant)

```cpp
// In handleGetEntity / handleQuery - nach Entschlüsselung
if (blob.key_version < pki->getCurrentDEKVersion()) {
    THEMIS_INFO("Lazy re-encryption: field {} from v{} to v{}", 
                field, blob.key_version, current_version);
    
    // plain_bytes bereits entschlüsselt mit alter Version
    
    // Re-encrypt mit aktueller Version
    uint32_t new_version = pki->getCurrentDEKVersion();
    std::vector<uint8_t> new_raw_key;
    
    if (context_type == "group" && !group_name.empty()) {
        auto new_gdek = pki->getGroupDEK(group_name, new_version);
        new_raw_key = HKDFHelper::derive(new_gdek, {}, "field:" + field, 32);
    } else {
        auto new_dek = key_provider_->getKey("dek", new_version);
        std::vector<uint8_t> salt(user_ctx.begin(), user_ctx.end());
        new_raw_key = HKDFHelper::derive(new_dek, salt, "field:" + field, 32);
    }
    
    auto new_blob = field_encryption_->encryptWithKey(
        plain_bytes, "field:" + field, new_version, new_raw_key
    );
    
    // Write-back (async, non-blocking)
    entity_json[field + "_encrypted"] = new_blob.toJson().dump();
    auto updated_entity = BaseEntity::fromJson(key, entity_json.dump());
    storage_->put(key, updated_entity.serialize());
    
    THEMIS_DEBUG("Field {} re-encrypted: v{} → v{}", field, blob.key_version, new_version);
}
```

### 3.3 Vorteile

✅ **Keine Downtime**: Rotation erfolgt ohne Service-Unterbrechung
✅ **Keine Full-Scan**: Nur gelesene Daten werden migriert (Hot Data zuerst)
✅ **Organisch**: Migration erfolgt bei normaler Nutzung
✅ **Transparenz**: Client merkt nichts von Re-Encryption
✅ **Monitoring**: Key-Version in Blob ermöglicht Progress-Tracking

### 3.4 Monitoring

```bash
# Anzahl noch nicht migrierter Felder
curl "http://localhost:8080/query" -d '{
  "table": "users",
  "predicates": []
}' | jq '.results[] | select(.email_encrypted | contains("\"key_version\":1"))'
```

**Prometheus Metrics (geplant):**
```
# HELP themis_encryption_key_version_distribution Distribution of key versions in use
# TYPE themis_encryption_key_version_distribution gauge
themis_encryption_key_version_distribution{version="1"} 1234
themis_encryption_key_version_distribution{version="2"} 5678
```

## 4. Group-DEK Rotation

### 4.1 Anwendungsfall

**Szenario**: User verlässt HR-Gruppe → Group-DEK rotieren → User kann neue Daten nicht lesen

```bash
# 1. User "alice" verlässt Gruppe "hr_team"
# 2. Admin rotiert Group-DEK
POST /keys/rotate
Content-Type: application/json
{
  "key_id": "group:hr_team"
}

# Response:
{
  "key_id": "group:hr_team",
  "new_version": 2,
  "rotated_at": "2025-11-08T12:00:00Z"
}
```

**Effekt:**
- Neue Verschlüsselungen mit Group-DEK v2
- Alice kann alte Daten (v1) noch lesen (falls noch im JWT)
- Alice kann neue Daten (v2) NICHT lesen (kein Zugriff mehr auf Gruppe)
- Lazy Re-Encryption migriert alte Daten bei Zugriff durch autorisierte User

### 4.2 Implementierung

```cpp
// PKIKeyProvider::rotateGroupDEK bereits implementiert
uint32_t PKIKeyProvider::rotateGroupDEK(const std::string& group_name) {
    std::scoped_lock lk(mu_);
    
    // Load current metadata
    auto meta_key = groupMetadataDbKey(group_name);
    auto meta_str_opt = db_->get(meta_key);
    
    uint32_t old_version = 1;
    if (meta_str_opt.has_value()) {
        std::string meta_str(meta_str_opt->begin(), meta_str_opt->end());
        auto pos = meta_str.find('|');
        if (pos != std::string::npos) {
            old_version = std::stoul(meta_str.substr(0, pos));
        }
    }
    
    uint32_t new_version = old_version + 1;
    
    // Create new Group-DEK
    loadOrCreateGroupDEK(group_name, new_version);
    
    // Update metadata
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    std::string new_meta = std::to_string(new_version) + "|" + std::to_string(ts);
    db_->put(meta_key, toBytes(new_meta));
    
    // Clear cache
    group_dek_cache_.erase(group_name);
    
    return new_version;
}
```

## 5. Best Practices

### 5.1 Rotation-Frequenz

**Empfehlung:**
- **DEK**: Alle 90 Tage (compliance-driven)
- **Group-DEK**: Bei User-Austritt aus Gruppe (event-driven)
- **KEK**: Jährlich bei Zertifikat-Erneuerung (PKI-lifecycle)

### 5.2 Migration-Monitoring

**Vor Rotation:**
```bash
# Check: Alle Daten auf aktueller Version?
curl -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8080/metrics" | grep themis_encryption_key_version

# Falls v1 noch > 10% → Warten oder manuellen Full-Scan triggern
```

**Nach Rotation:**
```bash
# Progress tracking
watch -n 60 'curl -s http://localhost:8080/metrics | grep key_version'
```

### 5.3 Alte Schlüssel löschen

**Nach vollständiger Migration:**
```bash
# Verify: Keine v1 Blobs mehr
SELECT COUNT(*) FROM ... WHERE key_version = 1;  # → 0

# Delete old DEK
DELETE FROM rocksdb WHERE key = 'dek:encrypted:v1';
```

⚠️ **Warnung**: Erst löschen nach 100% Migration!

## 6. Roadmap

| Feature | Status | Priorität |
|---------|--------|-----------|
| DEK Rotation API | ✅ Implementiert | - |
| Group-DEK Rotation API | ✅ Implementiert | - |
| Lazy Re-Encryption | 🟡 Design | Medium |
| Prometheus Metrics | ❌ TODO | Low |
| Automated Migration Job | ❌ TODO | Low |
| Old Key Cleanup API | ❌ TODO | Low |

## 7. Testing

### 7.1 DEK Rotation Test

```cpp
TEST(KeyRotation, DEKRotationPreservesOldData) {
    // 1. Encrypt data with DEK v1
    auto blob_v1 = encrypt("sensitive", "dek", 1, dek_v1);
    
    // 2. Rotate DEK
    uint32_t new_version = provider.rotateKey("dek");
    EXPECT_EQ(new_version, 2);
    
    // 3. Old data still decryptable
    auto decrypted = decrypt(blob_v1, provider.getKey("dek", 1));
    EXPECT_EQ(decrypted, "sensitive");
    
    // 4. New data uses v2
    auto blob_v2 = encrypt("new_data", "dek", new_version, provider.getKey("dek"));
    EXPECT_EQ(blob_v2.key_version, 2);
}
```

### 7.2 Lazy Re-Encryption Test

```cpp
TEST(KeyRotation, LazyReEncryptionOnRead) {
    // Setup: Old encrypted data (v1)
    storage->put("user:1", encrypt_entity_v1());
    
    // Rotate
    provider.rotateKey("dek");
    
    // Read with decrypt=true
    auto resp = GET("/entities/user:1?decrypt=true");
    EXPECT_EQ(resp.status, 200);
    
    // Verify: Data re-encrypted to v2
    auto entity = storage->get("user:1");
    auto blob = parse_encrypted_field(entity, "email");
    EXPECT_EQ(blob.key_version, 2);
}
```

---

**Status**: Infrastruktur ✅ | Lazy Re-Encryption 🟡 Design | Testing ❌ TODO
