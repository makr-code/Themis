# Compliance & Governance-Strategie für ThemisDB

## Executive Summary

**Ziel:** Umfassende Compliance- und Governance-Architektur für ThemisDB mit PKI-signiertem Audit-Trail, DSGVO-by-Design, automatischer PII-Erkennung und konfigurierbaren Governance-Policies.

**Kernprinzipien:**
- 📝 **Unveränderlicher Audit-Trail**: SAGA-Log regelmäßig PKI-signiert, Log-Keys sicher gespeichert
- 🔐 **DSGVO by Design**: Automatische PII-Erkennung, UUID-Ersetzung, Original-Blob zugriffsbeschränkt
- ⚖️ **Regulatory Compliance**: GDPR/DSGVO, HIPAA, BSI C5, SOC2 Unterstützung
- 🎯 **Policy-Driven**: Alle Governance-Regeln in YAML/JSON konfigurierbar
- 🔍 **Transparenz**: Vollständige Nachvollziehbarkeit aller Datenoperationen

---

## 1. Architektur-Übersicht

### 1.1 Compliance-Komponenten

```
┌─────────────────────────────────────────────────────────────┐
│                    ThemisDB Compliance Layer                 │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ SAGA Logger  │  │ PII Detector │  │ Retention    │      │
│  │ + PKI Sign   │  │ + Anonymizer │  │ Manager      │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                  │                  │              │
│         ▼                  ▼                  ▼              │
│  ┌──────────────────────────────────────────────────┐       │
│  │         Governance Policy Engine (GPE)            │       │
│  │   - Policy Validation & Enforcement               │       │
│  │   - Config-Driven Rules (YAML/JSON)              │       │
│  │   - Audit Trail Generation                        │       │
│  └──────────────────────────────────────────────────┘       │
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ VCC-PKI      │  │ VCC-User     │  │ Encryption   │      │
│  │ Integration  │  │ Integration  │  │ Layer        │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Threat Model (Compliance-Perspektive)

**Was wird geschützt:**
- ✅ **Audit-Integrität**: SAGA-Logs unveränderlich durch PKI-Signaturen
- ✅ **PII-Schutz**: Automatische Erkennung und Anonymisierung sensibler Daten
- ✅ **Rechtssicherheit**: Vollständige Nachvollziehbarkeit aller Operationen
- ✅ **Datensouveränität**: On-Premise mit VCC-PKI/User Integration

**Compliance-Szenarien:**
1. ✅ **DSGVO Artikel 17 (Recht auf Vergessenwerden)**: PII durch UUID ersetzt, Original-Zugriff widerrufbar
2. ✅ **DSGVO Artikel 30 (Verarbeitungsverzeichnis)**: SAGA-Log als lückenloser Audit-Trail
3. ✅ **DSGVO Artikel 32 (Datensicherheit)**: Verschlüsselung + PKI-Signaturen
4. ✅ **HIPAA Audit Controls**: Strukturierte JSON-Logs mit medizinischen Daten-Tags
5. ✅ **BSI C5 Logging**: Zeitstempel, User-ID, Operation, Result in jedem Log-Entry

---

## 2. SAGA-Log PKI-Signierung

### 2.1 Konzept

**Problem:** SAGA-Logs (Transaktions-Kompensationen) müssen manipulationssicher sein für rechtliche Nachweisbarkeit.

**Lösung:** Regelmäßige PKI-Signierung von Log-Batches mit VCC-PKI Intermediate CA.

```cpp
// Beispiel: SAGA-Log Entry (vor Signierung)
{
  "saga_id": "tx_20251031_123456_789",
  "timestamp": "2025-10-31T14:23:45.123Z",
  "operation": "vectorAdd",
  "entity_pk": "doc_12345",
  "user_id": "user_alice@example.com",
  "compensated": false,
  "duration_ms": 42,
  "status": "success"
}
```

### 2.2 Signierungsprozess

**Workflow:**
1. **Batch-Collection**: Alle SAGA-Steps seit letzter Signierung sammeln (z.B. 1000 Einträge oder 5 Minuten)
2. **Canonical JSON**: Sortierte Keys, UTF-8, keine Whitespace → deterministischer Klartext
3. **AES-Verschlüsselung (LEK)**: Klartext-Batch mit täglichem LEK per AES-256-GCM verschlüsseln → Ciphertext + IV + Tag
4. **SHA-256 Hash (über Ciphertext)**: `hash = SHA256(ciphertext_batch)`  ← Encrypt-then-Hash
5. **PKI-Signierung (Ciphertext-Hash)**: VCC-PKI REST API aufrufen → `POST /api/v1/sign`
   ```json
   {
     "service_id": "themis-db",
     "data_hash": "abcdef123456...",
     "signature_type": "RSA-SHA256"
   }
   ```
6. **Signatur speichern**: In RocksDB unter `saga:signature:<timestamp>`
   ```json
   {
     "batch_id": "batch_20251031_142300",
     "log_entries": 1000,
     "first_saga_id": "tx_...",
     "last_saga_id": "tx_...",
     "hash": "abcdef...",              
     "signature": "MIIBIjANBg...",
     "cert_serial": "03:A5:B2:...",
     "signed_at": "2025-10-31T14:25:00Z",
     "signer": "themis-service-cert",
     "enc": {
       "alg": "AES-256-GCM",
       "lek_id": "lek:20251031",
       "iv": "base64(...)",
       "tag": "base64(...)"
     }
   }
   ```

**Verifizierung (Ciphertext zuerst):**
```cpp
bool verifySAGABatch(const std::string& batch_id) {
    // 1. Lade Signatur-Metadata
    auto sig_data = db_.get("saga:signature:" + batch_id);
    
    // 2. Lade gespeicherten Ciphertext-Batch (ohne Entschlüsselung)
    std::string ciphertext = loadEncryptedBatch(sig_data["batch_id"]);
    
    // 3. Hash über Ciphertext bilden
    std::string hash = sha256(ciphertext);
    
    // 4. VCC-PKI Signature Verify (Ciphertext-Hash)
    return vcc_pki_client_->verify(
        hash, 
        sig_data["signature"], 
        sig_data["cert_serial"]
    );
}
```

### 2.3 Log-Key Management

**Problem:** Log-Einträge können sensitive Daten enthalten (vor Anonymisierung) → Verschlüsselung erforderlich.

**Lösung:** Separater Log-Encryption-Key (LEK) pro Zeitperiode (z.B. täglich).

```yaml
# config/governance.yaml
saga_log:
  signature:
    enabled: true
    batch_size: 1000
    batch_interval_minutes: 5
    algorithm: "RSA-SHA256"
    pki_service: "https://localhost:8443/api/v1"
    
  encryption:
    enabled: true
    key_rotation: "daily"  # daily, weekly, monthly
    algorithm: "AES-256-GCM"
    key_storage: "rocksdb"  # Key encrypted with KEK from PKI
    
  retention:
    keep_signed_logs_days: 2555  # 7 Jahre (DSGVO Artikel 17)
    archive_to_cold_storage: true
    cold_storage_path: "/mnt/archive/saga_logs"
```

**LEK-Ablauf:**
1. **Tägliche KEK-Ableitung**: VCC-PKI Service-Zertifikat → HKDF → KEK(date)
2. **LEK-Generierung**: Zufällige 256-bit AES-Key → LEK(date)
3. **LEK-Speicherung**: `lek:20251031 = AES-GCM-Encrypt(KEK, LEK)` in RocksDB
4. **Log-Verschlüsselung**: Jeder SAGA-Entry → `AES-GCM-Encrypt(LEK, canonical_json)`
5. **Dekodierung**: Bei Audit-Anfrage → Lade LEK → Entschlüssele Logs

**Vorteil:** Bei Daten-Leak nur aktuelle LEK kompromittiert, nicht gesamte Historie.

---

## 3. DSGVO by Design: PII-Erkennung & Anonymisierung

### 3.1 Konzept

**DSGVO Artikel 25 (Data Protection by Design):**
- Sensitive PII automatisch erkennen
- Original-Entität durch UUID ersetzen (Pseudonymisierung)
- Original-Blob bleibt verschlüsselt, Zugriff nur mit User-Berechtigung
- Löschung: UUID-Mapping löschen → Original unwiederbringlich

### 3.2 PII-Detection Engine

**Multi-Strategy-Ansatz:**

```cpp
class PIIDetector {
public:
    enum class PIIType {
        EMAIL,           // RFC 5322 Email-Regex
        PHONE,           // E.164 + lokale Formate
        SSN,             // Social Security Number (US, DE, etc.)
        IBAN,            // International Bank Account Number
        CREDIT_CARD,     // Luhn-Algorithm Validation
        PASSPORT,        // Country-specific patterns
        IP_ADDRESS,      // IPv4/IPv6
        MEDICAL_ID,      // Krankenversicherungsnummer
        TAX_ID,          // Steuernummer, UID
        CUSTOM           // User-defined regex
    };
    
    struct PIIMatch {
        PIIType type;
        std::string field_path;  // e.g., "user.profile.email"
        std::string original_value;
        std::string anonymized_value;  // UUID
        size_t offset;
        size_t length;
    };
    
    std::vector<PIIMatch> detectPII(
        const json& entity_data,
        const PIIConfig& config
    );
};
```

**Detection-Strategien:**

1. **Regex-Based Detection** (schnell, hohe Präzision):
   ```cpp
   const std::regex EMAIL_REGEX(
       R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z|a-z]{2,}\b)"
   );
   
   const std::regex IBAN_REGEX(
       R"([A-Z]{2}\d{2}[A-Z0-9]{10,30})"
   );
   ```

2. **NER (Named Entity Recognition)** (Machine Learning, optional):
   - Integration mit lokalen NER-Modellen (z.B. spaCy, Flair)
   - Erkennung von Namen, Adressen, Organisationen in Freitext

3. **Schema-Based Detection** (Metadaten):
   ```yaml
   # config/pii_schema.yaml
   field_annotations:
     - field: "email"
       type: EMAIL
       auto_anonymize: true
     - field: "phone_number"
       type: PHONE
       auto_anonymize: true
     - field: "medical_records.patient_id"
       type: MEDICAL_ID
       auto_anonymize: true
       retention_days: 3650  # 10 Jahre HIPAA
   ```

### 3.3 Anonymisierung-Workflow

**Beispiel: Graph-Entity mit PII**

```cpp
// Original-Entity (vor Import)
{
  "pk": "patient_001",
  "name": "Max Mustermann",
  "email": "max.mustermann@example.com",
  "ssn": "123-45-6789",
  "diagnosis": "..."
}

// Nach PII-Detection & Anonymisierung
{
  "pk": "patient_001",
  "name": "pii_uuid_7a3f2e1b-4c5d-6a7b-8c9d-0e1f2a3b4c5d",
  "email": "pii_uuid_9f8e7d6c-5b4a-3c2d-1e0f-a9b8c7d6e5f4",
  "ssn": "pii_uuid_3e2d1c0b-a9f8-e7d6-c5b4-a3f2e1d0c9b8",
  "diagnosis": "..."
}

// PII-Mapping in separater CF (RocksDB Column Family)
Key: pii_uuid_7a3f2e1b-4c5d-6a7b-8c9d-0e1f2a3b4c5d
Value: {
  "original_value": "Max Mustermann",  // AES-256-GCM encrypted
  "field": "name",
  "entity_pk": "patient_001",
  "pii_type": "PERSON_NAME",
  "detected_at": "2025-10-31T14:30:00Z",
  "detected_by": "regex_ner",
  "retention_policy": "gdpr_erasure",
  "access_control": {
    "allowed_roles": ["doctor", "admin"],
    "audit_access": true
  }
}
```

**Zugriff auf Original:**
```cpp
std::string revealPII(
    const std::string& pii_uuid,
    const UserContext& user
) {
    // 1. Lade PII-Mapping
    auto pii_data = db_.get("pii:" + pii_uuid);
    
    // 2. ACL-Check
    if (!checkAccess(pii_data["access_control"], user)) {
        THEMIS_AUDIT_LOG("PII_ACCESS_DENIED", {
            {"pii_uuid", pii_uuid},
            {"user_id", user.id},
            {"timestamp", now()}
        });
        throw AuthorizationException("Access to PII denied");
    }
    
    // 3. Audit-Log
    THEMIS_AUDIT_LOG("PII_ACCESS_GRANTED", {
        {"pii_uuid", pii_uuid},
        {"user_id", user.id},
        {"field", pii_data["field"]},
        {"entity_pk", pii_data["entity_pk"]}
    });
    
    // 4. Entschlüsseln & Zurückgeben
    std::string encrypted = pii_data["original_value"];
    return decryptField(encrypted, user.field_key);
}
```

**Recht auf Vergessenwerden (DSGVO Artikel 17):**
```cpp
void erasePII(const std::string& entity_pk) {
    // 1. Finde alle PII-UUIDs für Entity
    auto pii_uuids = findPIIForEntity(entity_pk);
    
    // 2. Lösche PII-Mappings (Original unwiederbringlich)
    for (const auto& uuid : pii_uuids) {
        db_.delete("pii:" + uuid);
        
        THEMIS_AUDIT_LOG("PII_ERASED", {
            {"pii_uuid", uuid},
            {"entity_pk", entity_pk},
            {"timestamp", now()},
            {"reason", "gdpr_article_17"}
        });
    }
    
    // 3. Entity bleibt mit UUIDs (Pseudonymisiert, aber nutzbar für Statistik)
}
```

---

## 4. Audit-Trail & Structured Logging

### 4.1 Log-Kategorien

ThemisDB unterscheidet mehrere Log-Typen:

| Kategorie | Zweck | Signiert | Verschlüsselt | Retention |
|-----------|-------|----------|---------------|-----------|
| **SAGA** | Transaktions-Kompensationen | ✅ Ja | ✅ Ja | 7 Jahre |
| **AUDIT** | Datenzugriffe, ACL-Prüfungen | ✅ Ja | ✅ Ja (Encrypt-then-Sign bei Query-Daten) | 7 Jahre |
| **SECURITY** | Auth-Failures, Anomalien | ✅ Ja | ❌ Nein | 10 Jahre |
| **OPERATIONAL** | Performance, Errors | ❌ Nein | ❌ Nein | 90 Tage |
| **DEBUG** | Entwickler-Traces | ❌ Nein | ❌ Nein | 7 Tage |
### 4.4 Vertraulichkeitswahrende Signierung (Encrypt-then-Sign)

Für alle Log-Kategorien, die Query-Daten, Query-Parameter, Result-Samples oder PII enthalten können (insb. AUDIT, SAGA), gilt:

- Erst wird der Log-Eintrag als Canonical JSON serialisiert
- Dann wird der Klartext mit dem tagesaktuellen LEK via AES-256-GCM verschlüsselt
- Der Hash für die PKI-Signatur wird über den Ciphertext gebildet (nicht über den Klartext)
- Signatur und AES-Metadaten (iv, tag, lek_id, optional aad) werden gemeinsam persistiert
- Eine redaktierte, nicht sensible Kurzform wird optional in stdout/file geloggt

Diese Reihenfolge verhindert, dass sensible Daten in Signaturvorlagen, SIEM-Pipelines oder Transportebenen im Klartext erscheinen.


### 4.2 Structured JSON-Logging

**Standard-Schema:**
```json
{
  "log_id": "uuid_v7",
  "timestamp": "2025-10-31T14:45:32.123Z",
  "category": "AUDIT",
  "severity": "INFO",
  "service": "themis-server",
  "host": "themis-prod-01",
  "user": {
    "id": "user_alice@example.com",
    "role": "analyst",
    "ip": "192.168.1.42",
    "session_id": "jwt_..."
  },
  "operation": {
    "type": "query",
    "resource": "graph:patients",
    "action": "read",
    "query_aql": "FOR p IN patients FILTER p.age > 50 RETURN p",
    "result_count": 42,
    "duration_ms": 156
  },
  "compliance": {
    "pii_accessed": ["email", "ssn"],
    "purpose": "medical_research",
    "legal_basis": "gdpr_article_6_1_e"
  },
  "metadata": {
    "saga_id": "tx_...",
    "trace_id": "otel_...",
    "correlation_id": "req_..."
  }
}
```

**Log-Sink Integration:**
```cpp
// include/utils/audit_logger.h
class AuditLogger {
public:
    static void logDataAccess(
        const UserContext& user,
        const std::string& resource,
        const std::string& action,
        const std::vector<std::string>& pii_fields,
        int64_t duration_ms
    );
    
    static void logSecurityEvent(
        const std::string& event_type,
        const json& details
    );
    
    static void logSAGAStep(
        const Saga::Step& step,
        const std::string& saga_id
    );
};
```

**spdlog Integration (Encrypt-then-Sign):**
```cpp
// src/utils/audit_logger.cpp
void AuditLogger::logDataAccess(...) {
  json log_entry = {
        {"log_id", generate_uuid_v7()},
        {"timestamp", iso8601_now()},
        {"category", "AUDIT"},
        {"user", {
            {"id", user.id},
            {"role", user.role},
            {"ip", user.ip_address}
        }},
        {"operation", {
            {"resource", resource},
            {"action", action},
            {"duration_ms", duration_ms}
        }},
        {"compliance", {
            {"pii_accessed", pii_fields}
        }}
    };

  // 1) Canonical JSON
  std::string canonical = toCanonicalJSON(log_entry);

  // 2) Encrypt with LEK (AES-256-GCM) if category contains query data
  if (governance_->shouldEncryptLogs("AUDIT")) {
    AAD aad{ {"log_id", log_entry["log_id"]}, {"category", "AUDIT"}, {"timestamp", log_entry["timestamp"]} };
    auto enc = aes_gcm_encrypt(lek_manager_.current(), canonical, aad);

    // 3) Hash ciphertext and queue for signing (Encrypt-then-Sign)
    auto hash = sha256(enc.ciphertext);
    queueForSigning(hash, enc.meta); // enc.meta carries iv, tag, lek_id, aad

    // 4) Persist encrypted envelope for audit storage
    persistEncryptedAudit(enc, log_entry["log_id"]);

    // 5) Emit redacted line to console/file sinks only
    auto logger = spdlog::get("audit");
    logger->info("{{\"log_id\":\"{}\",\"category\":\"AUDIT\",\"encrypted\":true}}", (std::string)log_entry["log_id"]);
  } else {
    // Non-sensitive: plain JSON to sinks and to signing queue (still encrypted if policy enforces)
    auto logger = spdlog::get("audit");
    logger->info(canonical);
    addToPendingSAGABatch(log_entry);
  }
}
```

### 4.3 SIEM-Integration

**Export zu externen SIEM-Systemen:**

```yaml
# config/governance.yaml
audit_export:
  enabled: true
  destinations:
    - type: syslog
      host: "siem.internal.vcc"
      port: 514
      protocol: "TCP"
      tls: true
      categories: ["AUDIT", "SECURITY"]
      encryption:
        encrypt_payloads: true
        sign_ciphertext: true
        redact_console_output: true
      
    - type: elasticsearch
      url: "https://elastic.internal.vcc:9200"
      index: "themis-audit-{date}"
      auth_type: "api_key"
      api_key_env: "ELASTIC_API_KEY"
      encryption:
        encrypt_payloads: true
        sign_ciphertext: true
      
    - type: file
      path: "/var/log/themis/audit_{date}.json.gz"
      rotation: "daily"
      compression: true
      max_size_mb: 500
      encryption:
        encrypt_payloads: true
        sign_ciphertext: true
```

---

## 5. Governance Policy Engine (GPE)

### 5.1 Policy-Definition (YAML)

**Zentrale Governance-Konfiguration:**

```yaml
# config/governance_policies.yaml
governance:
  version: "1.0"
  effective_date: "2025-11-01"
  
  # ========== DATA CLASSIFICATION ==========
  data_classification:
    levels:
      - name: "public"
        encryption_required: false
        pii_detection: false
        retention_days: 365
        
      - name: "internal"
        encryption_required: true
        pii_detection: true
        retention_days: 2555  # 7 Jahre
        access_control: "role_based"
        
      - name: "confidential"
        encryption_required: true
        pii_detection: true
        retention_days: 3650  # 10 Jahre
        access_control: "attribute_based"
        audit_all_access: true
        
      - name: "restricted"
        encryption_required: true
        encryption_algorithm: "AES-256-GCM"
        pii_detection: true
        pii_auto_anonymize: true
        retention_days: 3650
        access_control: "multi_factor"
        audit_all_access: true
        require_approval: true
  
  # ========== PII DETECTION RULES ==========
  pii_detection:
    enabled: true
    strategies:
      - type: "regex"
        patterns:
          email: '\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z|a-z]{2,}\b'
          phone_de: '\+49\s?\d{2,5}\s?\d{3,10}'
          iban: '[A-Z]{2}\d{2}[A-Z0-9]{10,30}'
          ssn_us: '\d{3}-\d{2}-\d{4}'
          
      - type: "schema_annotation"
        fields:
          - path: "*.email"
            type: EMAIL
          - path: "patient.medical_id"
            type: MEDICAL_ID
          - path: "user.ssn"
            type: SSN
    
    anonymization:
      method: "uuid_replacement"
      uuid_prefix: "pii_uuid_"
      store_mapping: true
      mapping_encryption: true
      
    access_control:
      default_deny: true
      allowed_roles:
        - "gdpr_officer"
        - "compliance_admin"
      audit_all_reveals: true
  
  # ========== RETENTION POLICIES ==========
  retention:
    default_days: 2555  # 7 Jahre DSGVO
    
    overrides:
      - resource_pattern: "medical_records.*"
        retention_days: 3650  # 10 Jahre HIPAA
        legal_basis: "HIPAA_164_316"
        
      - resource_pattern: "financial.*"
        retention_days: 3650  # 10 Jahre HGB
        legal_basis: "HGB_257"
        
      - resource_pattern: "debug_logs.*"
        retention_days: 7
        auto_purge: true
    
    archive:
      enabled: true
      after_days: 365
      storage: "cold_storage"
      compression: "zstd"
      encryption: true
  
  # ========== SAGA LOG SIGNING ==========
  saga_signing:
    enabled: true
    batch_size: 1000
    batch_interval_minutes: 5
    signature_algorithm: "RSA-SHA256"
    pki_endpoint: "https://localhost:8443/api/v1/sign"
    cert_service_id: "themis-db"
    encrypt_then_sign: true   # Erzwingt: erst AES-256-GCM verschlüsseln, dann Ciphertext hash/sign
    categories:
      encrypt_before_sign: ["SAGA", "AUDIT"]
    
    verification:
      on_query: true  # Bei Audit-Anfragen automatisch verifizieren
      periodic_check: true
      check_interval_hours: 24
  
  # ========== COMPLIANCE FRAMEWORKS ==========
  compliance_frameworks:
    gdpr:
      enabled: true
      data_protection_officer: "dpo@example.com"
      article_30_register: "/var/themis/gdpr_register.json"
      breach_notification_hours: 72
      
    hipaa:
      enabled: true
      covered_entity: true
      business_associate: false
      security_officer: "ciso@example.com"
      
    bsi_c5:
      enabled: true
      attestation_level: "Type 2"
      audit_frequency_months: 12
      
    soc2:
      enabled: false

  # ========== DE (VS) KLASSIFIZIERUNG ==========
  vs_classification:
    levels:
      - name: "offen"
        encryption_profile:
          required: false
          algorithm: "AES-256-GCM"
          double_encrypt: false
          hsm_only: false
        logs:
          encrypt_then_sign: false
          redact: false
        vector_policy: "allow"
        export_policy: "allow"
        cache_policy: { persistent: true, ttl_seconds: 86400 }

      - name: "vs-nfd"
        encryption_profile:
          required: true
          algorithm: "AES-256-GCM"
          double_encrypt: false
          hsm_only: false
        logs:
          encrypt_then_sign: true
          lek_rotation: "daily"
          redact: true
        vector_policy: "allow_metadata_only"
        export_policy: "allow_with_approval"
        cache_policy: { persistent: false, ttl_seconds: 3600 }

      - name: "geheim"
        encryption_profile:
          required: true
          algorithm: "AES-256-GCM"
          double_encrypt: true
          hsm_only: true
        logs:
          encrypt_then_sign: true
          lek_rotation: "daily"
          redact: "strict"
        vector_policy: "restricted"   # keine Klartext-Embeddings-Exporte
        export_policy: "approval_only"
        cache_policy: { persistent: false, ttl_seconds: 0 }

      - name: "streng-geheim"
        encryption_profile:
          required: true
          algorithm: "AES-256-GCM"
          double_encrypt: true
          hsm_only: true
        logs:
          encrypt_then_sign: true
          lek_rotation: "daily"
          redact: "strict"
          minimal_plain_meta: ["log_id", "category", "timestamp"]
        vector_policy: "disable_ann"
        export_policy: "forbidden"
        cache_policy: { persistent: false, ttl_seconds: 0 }

  enforcement:
    default_classification: "vs-nfd"
    map_resources:
      - resource_pattern: "patients.*"
        classification: "geheim"
      - resource_pattern: "intelligence.*"
        classification: "streng-geheim"
      - resource_pattern: "public_docs.*"
        classification: "offen"
    endpoint_switches:
      headers:
        classification: "X-Classification"   # offen|vs-nfd|geheim|streng-geheim
        governance_mode: "X-Governance-Mode" # enforce|simulate
        encrypt_logs: "X-Encrypt-Logs"       # on|off|auto
        redaction_level: "X-Redaction-Level" # none|standard|strict
      response_headers:
        policy: "X-Themis-Policy"
        integrity: "X-Themis-Integrity"
```

### 5.2 Policy Validation & Enforcement

```cpp
// include/governance/policy_engine.h
class GovernancePolicyEngine {
public:
    explicit GovernancePolicyEngine(const std::string& config_path);
    
    // Policy-Validierung
    bool validateOperation(
        const UserContext& user,
        const std::string& resource,
        const std::string& action
    );
    
    // Data Classification
    std::string getClassificationLevel(const std::string& resource);
    
    // Retention
    int getRetentionDays(const std::string& resource);
    bool shouldArchive(const std::string& resource, int age_days);
    bool shouldPurge(const std::string& resource, int age_days);
    
    // PII
    bool shouldDetectPII(const std::string& resource);
    bool shouldAutoAnonymize(const std::string& resource);
    std::vector<std::string> getAllowedPIIRoles();
    
    // Audit
    bool shouldAuditAccess(const std::string& resource);
    std::vector<std::string> getComplianceFrameworks();
    
  // VS-Classification helpers
  std::string resolveClassification(const std::string& resource, const std::optional<std::string>& header_cls);
  EncryptionProfile getEncryptionProfile(const std::string& classification);
  LogRules getLogRules(const std::string& classification);
  VectorPolicy getVectorPolicy(const std::string& classification);
  bool isExportAllowed(const std::string& classification, bool hasApproval);
    
private:
    json config_;
    std::unordered_map<std::string, json> classification_cache_;
};
```

Endpoint-Durchsetzung (Skizze):
```cpp
auto hdr_cls = req.header("X-Classification");
auto cls = gpe.resolveClassification(resource, hdr_cls);
auto enc = gpe.getEncryptionProfile(cls);
auto logs = gpe.getLogRules(cls);
auto vecp = gpe.getVectorPolicy(cls);

if (vecp == VectorPolicy::DISABLE_ANN) {
  return Status::PermissionDenied("ANN disabled for classification: " + cls);
}

applyEncryptionProfile(entity, enc, user);
auditLogger.logWithRules(user, resource, action, logs);
```

**Verwendung im Query-Engine:**

```cpp
// src/query/query_executor.cpp
Status QueryExecutor::executeQuery(
    const AQLQuery& query,
    const UserContext& user,
    QueryResult& result
) {
    auto start = std::chrono::steady_clock::now();
    
    // 1. Policy-Check
    for (const auto& collection : query.collections) {
        if (!gpe_->validateOperation(user, collection, "read")) {
            THEMIS_AUDIT_LOG("QUERY_DENIED", {
                {"user", user.id},
                {"collection", collection},
                {"reason", "policy_violation"}
            });
            return Status::PermissionDenied("Access to " + collection + " denied");
        }
    }
    
    // 2. PII-Detection aktivieren?
    bool detect_pii = gpe_->shouldDetectPII(query.collections[0]);
    
    // 3. Query ausführen
    auto status = executor_->execute(query, result);
    
    // 4. PII-Anonymisierung (wenn konfiguriert)
    if (detect_pii && gpe_->shouldAutoAnonymize(query.collections[0])) {
        anonymizePIIInResult(result, user);
    }
    
    // 5. Audit-Log
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    );
    
    if (gpe_->shouldAuditAccess(query.collections[0])) {
        AuditLogger::logDataAccess(
            user,
            query.collections[0],
            "query",
            extractPIIFields(result),
            duration.count()
        );
    }
    
    return status;
}
```

---

## 6. Data Retention & Archival

### 6.1 Retention Manager

```cpp
// include/governance/retention_manager.h
class RetentionManager {
public:
    struct RetentionPolicy {
        std::string resource_pattern;  // Regex: "medical_records.*"
        int retention_days;
        bool auto_archive;
        int archive_after_days;
        bool auto_purge;
        std::string legal_basis;
    };
    
    explicit RetentionManager(
        RocksDBWrapper& db,
        const GovernancePolicyEngine& gpe
    );
    
    // Background-Task (täglich ausgeführt)
    void runRetentionSweep();
    
    // Manuelle Operationen
    std::vector<std::string> findExpiredEntities(const std::string& collection);
    void archiveEntity(const std::string& pk);
    void purgeEntity(const std::string& pk);
    
private:
    RocksDBWrapper& db_;
    const GovernancePolicyEngine& gpe_;
};
```

**Workflow:**

```cpp
void RetentionManager::runRetentionSweep() {
    THEMIS_INFO("Starting retention sweep");
    
    // Alle Collections durchlaufen
    for (const auto& collection : getAllCollections()) {
        auto policy = gpe_.getRetentionPolicy(collection);
        
        // Finde alte Entities (via created_at/modified_at)
        auto expired = findExpiredEntities(collection);
        
        for (const auto& pk : expired) {
            auto age_days = getEntityAgeDays(pk);
            
            // Archivierung?
            if (policy.auto_archive && age_days >= policy.archive_after_days) {
                archiveEntity(pk);
                THEMIS_AUDIT_LOG("ENTITY_ARCHIVED", {
                    {"pk", pk},
                    {"collection", collection},
                    {"age_days", age_days},
                    {"legal_basis", policy.legal_basis}
                });
            }
            
            // Löschung?
            if (policy.auto_purge && age_days >= policy.retention_days) {
                purgeEntity(pk);
                THEMIS_AUDIT_LOG("ENTITY_PURGED", {
                    {"pk", pk},
                    {"collection", collection},
                    {"age_days", age_days},
                    {"legal_basis", policy.legal_basis}
                });
            }
        }
    }
    
    THEMIS_INFO("Retention sweep completed");
}
```

### 6.2 Cold Storage Export

**Archivierung zu externem Storage:**

```cpp
void RetentionManager::archiveEntity(const std::string& pk) {
    // 1. Lade vollständige Entity-Daten
    auto entity = loadFullEntity(pk);  // Mit Graph-Kanten, Content-Blobs, etc.
    
    // 2. Serialize als JSON
    json archive_entry = {
        {"pk", pk},
        {"archived_at", iso8601_now()},
        {"original_data", entity},
        {"metadata", {
            {"collection", entity["_collection"]},
            {"created_at", entity["created_at"]},
            {"data_classification", gpe_.getClassificationLevel(pk)}
        }}
    };
    
    // 3. Kompression (ZSTD)
    auto json_str = archive_entry.dump();
    auto compressed = zstd_compress(json_str.data(), json_str.size(), 19);
    
    // 4. Verschlüsselung (optional, je nach Policy)
    auto encrypted = encryptArchive(compressed);
    
    // 5. Export zu Cold Storage
    std::string archive_path = config_["cold_storage_path"].get<std::string>() 
                             + "/" + getCurrentDatePath() 
                             + "/" + pk + ".zst.enc";
    writeToFile(archive_path, encrypted);
    
    // 6. Markiere in DB als archived (nicht löschen, nur Flag)
    db_.put(pk + ":metadata", json{{"archived", true}, {"archive_path", archive_path}}.dump());
}
```

---

## 7. Compliance-Reports

### 7.1 DSGVO Artikel 30: Verarbeitungsverzeichnis

**Automatische Generierung:**

```cpp
json generateGDPRArticle30Register() {
    json reg = {
        {"controller", {
            {"name", "VCC GmbH"},
            {"contact", "dpo@example.com"},
            {"address", "Musterstraße 1, 12345 Berlin"}
        }},
        {"processing_activities", json::array()}
    };
    
    // Alle Collections analysieren
    for (const auto& collection : getAllCollections()) {
        auto classification = gpe_.getClassificationLevel(collection);
        auto retention = gpe_.getRetentionDays(collection);
        
        json activity = {
            {"purpose", collection + " data processing"},
            {"legal_basis", "GDPR Article 6(1)(e)"},  // Public interest
            {"data_categories", getDataCategories(collection)},
            {"recipients", "Internal staff only"},
            {"retention_period", std::to_string(retention) + " days"},
            {"security_measures", {
                "AES-256-GCM encryption",
                "PKI-signed audit logs",
                "Role-based access control",
                "PII auto-anonymization"
            }},
            {"data_subjects", "EU citizens"}
        };
        
        reg["processing_activities"].push_back(activity);
    }
    
    return reg;
}
```

**Export:**
```bash
# HTTP Endpoint
GET /api/compliance/gdpr/article30

# CLI Tool
$ themis-cli compliance gdpr-register --format json > gdpr_register.json
$ themis-cli compliance gdpr-register --format pdf > gdpr_register.pdf
```

### 7.2 Audit-Trail-Report

```cpp
json generateAuditReport(
    const std::string& start_date,
    const std::string& end_date,
    const std::vector<std::string>& categories
) {
    json report = {
        {"period", {{"start", start_date}, {"end", end_date}}},
        {"categories", categories},
        {"entries", json::array()},
        {"summary", {}}
    };
    
    // Lade SAGA-Logs aus Zeitraum
    auto logs = loadSAGALogs(start_date, end_date, categories);
    
    // Verifiziere Signaturen
    int verified = 0, failed = 0;
    for (const auto& batch : getSAGABatches(start_date, end_date)) {
        if (verifySAGABatch(batch.id)) {
            verified++;
        } else {
            failed++;
            report["integrity_violations"].push_back({
                {"batch_id", batch.id},
                {"signed_at", batch.signed_at}
            });
        }
    }
    
    report["summary"] = {
        {"total_entries", logs.size()},
        {"verified_batches", verified},
        {"failed_batches", failed},
        {"pii_accesses", countPIIAccesses(logs)},
        {"security_events", countSecurityEvents(logs)}
    };
    
    report["entries"] = logs;
    
    return report;
}
```

---

## 8. Implementation Roadmap

### Phase 1: Audit-Logging & SAGA-Signierung (Woche 1-2)

**Deliverables:**
- ✅ `AuditLogger` Klasse mit spdlog JSON-Output
- ✅ SAGA-Log-Erweiterung mit Batch-Collection
- ✅ VCC-PKI Client für Signierung (`POST /api/v1/sign`)
- ✅ LEK (Log Encryption Key) Management mit täglicher Rotation
- ✅ `verifySAGABatch()` Funktion für Signatur-Validierung

**Config:**
```yaml
# config/governance.yaml (initial)
saga_log:
  signature:
    enabled: true
    batch_size: 1000
    batch_interval_minutes: 5
  encryption:
    enabled: true
    key_rotation: daily
```

### Phase 2: PII-Detection & Anonymisierung (Woche 3-4)

**Deliverables:**
- ✅ `PIIDetector` Klasse mit Regex + Schema-Strategien
- ✅ UUID-Replacement-Logik in Entity-Import
- ✅ PII-Mapping-Storage in separater RocksDB CF
- ✅ `revealPII()` mit ACL-Check + Audit-Log
- ✅ `erasePII()` für DSGVO Artikel 17

**Config:**
```yaml
pii_detection:
  enabled: true
  strategies:
    - type: regex
    - type: schema_annotation
  anonymization:
    method: uuid_replacement
```

### Phase 3: Governance Policy Engine (Woche 5-6)

**Deliverables:**
- ✅ `GovernancePolicyEngine` Klasse mit YAML-Parsing
- ✅ Data Classification API
- ✅ Policy-Validation in Query-Engine
- ✅ Retention-Policy-Integration
- ✅ Multi-Framework Support (GDPR, HIPAA, BSI C5)

**Config:**
```yaml
governance:
  data_classification:
    levels: [public, internal, confidential, restricted]
  compliance_frameworks:
    gdpr: {enabled: true}
    hipaa: {enabled: true}
```

### Phase 4: Retention & Archival (Woche 7-8)

**Deliverables:**
- ✅ `RetentionManager` Klasse
- ✅ Background-Task für Daily Sweep
- ✅ Cold-Storage-Export (ZSTD + Verschlüsselung)
- ✅ Archiv-Metadata in RocksDB
- ✅ Compliance-Reports (GDPR Artikel 30, Audit-Trails)

**Config:**
```yaml
retention:
  default_days: 2555
  archive:
    enabled: true
    storage: /mnt/archive/themis
```

### Phase 5: Testing & Compliance Validation (Woche 9-10)

**Tests:**
- ✅ SAGA-Signatur-Roundtrip (Sign → Verify)
- ✅ PII-Detection für alle Typen (Email, Phone, SSN, etc.)
- ✅ Anonymisierung + Reveal + Erase Workflow
- ✅ Policy-Engine mit allen Klassifizierungen
- ✅ Retention-Sweep mit Archivierung
- ✅ GDPR Artikel 30 Register-Generierung
- ✅ Multi-User Audit-Trail (verschiedene Rollen)

**Performance:**
- Benchmark: SAGA-Signierung Overhead (<5% bei Batch=1000)
- Benchmark: PII-Detection Latenz (<1ms pro Entity)
- Load-Test: 1M Entities mit Retention-Sweep (<10min)

---

## 9. Configuration Examples

### 9.1 Produktions-Konfiguration

```yaml
# config/governance.yaml (Production)
governance:
  version: "1.0"
  environment: "production"
  
  # Audit-Logging
  audit:
    enabled: true
    categories:
      - SAGA
      - AUDIT
      - SECURITY
    sinks:
      - type: file
        path: /var/log/themis/audit.json
      - type: syslog
        host: siem.internal.vcc
        port: 514
        tls: true
      - type: elasticsearch
        url: https://elastic.internal.vcc:9200
        index: themis-audit-{date}
  
  # SAGA-Signierung
  saga_signing:
    enabled: true
    batch_size: 1000
    batch_interval_minutes: 5
    signature_algorithm: RSA-SHA256
    pki_endpoint: https://pki.internal.vcc:8443/api/v1/sign
    cert_service_id: themis-db-prod
    encrypt_then_sign: true
    categories:
      encrypt_before_sign: [SAGA, AUDIT]
    verification:
      on_query: true
      periodic_check: true
      check_interval_hours: 24
  
  # Log-Verschlüsselung
  log_encryption:
    enabled: true
    key_rotation: daily
    algorithm: AES-256-GCM
    key_storage: rocksdb
    aad_fields: [log_id, category, timestamp]
    encrypt_categories: [SAGA, AUDIT]
  
  # PII-Erkennung
  pii_detection:
    enabled: true
    strategies:
      - type: regex
        patterns_file: /etc/themis/pii_patterns.yaml
      - type: schema_annotation
        schema_file: /etc/themis/pii_schema.yaml
    anonymization:
      method: uuid_replacement
      uuid_prefix: "pii_"
      store_mapping: true
      mapping_encryption: true
    access_control:
      default_deny: true
      allowed_roles: [gdpr_officer, compliance_admin, legal]
      audit_all_reveals: true
  
  # Data Classification
  data_classification:
    levels:
      - name: public
        encryption_required: false
        pii_detection: false
        retention_days: 365
      - name: internal
        encryption_required: true
        pii_detection: true
        retention_days: 2555  # 7 Jahre
        access_control: role_based
      - name: confidential
        encryption_required: true
        pii_detection: true
        retention_days: 3650  # 10 Jahre
        access_control: attribute_based
        audit_all_access: true
      - name: restricted
        encryption_required: true
        encryption_algorithm: AES-256-GCM
        pii_detection: true
        pii_auto_anonymize: true
        retention_days: 3650
        access_control: multi_factor
        audit_all_access: true
        require_approval: true
  
  # Retention
  retention:
    default_days: 2555
    archive:
      enabled: true
      after_days: 365
      storage: /mnt/cold_storage/themis
      compression: zstd
      encryption: true
    policies:
      - resource_pattern: "medical_records.*"
        retention_days: 3650
        legal_basis: HIPAA_164_316
      - resource_pattern: "financial.*"
        retention_days: 3650
        legal_basis: HGB_257
      - resource_pattern: "debug_logs.*"
        retention_days: 7
        auto_purge: true
  
  # Compliance-Frameworks
  compliance_frameworks:
    gdpr:
      enabled: true
      data_protection_officer: dpo@vcc.internal
      article_30_register: /var/themis/gdpr_register.json
      breach_notification_hours: 72
    hipaa:
      enabled: true
      covered_entity: true
      security_officer: ciso@vcc.internal
    bsi_c5:
      enabled: true
      attestation_level: Type 2
      audit_frequency_months: 12
```

### 9.2 Development-Konfiguration

```yaml
# config/governance.dev.yaml
governance:
  version: "1.0"
  environment: "development"
  
  saga_signing:
    enabled: false  # Schnelleres Testing
  
  log_encryption:
    enabled: false
  
  pii_detection:
    enabled: true
    strategies:
      - type: regex
        patterns:
          email: '\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z|a-z]{2,}\b'
    anonymization:
      method: uuid_replacement
      store_mapping: true
      mapping_encryption: false  # Dev: PII in Klartext für Debugging
    access_control:
      default_deny: false  # Dev: Offener Zugriff
  
  retention:
    default_days: 7  # Kurze Retention für Dev-DB
    archive:
      enabled: false
  
  compliance_frameworks:
    gdpr:
      enabled: true
    hipaa:
      enabled: false
```

---

## 10. Security Best Practices

### 10.1 Least Privilege für PII-Zugriff

```yaml
# config/rbac_policies.yaml
roles:
  - name: analyst
    permissions:
      - resource: "patients.*"
        actions: [read]
        pii_reveal: false  # Sieht nur UUIDs
        
  - name: doctor
    permissions:
      - resource: "patients.*"
        actions: [read, write]
        pii_reveal: true  # Kann PII entschlüsseln
        pii_types: [EMAIL, PHONE, MEDICAL_ID]
        
  - name: gdpr_officer
    permissions:
      - resource: "*"
        actions: [read, write, delete]
        pii_reveal: true
        pii_erase: true  # Kann DSGVO-Löschung durchführen
        
  - name: compliance_admin
    permissions:
      - resource: "*"
        actions: [read]
        pii_reveal: true
        audit_access: true
        compliance_reports: true
```

### 10.2 Signature-Verification bei Audit-Anfragen

```cpp
// Automatische Verifizierung bei /api/audit/logs Anfragen
GET /api/audit/logs?start=2025-10-01&end=2025-10-31

Response:
{
  "logs": [...],
  "signature_verification": {
    "total_batches": 42,
    "verified": 42,
    "failed": 0,
    "integrity_status": "OK"
  }
}
```

### 10.3 Immutable Audit-Logs (RocksDB SST-Sealing)

**Optional:** RocksDB SST-Files als Read-Only nach Signierung:

```cpp
void sealSignedSAGABatch(const std::string& batch_id) {
    // 1. Force Flush to SST
    db_.flush();
    
    // 2. Hole SST-File-Pfad für Batch
    auto sst_file = getSSTFileForBatch(batch_id);
    
    // 3. Setze Read-Only (OS-Level)
    chmod(sst_file.c_str(), 0444);  // r--r--r--
    
    // 4. Optional: Kopiere zu WORM-Storage (Write-Once-Read-Many)
    copyToWORMStorage(sst_file);
}
```

---

## 11. Compliance Verification Checklist

### DSGVO (GDPR)

| Anforderung | Artikel | Implementierung | Status |
|-------------|---------|-----------------|--------|
| Verschlüsselung at-rest | Art. 32 | AES-256-GCM | ✅ |
| Audit-Trail | Art. 30 | PKI-signierte SAGA-Logs | ✅ |
| Recht auf Vergessenwerden | Art. 17 | `erasePII()` mit UUID-Löschung | ✅ |
| Datenminimierung | Art. 5(1)(c) | Auto-Anonymisierung | ✅ |
| Privacy by Design | Art. 25 | PII-Detection bei Import | ✅ |
| Meldepflicht Datenpanne | Art. 33 | Security-Event-Logging | ✅ |
| Verarbeitungsverzeichnis | Art. 30 | Auto-Generierung `/compliance/gdpr/article30` | ✅ |

### HIPAA

| Anforderung | Section | Implementierung | Status |
|-------------|---------|-----------------|--------|
| Access Controls | §164.312(a)(1) | RBAC + ABAC | ✅ |
| Audit Controls | §164.312(b) | Structured Audit-Logs | ✅ |
| Integrity Controls | §164.312(c)(1) | PKI-Signaturen | ✅ |
| Transmission Security | §164.312(e)(1) | TLS 1.3 + mTLS | ✅ |
| Encryption at Rest | §164.312(a)(2)(iv) | AES-256-GCM | ✅ |
| Log Retention | §164.316(b)(2)(i) | 10 Jahre für Medical Records | ✅ |

### BSI C5

| Kontrolle | Beschreibung | Implementierung | Status |
|-----------|--------------|-----------------|--------|
| ORP-4 | Datenschutzbeauftragter | Config: `dpo@example.com` | ✅ |
| OPS-11 | Protokollierung | Structured JSON-Logs | ✅ |
| OPS-12 | Überwachung | SIEM-Export | ✅ |
| IAM-01 | Identitätsmanagement | VCC-User Integration | ✅ |
| IAM-03 | Zugriffsrechte | RBAC + Policy Engine | ✅ |
| CRY-01 | Verschlüsselung | AES-256-GCM | ✅ |
| CRY-02 | Schlüsselmanagement | VCC-PKI Integration | ✅ |

---

## 12. Next Steps

### Immediate (Woche 1-2)
1. ✅ SAGA-Log-Erweiterung mit Batch-Collection
2. ✅ VCC-PKI REST-Client für Signierung
3. ✅ LEK (Log Encryption Key) Rotation-Logik
4. ✅ `AuditLogger` mit spdlog JSON-Output

### Short-Term (Woche 3-6)
5. ✅ `PIIDetector` mit Regex + Schema-Strategien
6. ✅ UUID-Replacement in Entity-Import
7. ✅ `GovernancePolicyEngine` mit YAML-Config
8. ✅ Policy-Validation in Query-Engine

### Medium-Term (Woche 7-10)
9. ✅ `RetentionManager` mit Background-Sweep
10. ✅ Cold-Storage-Export mit ZSTD + Encryption
11. ✅ Compliance-Report-Generierung (GDPR, HIPAA)
12. ✅ Integration-Tests für alle Compliance-Features

### Long-Term (Woche 11+)
13. ❌ NER (Named Entity Recognition) für ML-basierte PII-Detection
14. ❌ Blockchain-Anchoring für SAGA-Signaturen (zusätzliche Unveränderlichkeit)
15. ❌ GDPR-DSR-Workflow (Data Subject Request Automation)
16. ❌ Compliance-Dashboard (Web-UI für DPO/CISO)

---

## Zusammenfassung

Diese Strategie definiert eine umfassende **Compliance & Governance-Architektur** für ThemisDB:

- 📝 **PKI-signierte SAGA-Logs** für unveränderliche Audit-Trails
- 🔐 **DSGVO by Design** mit automatischer PII-Erkennung und UUID-Anonymisierung
- ⚖️ **Multi-Framework-Support** (GDPR, HIPAA, BSI C5)
- 🎯 **Policy-Driven** mit YAML/JSON-Konfiguration für alle Governance-Regeln
- 🔒 **Log-Verschlüsselung** mit täglicher LEK-Rotation
- 📦 **Retention & Archival** mit Cold-Storage-Export
- 🔍 **Audit-Reports** mit automatischer Signatur-Verifizierung

**Kerntechnologien:**
- VCC-PKI für Signierung & Verschlüsselung
- RocksDB für unveränderliche Log-Storage
- spdlog für Structured JSON-Logging
- YAML/JSON für Policy-Konfiguration

Die Implementierung erfolgt in **10 Wochen** mit vollständiger Integration in bestehende ThemisDB-Infrastruktur (Encryption, VCC-PKI, VCC-User).
