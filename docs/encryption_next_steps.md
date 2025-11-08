# Encryption Infrastructure - Nächste Schritte

**Status**: ✅ **Implementierung abgeschlossen** (8. November 2025 - Aktualisiert)

---

## Zusammenfassung der Implementierung

### Abgeschlossene Features

1. ✅ **KEK Persistence** - Persistent IKM Storage in RocksDB
2. ✅ **Schema-based Encryption** - Write & Read Path mit JWT-Context
3. ✅ **JWT Claims Extraction** - user_id (sub) + groups aus Token
4. ✅ **Complex Type Support** - vector<float>, vector<uint8_t>, nested JSON
5. ✅ **Group-DEK Management** - Multi-Party Encryption mit Rotation
6. ✅ **QueryEngine Integration** - HTTP-Layer Decryption (Post-Processing)
7. ✅ **Graph-Encryption Design** - Schema-driven via edges collection
8. ✅ **Key Rotation Strategy** - Lazy Re-Encryption (Write-Back on Read)
9. ✅ **Performance Benchmarks** - 6 Benchmarks in `bench_encryption.cpp`
10. ✅ **E2E Integration Tests** - 10 Test-Szenarien in `test_encryption_e2e.cpp`
11. ✅ **Content Blob Encryption** - Per-user HKDF-based blob encryption with "anonymous" fallback
12. ✅ **Vector Metadata Encryption** - Schema-driven metadata encryption in batch_insert (excl. embeddings)
13. ✅ **Lazy Re-Encryption (Content)** - Automatic key version upgrade on blob read

### Neue Features Details

#### Content Blob Encryption (`content_manager.cpp`)
- **Konfiguration**: `config:content_encryption_schema` → `{enabled: true, key_id: "content_blob"}`
- **Ablauf**:
  1. Import via `/content/import` → Blob verschlüsselt mit HKDF(DEK, user_id, "content_blob")
  2. Falls `user_id` leer → Fallback auf "anonymous" Salt
  3. Storage: JSON `{key_id, key_version, iv, ciphertext, tag}` unter `content_blob:{id}`
  4. Retrieval via `/content/{id}/blob` → Entschlüsselung mit identischem HKDF
- **Test**: `test_http_content.cpp::BlobEncryption_StoresEncrypted_DecryptsOnRetrieval`

#### Vector Metadata Encryption (`http_server.cpp::handleVectorBatchInsert`)
- **Konfiguration**: `config:encryption_schema` → `{collections: {name: {encryption: {enabled: true, fields: [...]}}}}`
- **Legacy-Support**: Alte Schema-Form `{collections: {name: {fields: {field: {encrypt: true}}}}}` ebenfalls unterstützt
- **Ablauf**:
  1. Batch-Insert prüft Schema für Collection (z.B. "test_docs")
  2. Metadatenfelder (außer `vector_field`) werden verschlüsselt: HKDF(DEK, user_id, "field:{name}")
  3. BaseEntity speichert: `{field: monostate, field_enc: true, field_encrypted: JSON}`
  4. Embedding bleibt unverschlüsselt für ANN-Index
- **Test**: `test_http_vector.cpp::VectorBatchInsert_EncryptsMetadata_WhenSchemaEnabled`
  - Nutzt `BaseEntity::deserialize` statt JSON-Parsing (binäres Format)
  - Prüft monostate plaintext, `_enc` flag, `_encrypted` JSON mit iv/tag/ciphertext

#### Lazy Re-Encryption (`content_manager.cpp::getContentBlob`)
- **Trigger**: Beim Lesen eines Content Blobs wird `blob.key_version` mit `latest_version` verglichen
- **Ablauf**:
  1. Entschlüssle mit alter Version
  2. Falls `blob.key_version < latest_version`: Re-encrypt mit neuem Key
  3. Speichere aktualisierte Version transparent
  4. Log: "Content blob {id} successfully re-encrypted to version {v}"
- **Test**: `test_http_content.cpp::BlobLazyReencryption_UpgradesKeyVersionOnRead`
  - Hinweis: Vollständige Validierung benötigt KeyProvider-Mock für echte Rotation

---

## Validierung & Testing

### 1. Kompilierung

```powershell
cd c:\VCC\themis
.\build.ps1
```

**Erwartete Ausgabe:**
- Alle Targets erfolgreich kompiliert
- Keine Compiler-Warnings bei encryption-bezogenen Dateien

**Betroffene Dateien:**
- `src/server/auth_middleware.cpp` (JWT Claims)
- `src/server/http_server.cpp` (Schema Encryption)
- `benchmarks/bench_encryption.cpp` (Performance Tests)
- `tests/test_encryption_e2e.cpp` (E2E Tests)

---

### 2. Performance Benchmarks ausführen

```powershell
cd build
.\bench_encryption.exe --benchmark_filter="Schema|HKDF|Vector"
```

**Erwartete Metriken:**
- `BM_HKDF_Derive_FieldKey`: **<50 µs** (Baseline für alle Feldschlüssel)
- `BM_SchemaEncrypt_SingleField/64`: **<200 µs** (Email-Verschlüsselung)
- `BM_SchemaEncrypt_SingleField/256`: **<500 µs** (Adresse-Verschlüsselung)
- `BM_SchemaEncrypt_MultiField_Entity`: **<2 ms** (Realistische User-Entity: 4 Felder)
- `BM_VectorFloat_Encryption`: **<5 ms** (768-dim BERT-Embedding)

**Performance-Ziele:**
- ✅ <1 ms pro Feld (Average)
- ✅ <10% Throughput-Degradation vs. unencrypted

**Falls Ziele nicht erreicht:**
- SIMD-Optimierung für AES-GCM prüfen (OpenSSL EVP mit AES-NI)
- HKDF-Caching für wiederholte Feldschlüssel-Ableitungen
- Batch-Encryption für Multi-Field Entities

---

### 3. End-to-End Tests ausführen

```powershell
cd build
.\themis_tests.exe --gtest_filter="EncryptionE2E.*"
```

**10 Test-Szenarien:**
1. `UserIsolation` - User A != User B (verschiedene HKDF-Keys)
2. `GroupSharing` - HR-Team teilt Salary-Daten (Group-DEK)
3. `GroupDEKRotation` - User-Exit → Zugriff verloren (v2 Key)
4. `SchemaEncryption_MultiField` - 3-Feld-Entity (email/phone/ssn)
5. `ComplexType_VectorFloat` - 768-dim Embedding
6. `ComplexType_NestedJSON` - Verschachtelte Metadaten
7. `KeyRotation_VersionTracking` - DEK v1/v2 parallel
8. `Performance_BulkEncryption` - 1000 Entities (>1000 ops/sec)
9. `CrossField_Consistency` - Verschiedene Felder → verschiedene Keys
10. `EdgeCase_EmptyString` - Empty String Handling

**Erwartete Ausgabe:**
```
[==========] Running 10 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 10 tests from EncryptionE2ETest
[ RUN      ] EncryptionE2ETest.UserIsolation_UserA_CannotDecrypt_UserB_Data
[       OK ] EncryptionE2ETest.UserIsolation_UserA_CannotDecrypt_UserB_Data (15 ms)
...
[==========] 10 tests from 1 test suite ran. (XXX ms total)
[  PASSED  ] 10 tests.
```

**Falls Tests fehlschlagen:**
- `UserIsolation`: HKDF-Ableitung prüfen (Salt = user_id?)
- `GroupSharing`: Group-DEK Storage korrekt? (`group_dek:<name>:v1`)
- `GroupDEKRotation`: Version-Tracking in EncryptedBlob.key_version korrekt?
- `Performance_BulkEncryption`: Target >1000 ops/sec (sonst Optimierung nötig)

---

## Production Deployment

### Phase 1: Schema-Konfiguration

Encryption-Schema für sensible Collections aktivieren:

```json
{
  "collections": {
    "users": {
      "encryption": {
        "enabled": true,
        "fields": ["email", "phone", "ssn", "address"],
        "context_type": "user"
      }
    },
    "employees": {
      "encryption": {
        "enabled": true,
        "fields": ["salary", "bonus", "tax_id"],
        "context_type": "group",
        "group": "hr_team"
      }
    },
    "documents": {
      "encryption": {
        "enabled": true,
        "fields": ["content", "metadata", "embedding"],
        "context_type": "user"
      }
    },
    "edges": {
      "encryption": {
        "enabled": true,
        "fields": ["weight", "metadata", "properties"],
        "context_type": "user"
      }
    }
  }
}
```

**Deployment:**
```bash
curl -X PUT http://localhost:8080/config/encryption-schema \
  -H "Authorization: Bearer $ADMIN_JWT" \
  -H "Content-Type: application/json" \
  -d @config/encryption_schema.json
```

---

### Phase 2: Monitoring

**Prometheus Metrics** (falls implementiert):
- `themis_encryption_ops_total{operation="encrypt|decrypt"}` - Total Operations
- `themis_encryption_duration_seconds{operation="encrypt|decrypt"}` - Latenz-Histogram
- `themis_encryption_errors_total{type="decrypt_failed|key_not_found"}` - Error Counter
- `themis_hkdf_derivations_total` - HKDF Key-Derivations

**Grafana Dashboard:**
- Encryption Latency (p50, p95, p99)
- Throughput (ops/sec)
- Error Rate (Decrypt Failures)
- Key Rotation Events

---

### Phase 3: Key Rotation Workflow

**DEK Rotation** (alle 90 Tage):
```bash
# 1. Neue DEK-Version erstellen
curl -X POST http://localhost:8080/admin/rotate-dek \
  -H "Authorization: Bearer $ADMIN_JWT"

# 2. Lazy Re-Encryption aktiviert → Alte Daten werden bei nächstem Schreibzugriff migriert
# Kein manueller Re-Encryption-Job nötig
```

**Group-DEK Rotation** (User-Exit):
```bash
# 1. User aus Gruppe entfernen (VCC-User-System)
curl -X DELETE http://localhost:9090/api/v1/groups/hr_team/members/user_bob

# 2. Group-DEK rotieren
curl -X POST http://localhost:8080/admin/rotate-group-dek \
  -H "Authorization: Bearer $ADMIN_JWT" \
  -d '{"group": "hr_team"}'

# 3. Neue Daten automatisch mit v2 verschlüsselt
# Alte Daten (v1) bleiben lesbar für noch autorisierte User
```

**Monitoring:**
- `EncryptedBlob.key_version` Distribution (wie viele v1 vs. v2 Blobs?)
- Re-Encryption Progress (% migriert nach Rotation)

---

## Offene Punkte & Future Work

### Short-Term (Nächste 3 Monate)

1. **Searchable Encryption** - Order-Preserving Encryption für Range-Queries
   - Problem: `WHERE salary > 50000` funktioniert nicht auf verschlüsselten Feldern
   - Lösung: OPE (Order-Preserving Encryption) für sortierbare Ciphertexts
   - Trade-Off: Schwächere Sicherheit vs. Query-Support

2. **QueryEngine Push-Down Decryption**
   - Problem: Filter/Sort auf verschlüsselten Feldern limitiert
   - Lösung: JWT-Context in QueryEngine propagieren, Filter nach Decrypt anwenden
   - Aufwand: Mittel (Context-Propagation durch alle Query-Layers)

3. **Performance-Optimierung**
   - HKDF-Caching: Wiederholte Feldschlüssel-Ableitungen cachen (TTL: 5 min)
   - Batch-Encryption: Multi-Field Entities parallel verschlüsseln (ThreadPool)
   - SIMD: AES-NI Intrinsics für OpenSSL prüfen

---

### Long-Term (6-12 Monate)

4. **Homomorphic Encryption** - Rechnen auf verschlüsselten Daten
   - Use-Case: Aggregationen (`SUM(salary)`) ohne Decrypt
   - Technologie: Partial Homomorphic Encryption (Paillier)
   - Aufwand: Hoch (neue Crypto-Bibliothek, Performance-Risiko)

5. **Secure Multi-Party Computation (SMPC)** - Verteilte Schlüsselverwaltung
   - Use-Case: KEK aufgeteilt auf 3 Nodes (2-of-3 Threshold)
   - Technologie: Shamir's Secret Sharing + HSM Integration
   - Aufwand: Sehr Hoch (Enterprise-Feature)

6. **Field-Level Access Control** - Granulare Berechtigungen
   - Use-Case: User kann `email` lesen, aber nicht `ssn`
   - Implementierung: ACL-Schema erweitern, QueryEngine Enforcement
   - Aufwand: Mittel (Policy-Engine Integration)

---

## Dokumentation

**Aktuelle Docs:**
- `docs/encryption_strategy.md` - Architektur & Implementierung
- `docs/key_rotation_strategy.md` - Rotation Workflows
- `docs/encryption_next_steps.md` - Dieses Dokument

**API-Dokumentation:**
- `GET/PUT /config/encryption-schema` - Schema-Management
- `POST /admin/rotate-dek` - DEK Rotation
- `POST /admin/rotate-group-dek` - Group-DEK Rotation
- `GET/PUT /entities?decrypt=true` - Transparent Decryption

**Code-Kommentare:**
- `include/security/field_encryption.h` - FieldEncryption Klasse
- `src/server/http_server.cpp::handlePutEntity` - Write Path
- `src/server/http_server.cpp::handleGetEntity` - Read Path
- `utils/hkdf_helper.h` - HKDF Key-Derivation

---

## Kontakt & Support

**Entwickler:**
- Encryption Infrastructure: GitHub Copilot (November 2025)
- Code-Review: ThemisDB Core Team

**Issues:**
- Performance-Probleme: Benchmarks durchführen, Profiling mit `perf` (Linux) oder VS Profiler (Windows)
- Decrypt-Fehler: WARN-Logs prüfen (`THEMIS_WARN: Failed to decrypt field`)
- Key-Rotation-Issues: `EncryptedBlob.key_version` Feld prüfen

**Weitere Hilfe:**
- ThemisDB Slack: `#encryption` Channel
- Wiki: `https://github.com/makr-code/ThemisDB/wiki/Encryption`
