# Security & Encryption E2E Gap Analysis
**Datum:** 10. November 2025  
**Status:** 11/15 TODOs abgeschlossen (73%)

## 🔐 Implementierte Komponenten

### 1. Key Management Infrastructure ✅
- **PKIKeyProvider** (564 Zeilen): 3-Tier KEK→DEK→Field-Key Hierarchie
- **VCCPKIClient** (227 Zeilen): RSA-SHA256 Signierung, PEM Support
- **MockKeyProvider**: Test-Implementation mit In-Memory Storage
- **Key Rotation**: Multi-Version Cache, `rotateKey()`, `rotateDEK()`, `rotateGroupDEK()`

**Status:** ✅ Vollständig implementiert

### 2. Authentication & Authorization ✅
- **JWTValidator** (6/6 Tests): RS256 Signature Validation, JWKS Endpoint
- **Claims Extraction**: sub, email, groups, roles, issuer, expiration, nbf, iat, audience
- **Access Control**: `hasAccess()` für User/Group-basierte Zugriffskontrolle
- **Clock Skew Tolerance**: Konfigurierbar (default 60s)

**Status:** ✅ Vollständig implementiert

### 3. User-Specific Key Derivation ✅
- **HKDF-basiert**: `deriveUserKey(dek, claims, field_name)`
- **Per-User Isolation**: Unterschiedliche User → unterschiedliche Keys
- **Per-Field Granularity**: Separater Key pro Feld
- **Group-DEK**: Multi-Party Access via Group-Kontext

**Tests:** 16/16 bestanden (test_user_key_derivation.cpp)

**Status:** ✅ Vollständig implementiert

### 4. Field-Level Encryption ✅
- **FieldEncryption**: AES-256-GCM mit EncryptedBlob
- **Batch Operations**: `encryptWithKey()`, `decryptWithKey()`
- **Metadata Preservation**: key_id, key_version, algorithm, IV, tag
- **Base64 Serialization**: Storage-ready format

**Status:** ✅ Vollständig implementiert

### 5. Schema-Driven Encryption ✅ (aber Tests fehlen!)
**Implementierung in handlePutEntity() (src/server/http_server.cpp:3483+):**

```cpp
// Schema laden
auto schema_bytes = storage_->get("config:encryption_schema");
auto schema = nlohmann::json::parse(schema_json);

// Pro Collection konfigurierte Felder verschlüsseln
if (schema["collections"][table]["encryption"]["enabled"]) {
    std::string context_type = coll["encryption"].value("context_type", "user");
    std::vector<std::string> fields = coll["encryption"]["fields"];
    
    // JWT-Context extrahieren
    auto auth_ctx = extractAuthContext(req);
    
    // Felder verschlüsseln mit User/Group-DEK
    for (const auto& f : fields) {
        auto user_key = JWTValidator::deriveUserKey(dek, claims, table + "." + f);
        auto encrypted = field_encryption_->encryptWithKey(plain_bytes, key_id, version, user_key);
        entity.setField(f + "_encrypted", encrypted.toBase64());
    }
}
```

**Bereits integriert in:**
- ✅ handlePutEntity (BaseEntity storage)
- ✅ handlePostDocument (Document API)
- ✅ handleVectorUpsert (Vector metadata encryption)

**HTTP Endpunkte:**
- ✅ GET /config/encryption-schema
- ✅ PUT /config/encryption-schema

**Status:** ✅ Implementiert, ⚠️ **GAP: Keine E2E Tests!**

### 6. Graph Edge Encryption ✅
**Tests:** 10/10 bestanden (test_graph_edge_encryption.cpp)

- Plaintext topology preservation (für Traversals)
- Encrypted edge properties (weight, metadata, custom fields)
- User-specific encryption per edge
- Group encryption für shared graphs
- Partial encryption (weight plain, metadata encrypted)

**Status:** ✅ Vollständig getestet

### 7. Query Engine Integration ⚠️ PARTIAL
**Verschlüsselung:** ✅ Implementiert in handlePutEntity  
**Entschlüsselung:** ⚠️ **GAP: Automatische Entschlüsselung fehlt!**

- AQL-Queries geben verschlüsselte Daten zurück
- Client muss manuell entschlüsseln
- Keine transparente Decryption in Query-Results

**Status:** ⚠️ **GAP: Query Result Decryption fehlt**

---

## 🚨 Identifizierte Gaps

### GAP 1: Schema-Based Encryption Tests ⚠️ CRITICAL
**Problem:** Schema-driven encryption ist implementiert, aber:
- Keine E2E Tests für `/config/encryption-schema` API
- Keine Tests für automatische Feldverschlüsselung bei INSERT
- Keine Tests für Context-Type (user vs group)

**Impact:** Hohe Regression-Gefahr bei Änderungen

**Fix Effort:** 2-3 Stunden (Test-Datei erstellen)

### GAP 2: Automatic Query Result Decryption ⚠️ MAJOR
**Problem:** Verschlüsselte Felder werden nicht automatisch entschlüsselt:

```json
// Query Result aktuell:
{
  "name": "Alice",
  "email_encrypted": "base64encodedblob...",
  "ssn_encrypted": "base64encodedblob..."
}

// Query Result erwartet:
{
  "name": "Alice",
  "email": "alice@example.com",  // ← automatisch entschlüsselt
  "ssn": "123-45-6789"
}
```

**Workaround:** Client-Side Decryption mit `?decrypt=true` Parameter

**Fix Effort:** 4-6 Stunden (GET-Handler erweitern)

### GAP 3: Vector Metadata Encryption ❌ TODO #7
**Problem:** Vector-Embeddings sind plaintext, Metadaten unverschlüsselt

**Anforderung aus encryption_strategy.md:**
- Option B: Encrypt metadata (source_text, custom fields)
- Keep embedding plaintext (für HNSW-Index)

**Fix Effort:** 3-4 Stunden (VectorIndexManager erweitern + Tests)

### GAP 4: Audit Log Encryption ❌ TODO #14
**Problem:** SAGA/AUDIT Logs sind plaintext

**Anforderung:**
- Encrypt-then-Sign Pattern
- AES-256-GCM + PKI-Signatur
- LEK (Log Encryption Key) mit täglicher Rotation

**Fix Effort:** 6-8 Stunden (LEKManager + Signature Integration)

### GAP 5: Key Rotation Lazy Re-Encryption ⚠️ PARTIAL
**Implementiert:**
- ✅ Multi-Version Key Cache
- ✅ rotateKey() API
- ✅ Group-DEK rotation

**Fehlt:**
- ❌ Automatische Re-Encryption on Read
- ❌ Prometheus Metrics für Migration Progress
- ❌ Background Job für Bulk Re-Encryption

**Fix Effort:** 8-10 Stunden

### GAP 6: BFS Bug 🐛 CRITICAL
**Problem:** GraphIndexManager::bfs() findet keine Edges nach rebuildTopology()

**Symptom:** BFS gibt nur Start-Node zurück statt vollständiger Traversierung

**Workaround:** Test verwendet outNeighbors() statt bfs()

**Fix Effort:** 1-2 Stunden (Debug + Fix)

---

## 📊 Feature Coverage Matrix

| Feature | Implementation | Tests | E2E | Docs |
|---------|----------------|-------|-----|------|
| PKI Key Management | ✅ 100% | ✅ 6/6 | ✅ | ✅ |
| JWT Validation | ✅ 100% | ✅ 6/6 | ✅ | ✅ |
| User Key Derivation | ✅ 100% | ✅ 16/16 | ✅ | ✅ |
| Field Encryption | ✅ 100% | ✅ 26/26 | ✅ | ✅ |
| Graph Edge Encryption | ✅ 100% | ✅ 10/10 | ✅ | ✅ |
| Schema-Based Encryption | ✅ 90% | ❌ 0/0 | ⚠️ | ✅ |
| Query Decryption | ⚠️ 30% | ❌ | ❌ | ⚠️ |
| Vector Metadata Encryption | ❌ 0% | ❌ | ❌ | ✅ |
| Audit Log Encryption | ❌ 0% | ❌ | ❌ | ✅ |
| Lazy Re-Encryption | ⚠️ 40% | ❌ | ❌ | ✅ |

**Gesamt-Coverage:** 62% implementiert, 45% getestet, 38% E2E-validiert

---

## 🎯 Empfohlene Prioritäten

### Phase 1: Test Gap Closing (1-2 Tage) 🔥
1. **Schema-Based Encryption Tests** erstellen (TODO #8)
2. **BFS Bug** fixen (TODO #16)
3. **Query Decryption Tests** erstellen

### Phase 2: Critical Features (3-4 Tage) ⚠️
4. **Automatic Query Result Decryption** implementieren
5. **Vector Metadata Encryption** (TODO #7)

### Phase 3: Production Readiness (1 Woche) 🚀
6. **Audit Log Encryption** (TODO #14)
7. **Lazy Re-Encryption** fertigstellen
8. **Prometheus Metrics** für Key Rotation
9. **Performance Benchmarks** erstellen

---

## 🔬 Nächste Schritte

### Sofort (heute):
1. ✅ BFS Bug zur TODO-Liste hinzugefügt
2. 🔄 Schema-Based Encryption Tests erstellen
3. 🔄 Security Benchmark erstellen

### Diese Woche:
- Query Result Decryption implementieren
- Vector Metadata Encryption implementieren
- Audit Log Encryption starten

### Nächste Sprint:
- Lazy Re-Encryption Background Job
- Performance Benchmarks erweitern
- Penetration Testing

---

## 📝 Erkenntnisse

**Positiv:**
- Kerninfrastruktur (PKI, JWT, HKDF, FieldEncryption) ist solid und gut getestet
- Graph Edge Encryption zeigt Best-Practice für Domain-Encryption
- Schema-driven approach ist flexibel und erweiterbar

**Verbesserungsbedarf:**
- Test-Coverage für Schema-Encryption erhöhen
- Query-Layer transparent machen (Auto-Decryption)
- Operations-Features (Lazy Re-Encryption, Metrics) finalisieren

**Risiken:**
- BFS Bug könnte auf tiefere Topologie-Probleme hinweisen
- Query Decryption Gap ist Breaking Change für Clients
- Fehlende Metrics erschweren Production Monitoring
