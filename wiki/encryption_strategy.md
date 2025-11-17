# Verschlüsselungsstrategie für ThemisDB (E2E On-Premise)

## Executive Summary

**Ziel:** End-to-End-Verschlüsselung für sensible Daten in ThemisDB mit on-premise PKI-basiertem Key-Management unter Nutzung des VCC-PKI-Systems (`c:\vcc\pki`) und VCC-User-Systems (`c:\vcc\user`).

**Kernprinzipien:**
- 🔓 **Metadaten sichtbar**: Indexstrukturen, PKs, Timestamps, Kategorien bleiben unverschlüsselt für Query-Performance
- 🔐 **Daten verschlüsselt**: Graph-Properties, Relational-Fields, Content-Blobs, Vector-Embeddings verschlüsselt at-rest
- 🔑 **PKI-basiert**: Integration mit VCC-PKI für Zertifikat-basierte Schlüsselableitung
- 👤 **User-Context**: Per-User-Verschlüsselung via VCC-User-System (JWT-Propagation)
- 🚫 **Zero-Knowledge**: Ohne korrekten Schlüssel keine Datenrekonstruktion möglich

---

## 1. Architektur-Übersicht

### 1.1 Threat Model

**Was wird geschützt:**
- Graph: Edge-Properties (z.B. `weight`, `metadata`, benutzerdefinierte Felder)
- Relational: Sensitive Spalten (z.B. `email`, `phone`, `address`, Custom-Fields)
- Content: Binärblobs (PDF, DOCX, Bilder mit EXIF, Audio mit Metadaten)
- Vector: Embeddings (768-dim float32, rekonstruierbar → Originaldokument)

**Was NICHT verschlüsselt wird (Performance/Query):**
- Primary Keys, Foreign Keys
- Index-Keys (SecondaryIndex, CompositeIndex)
- Timestamps (`created_at`, `modified_at`)
- Kategorien, Tags, MIME-Types
- Vector-Dimensionen (für Index-Initialisierung)
- Graph-Topologie (Knoten-IDs, Kanten-Richtung, Label)

**Angriffszenarien:**
1. ❌ Disk-Theft: Festplatte gestohlen → verschlüsselte Daten unlesbar
2. ❌ Backup-Leak: Backup-Datei im Netz → ohne Schlüssel nutzlos
3. ❌ Insider-Threat: DB-Admin ohne User-Key kann Daten nicht lesen
4. ❌ Memory-Dump: Angreifer kann nur kurzlebige In-Memory-Schlüssel extrahieren

---

## 2. PKI-Integration (VCC-PKI)

### 2.1 VCC-PKI System (`c:\vcc\pki`)

**Vorhandene Infrastruktur:**
- **Root CA**: 10 Jahre Gültigkeit, 4096-bit RSA
- **Intermediate CA**: 5 Jahre, signiert Service-Zertifikate
- **Service Certificates**: Pro Service (veritas, covina, clara, themis)
- **REST API**: `https://localhost:8443/api/v1` (FastAPI)
- **mTLS**: Client-Zertifikat-basierte Authentifizierung (geplant)

**Nutzung für ThemisDB:**
```
Root CA (VCC Root CA)
 └── Intermediate CA (VCC Intermediate CA)
      ├── Service Cert: themis-db.vcc.local
      └── Data Encryption Key (DEK) Wrapping Cert
```

### 2.2 Key-Hierarchie

**3-Tier Key-Architektur:**

```
┌─────────────────────────────────────────┐
└──────────────┬──────────────────────────┘
               │ verschlüsselt
               ▼
│  DEK (Data Encryption Key)              │
│  - AES-256-GCM Master-Key               │
│  - Pro Datenbank/Tenant                 │
│  - Gespeichert verschlüsselt in DB      │
│  - Rotierbar ohne Daten-Re-Encryption   │
└──────────────┬──────────────────────────┘
│  - Aus JWT-Token + DEK abgeleitet       │
│  - HKDF mit User-ID als Context         │
│  - Ephemeral (nur In-Memory)            │
└─────────────────────────────────────────┘
               │ verschlüsselt
               ▼
        [Sensitive Data]
```

**Key-Derivation:**
```cpp
// 1. KEK aus PKI-Zertifikat (einmalig beim Start)
KEK = HKDF-SHA256(
    info="KEK for ThemisDB instance"
)

// 2. DEK laden/erstellen (beim DB-Init)
encrypted_DEK = storage->get("config:dek_encrypted")
if (!encrypted_DEK) {
    DEK = random_bytes(32)  // AES-256
    encrypted_DEK = AES-GCM-encrypt(DEK, KEK, nonce=random(12))
    storage->put("config:dek_encrypted", encrypted_DEK)
} else {
    DEK = AES-GCM-decrypt(encrypted_DEK, KEK)
}

// 3. User-spezifischer Field-Key (bei jedem Request)
user_id = extract_from_jwt(request.headers["Authorization"])
field_key = HKDF-SHA256(
    DEK,
    salt=user_id,
    info="field-encryption:" + field_name
)
---
## 3. User-Context-Integration (VCC-User)

### 3.1 VCC-User System (`c:\vcc\user`)

**Identity Propagation:**
- **Keycloak**: OIDC Identity Provider mit AD-Föderation
- **JWT-Token**: Durchgängige Propagation durch alle Services
- **Zero-Trust**: Jeder Service validiert JWT unabhängig

**JWT-Claims für ThemisDB:**
```json
{
  "sub": "user123",
  "email": "alice@vcc.local",
  "groups": ["data_scientists", "hr_team"],
  "roles": ["data_reader", "pii_access"],
  "iss": "https://keycloak.vcc.local/realms/vcc",
  "exp": 1730000000
}
```

### 3.2 Access-Control-basierte Verschlüsselung

**Idee:** Verschiedene User-Gruppen haben verschiedene Verschlüsselungskontext → Multi-User-Encryption

**Beispiel:**
```cpp
// In ThemisDB HTTP-Handler
std::string jwt_token = request.get_header("Authorization");
auto claims = jwt_validator_.parse_and_validate(jwt_token);
std::string user_id = claims["sub"];
std::vector<std::string> groups = claims["groups"];

// Ableitung eines gruppenspezifischen Schlüssels
std::string encryption_context = user_id; // oder group[0] für Gruppenschlüssel
auto field_key = key_provider_->deriveUserKey(dek_, encryption_context, field_name);

// Verschlüsseln mit User-Context
EncryptedBlob blob = field_encryption_->encrypt(sensitive_data, field_key);
```

**Vorteil:**
- 👤 **User-Isolation**: User A kann Daten von User B nicht entschlüsseln
- 👥 **Gruppenschlüssel**: HR-Gruppe verschlüsselt mit `group=hr_team` → alle HR-Mitglieder können lesen
- 🔄 **Key-Rotation**: Bei User-Austritt → Keys ungültig ohne Daten-Re-Encryption

---

## 4. Datenmodell-spezifische Verschlüsselung

### 4.1 Graph (Property Graph)

**Was verschlüsseln:**
```cpp
// BaseEntity für Graph-Edge
{
  "pk": "graph:edge:alice->bob",           // PLAIN (Index)
  "from": "alice",                          // PLAIN (Topologie)
  "to": "bob",                              // PLAIN (Topologie)
  "label": "KNOWS",                         // PLAIN (Query)
  "created_at": 1730000000,                 // PLAIN (Index)
  "weight": 0.95,                           // 🔐 ENCRYPTED
  "metadata": {                             // 🔐 ENCRYPTED (ganzes Objekt)
    "since": "2020-01-01",
    "context": "university"
  }
}
```

**Implementierung:**
```cpp
// In GraphIndexManager::addEdge()
BaseEntity::FieldMap fields;
fields["pk"] = edge.getPrimaryKey();
fields["from"] = edge.getFieldAsString("from");
fields["to"] = edge.getFieldAsString("to");
fields["label"] = edge.getFieldAsString("label");
fields["created_at"] = edge.getFieldAsInt("created_at");

// Sensitive Felder verschlüsseln
if (auto weight = edge.getField("weight")) {
    std::string user_key = deriveUserKey(jwt_context, "edge.weight");
    auto encrypted = field_enc_->encrypt(serializeValue(*weight), user_key);
    fields["weight_encrypted"] = encrypted.toBase64();
}
if (auto meta = edge.getField("metadata")) {
    std::string user_key = deriveUserKey(jwt_context, "edge.metadata");
    auto encrypted = field_enc_->encrypt(serializeValue(*meta), user_key);
    fields["metadata_encrypted"] = encrypted.toBase64();
}

BaseEntity encrypted_edge = BaseEntity::fromFields(pk, fields);
storage_->put(key, encrypted_edge.serialize());
```

### 4.2 Relational (BaseEntity Fields)

**Schema-basierte Verschlüsselung:**
```json
{
  "schema": {
    "users": {
      "fields": {
        "id": { "type": "string", "encrypted": false, "indexed": true },
        "email": { "type": "string", "encrypted": true, "indexed": false },
        "name": { "type": "string", "encrypted": false, "indexed": true },
        "ssn": { "type": "string", "encrypted": true, "indexed": false },
        "salary": { "type": "int64", "encrypted": true, "indexed": false }
      }
    }
  }
}
```

**Automatische Verschlüsselung:**
```cpp
// In QueryEngine beim INSERT
auto schema = loadSchema("users");
for (const auto& [field, config] : schema.fields) {
    if (config.encrypted) {
        auto value = entity.getField(field);
        auto user_key = deriveUserKey(jwt, "users." + field);
        auto enc = field_enc_->encrypt(serializeValue(*value), user_key);
        entity.setField(field + "_encrypted", enc.toBase64());
        entity.setField(field, std::monostate{}); // clear plaintext
    }
}
```

### 4.3 Content (Binärblobs)

**Chunk-Level-Verschlüsselung:**
```cpp
// In ContentManager::importContent()
if (config.encrypt_blobs && blob.has_value()) {
    std::string user_key = deriveUserKey(jwt, "content.blob:" + meta.id);
    auto encrypted = field_enc_->encrypt(*blob, user_key);
    
    // Meta-Flag setzen
    meta.encrypted = true;
    meta.encryption_type = "aes-256-gcm";
    meta.encryption_context = jwt_claims["sub"]; // oder group
    
    storage_->put("content_blob:" + meta.id, encrypted.toBase64());
}

// In ContentManager::getContentBlob()
if (meta.encrypted) {
    // User-Context validieren
    if (jwt_claims["sub"] != meta.encryption_context && 
        !hasGroupAccess(jwt_claims, meta.encryption_context)) {
        throw UnauthorizedException("No access to encrypted content");
    }
    
    auto user_key = deriveUserKey(jwt, "content.blob:" + meta.id);
    auto decrypted = field_enc_->decrypt(encrypted_blob, user_key);
    return decrypted;
}
```

### 4.4 Vector (Embeddings)

**Trade-off: Verschlüsselung vs. Nearest-Neighbor-Search**

**Problem:**
- ANN-Search (HNSW) benötigt float32-Vektoren im Klartext
- Verschlüsselte Vektoren → keine Distanz-Berechnung möglich

**Lösungen:**

#### Option A: Keine Vektor-Verschlüsselung (Default)
```cpp
// Vektoren bleiben unverschlüsselt für ANN
// Zugriff nur über authorizierte API-Calls
// Audit-Logging aller Vector-Queries
```

**Vorteil:** ✅ Volle ANN-Performance  
**Nachteil:** ⚠️ Vektoren at-rest rekonstruierbar

#### Option B: Encrypt-then-Search (Metadata-only)
```cpp
// Nur Vektor-Metadaten verschlüsseln
BaseEntity vector_entity;
vector_entity.setField("pk", pk);                    // PLAIN
vector_entity.setField("embedding", embedding);      // PLAIN (für HNSW)
vector_entity.setField("source_text_encrypted", enc_text);  // 🔐 ENCRYPTED
vector_entity.setField("metadata_encrypted", enc_meta);     // 🔐 ENCRYPTED
```

**Vorteil:** ✅ ANN funktioniert, Quelltext geschützt  
**Nachteil:** ⚠️ Embedding selbst im Klartext

#### Option C: Homomorphic Encryption (Future)
```cpp
// Fully Homomorphic Encryption (FHE) für Distanz-Berechnung
// Aktuell nicht produktionsreif (100-1000x Slowdown)
```

**Empfehlung:** Start mit **Option B** (Metadata-Verschlüsselung), später **Option C** evaluieren

---

## 5. Implementierungsplan

### 5.1 Phase 1: PKI-Integration (Week 1)

**Tasks:**
1. ✅ Bereits vorhanden: `FieldEncryption`, `KeyProvider`, `EncryptedBlob`
2. ❌ Neuer `PKIKeyProvider`:
   ```cpp
   class PKIKeyProvider : public KeyProvider {
   public:
       PKIKeyProvider(std::string cert_path, std::string key_path);
       std::vector<uint8_t> getKey(const std::string& key_id, uint32_t version) override;
   private:
       std::vector<uint8_t> kek_;  // aus Zertifikat
       std::vector<uint8_t> dek_;  // aus verschlüsseltem DB-Key
   };
   ```

3. ❌ VCC-PKI REST-Client:
   ```cpp
   class VCCPKIClient {
   public:
       // Zertifikat von PKI-Server holen
       Certificate requestServiceCertificate(std::string service_id);
       void verifyCertificateChain(Certificate cert);
   };
   ```

### 5.2 Phase 2: User-Context (Week 2)

**Tasks:**
1. ❌ JWT-Validator für Keycloak-Token:
   ```cpp
   class JWTValidator {
   public:
       nlohmann::json parseAndValidate(const std::string& token);
   private:
       std::string jwks_url_;  // Keycloak JWKS-Endpoint
   };
   ```

2. ❌ User-Key-Derivation:
   ```cpp
   std::vector<uint8_t> deriveUserKey(
       const std::vector<uint8_t>& dek,
       const std::string& user_id,
       const std::string& field_name
   ) {
       return HKDF(dek, user_id, "field:" + field_name);
   }
   ```

### 5.3 Phase 3: Storage-Layer-Integration (Week 3)

**Tasks:**
1. ❌ GraphIndexManager: Verschlüssele `weight`, `metadata`
2. ❌ ContentManager: Verschlüssele Blobs (bereits vorbereitet mit `meta.encrypted`)
3. ❌ VectorIndexManager: Verschlüssele Vektor-Metadaten (Option B)
4. ❌ QueryEngine: Schema-basierte Auto-Verschlüsselung

### 5.4 Phase 4: Testing & Audit (Week 4)

**Tests:**
- Unit-Tests: Encrypt/Decrypt-Roundtrip für alle Datentypen
- Integration: Multi-User-Szenarien (User A kann Daten von User B nicht lesen)
- Performance: Overhead-Messung (Encrypt: ~0.5ms/KB, Decrypt: ~0.5ms/KB)
- Security: Pen-Test mit gestohlenem Backup ohne Keys

---

## 6. Konfiguration

### 6.1 DB-Config (`config:encryption` in RocksDB)

```json
{
  "enabled": true,
  "algorithm": "aes-256-gcm",
  "key_provider": "pki",
  "pki": {
    "server_url": "https://localhost:8443/api/v1",
    "service_id": "themis-db",
    "cert_path": "/etc/themis/certs/themis-db.pem",
    "key_path": "/etc/themis/certs/themis-db-key.pem"
  },
  "user_context": {
    "enabled": true,
    "jwt_issuer": "https://keycloak.vcc.local/realms/vcc",
    "jwks_url": "https://keycloak.vcc.local/realms/vcc/protocol/openid-connect/certs"
  },
  "encrypt_fields": {
    "graph_edge_properties": true,
    "content_blobs": true,
    "vector_metadata": true,
    "relational_sensitive": true
  }
}
```

### 6.2 Schema-Definition (per Collection/Object)

```json
{
  "collections": {
    "users": {
      "encryption": {
        "enabled": true,
        "fields": ["email", "phone", "ssn", "address"],
        "context_type": "user"  // per-user oder "group"
      }
    },
    "documents": {
      "encryption": {
        "enabled": true,
        "fields": ["content_blob"],
        "context_type": "group",
        "allowed_groups": ["legal_team", "executives"]
      }
    }
  }
}
```

---

## 7. Security Best-Practices

### 7.1 Key-Management

✅ **DO:**
- KEK aus PKI-Zertifikat ableiten (Hardware-backed wenn möglich)
- DEK verschlüsselt in DB speichern
- User-Keys nur in-memory halten (ephemeral)
- Key-Rotation alle 90 Tage (DEK), Zertifikat-Erneuerung jährlich

❌ **DON'T:**
- Keys im Klartext in Config-Dateien
- Hardcoded Keys im Source-Code
- DEK unverschlüsselt in Environment Variables

### 7.2 Audit-Logging

**Encrypt-then-Sign für sensible Logs (SAGA, AUDIT):**

- Canonical JSON erzeugen (stabile Key-Order, UTF-8)
- Mit täglichem LEK (Log Encryption Key) via AES-256-GCM verschlüsseln
- Hash über den Ciphertext bilden (SHA-256)
- PKI-Signatur über den Ciphertext-Hash (VCC-PKI)
- Persistieren: Ciphertext + iv + tag + lek_id + Signatur + Zert-Metadaten
- Optional redaktierte Kurzform in stdout/file loggen (kein Klartext)

Konfiguration siehe Governance (`config/governance.yaml`):
- `saga_signing.encrypt_then_sign: true`
- `saga_signing.categories.encrypt_before_sign: [SAGA, AUDIT]`
- `log_encryption.encrypt_categories: [SAGA, AUDIT]`
- `log_encryption.aad_fields: [log_id, category, timestamp]`

LEK-Handling (täglich rotierend):
1) KEK aus PKI-Zertifikat per HKDF → KEK(date)
2) Zufälliger 256-bit LEK generiert → LEK(date)
3) LEK verschlüsselt mit KEK(date) in RocksDB abgelegt

**Log jede Verschlüsselungs-/Entschlüsselungs-Operation:**
```json
{
  "timestamp": "2025-10-31T10:00:00Z",
  "operation": "decrypt",
  "user_id": "user123",
  "field": "content.blob:abc123",
  "success": true,
  "ip": "192.168.1.50"
}
```

### 7.3 Zero-Knowledge-Compliance

**Verification:**
```bash
# Backup ohne Keys erstellen
rocksdb_dump --db=/data/themis > backup.sst

# Ohne DEK: Daten unlesbar
strings backup.sst | grep "alice@example.com"  # → Gibberish

# Mit DEK: Daten lesbar
themis-decrypt --dek-file=dek.bin --input=backup.sst | grep "alice@"  # → alice@example.com
```

---

## 8. Zusammenfassung

| Feature | Status | Technologie | Nutzen |
|---------|--------|-------------|--------|
| **PKI-Integration** | ❌ TODO | VCC-PKI (c:\vcc\pki) | Zertifikat-basierte KEK |
| **User-Context** | ❌ TODO | VCC-User JWT (c:\vcc\user) | Per-User-Verschlüsselung |
| **Graph-Encryption** | ❌ TODO | AES-256-GCM | Edge-Properties geschützt |
| **Content-Encryption** | 🟡 PARTIAL | AES-256-GCM | Blob-Verschlüsselung vorbereitet |
| **Vector-Metadata-Enc** | ❌ TODO | AES-256-GCM | Quelltext geschützt, ANN nutzbar |
| **Schema-based Auto-Enc** | ❌ TODO | Config-driven | Deklarative Verschlüsselung |
| **Audit-Logging** | ❌ TODO | Encrypt-then-Sign (AES-256-GCM + PKI) | Compliance & Forensics |

**Nächste Schritte:**
1. Implementiere `PKIKeyProvider` mit VCC-PKI REST-Client
2. Integriere JWT-Validator für Keycloak-Token
3. Erweitere `GraphIndexManager`, `ContentManager`, `VectorIndexManager`
4. Teste Multi-User-Szenarien mit verschiedenen JWT-Claims
5. Performance-Benchmarks mit verschlüsselten Daten
