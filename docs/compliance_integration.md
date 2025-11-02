# Themis Compliance Integration Guide

## Übersicht

Themis bietet eine vollständige Compliance-Pipeline für DSGVO/eIDAS-konforme Datenverarbeitung:

1. **Data Governance**: Klassifizierung nach VS-NfD/Geheim/Streng Geheim
2. **PII Detection**: Automatische Erkennung personenbezogener Daten
3. **Audit Logging**: Encrypt-then-Sign mit PKI
4. **Retention Management**: Automatische Archivierung und Löschung

---

## 1. Data Governance

### Klassifizierungsstufen

| Level | Beschreibung | Regeln |
|-------|--------------|--------|
| `offen` | Öffentliche Daten | Keine Einschränkungen |
| `vs-nfd` | Verschlusssache - Nur für den Dienstgebrauch | Verschlüsselung erforderlich |
| `geheim` | Geheime Daten | Verschlüsselung + Zugriffskontrolle + kein ANN |
| `streng_geheim` | Streng geheim | Maximale Sicherheit + Audit-Trail |

### HTTP-Header

```http
X-Data-Classification: geheim
X-Governance-Mode: enforce
```

### Governance-Policies

```json
{
  "classification": "geheim",
  "encryption_required": true,
  "retention_days": 90,
  "allow_ann_indexing": false,
  "require_audit": true,
  "cache_policy": "no-cache",
  "export_policy": "deny"
}
```

### Beispiel: Klassifizierter Request

```bash
curl -X POST http://localhost:8765/entities \
  -H "Content-Type: application/json" \
  -H "X-Data-Classification: vs-nfd" \
  -H "X-Governance-Mode: enforce" \
  -d '{
    "object_type": "patient_record",
    "name": "Max Mustermann",
    "ssn": "123-45-6789",
    "created_at": 1730505600
  }'
```

**Response Headers:**
```http
X-Data-Classification: vs-nfd
X-Encryption-Required: true
X-Retention-Days: 365
X-Allow-ANN: false
```

---

## 2. PII Detection

### Unterstützte PII-Typen

| Typ | Beispiel | Regex-Pattern |
|-----|----------|---------------|
| `EMAIL` | `user@example.com` | RFC 5322 |
| `PHONE` | `+49 123 456789`, `(555) 123-4567` | International + US |
| `SSN` | `123-45-6789` | US Social Security |
| `CREDIT_CARD` | `4532-1234-5678-9010` | Luhn-validiert |
| `IBAN` | `DE89370400440532013000` | IBAN-Format |
| `IP_ADDRESS` | `192.168.1.1` | IPv4 |
| `URL` | `https://example.com/path` | HTTP(S) URLs |

### YAML-Konfiguration

```yaml
# config/pii_detection.yaml
engines:
  - name: regex_engine
    type: regex
    enabled: true
    patterns:
      - type: CREDIT_CARD
        pattern: '\b\d{4}[- ]?\d{4}[- ]?\d{4}[- ]?\d{4}\b'
        validate_luhn: true
      - type: EMAIL
        pattern: '[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}'
```

### Automatische PII-Erkennung

```cpp
#include "utils/pii_detector.h"

vcc::PIIDetector detector("./config/pii_detection.yaml");

nlohmann::json data = {
    {"email", "john.doe@example.com"},
    {"phone", "+49 30 12345678"},
    {"ssn", "123-45-6789"},
    {"credit_card", "4532123456789010"}
};

auto findings = detector.detectInJson(data);

for (const auto& finding : findings) {
    std::cout << "Found " << finding.type 
              << " at " << finding.json_path 
              << ": " << finding.value << "\n";
    
    // Redact based on classification
    auto redacted = detector.maskValue(finding.value, finding.type, 
                                       vcc::PIIDetector::RedactionMode::PARTIAL);
    std::cout << "Redacted: " << redacted << "\n";
}
```

**Output:**
```
Found EMAIL at $.email: john.doe@example.com
Redacted: j***@example.com
Found PHONE at $.phone: +49 30 12345678
Redacted: +49 *** *** **78
Found SSN at $.ssn: 123-45-6789
Redacted: ***-**-6789
Found CREDIT_CARD at $.credit_card: 4532123456789010
Redacted: 4532-****-****-9010
```

### Field-Hint-Klassifizierung

```cpp
// Automatische Erkennung via Feldnamen
auto hint = detector.classifyFieldName("user_email");
// hint = PIIType::EMAIL

auto hint2 = detector.classifyFieldName("credit_card_number");
// hint2 = PIIType::CREDIT_CARD
```

---

## 3. Audit Logging

### Konfiguration

```cpp
#include "utils/audit_logger.h"
#include "utils/pki_client.h"
#include "security/encryption.h"

// Setup
auto key_provider = std::make_shared<MockKeyProvider>();
key_provider->createKey("audit_key", 32);

auto field_enc = std::make_shared<FieldEncryption>(key_provider);

PKIConfig pki_cfg;
pki_cfg.service_id = "themis-audit";
pki_cfg.endpoint = "https://pki.example.com";
auto pki_client = std::make_shared<VCCPKIClient>(pki_cfg);

AuditLoggerConfig cfg;
cfg.enabled = true;
cfg.encrypt_then_sign = true;
cfg.log_path = "data/logs/audit.jsonl";
cfg.key_id = "audit_key";

auto audit_logger = std::make_shared<AuditLogger>(field_enc, pki_client, cfg);
```

### Ereignis loggen

```cpp
nlohmann::json event;
event["action"] = "DATA_ACCESS";
event["user_id"] = "user_12345";
event["resource"] = "/entities/patient_123";
event["classification"] = "geheim";
event["timestamp"] = std::time(nullptr);
event["ip_address"] = "192.168.1.42";

audit_logger->logEvent(event);
```

### Audit-Log-Format (JSONL)

```jsonl
{"encrypted_data":"base64...", "signature":"base64...", "key_id":"audit_key", "algorithm":"RSA-SHA256"}
{"encrypted_data":"base64...", "signature":"base64...", "key_id":"audit_key", "algorithm":"RSA-SHA256"}
```

---

## 4. Retention Management

### Policy-Konfiguration

```yaml
# config/retention_policies.yaml
policies:
  - name: user_personal_data
    retention_period: 365d
    archive_after: 180d
    auto_purge_enabled: true
    require_audit_trail: true
    classification_level: geheim
    metadata:
      legal_basis: "DSGVO Art. 17"
      
  - name: transaction_logs
    retention_period: 2555d  # 7 Jahre
    archive_after: 1095d     # 3 Jahre
    auto_purge_enabled: false
    require_audit_trail: true
    classification_level: vs-nfd
    metadata:
      legal_basis: "HGB §257"
```

### Server-Konfiguration

```json
// config/config.json
{
  "features": {
    "retention": {
      "enabled": true,
      "interval_hours": 24,
      "policies_path": "./config/retention_policies.yaml"
    }
  }
}
```

### Automatischer Retention-Check

Der Server führt automatisch täglich (konfigurierbar) Retention-Checks durch:

```
[INFO] Retention worker started (interval: 24h)
[INFO] [Retention] Completed: scanned=1523, archived=42, purged=18, retained=1463, errors=0
```

### Manuelle Retention-Prüfung

```cpp
#include "utils/retention_manager.h"

RetentionManager mgr("./config/retention_policies.yaml");

// Prüfe einzelne Entity
auto created_at = std::chrono::system_clock::now() - std::chrono::hours(24 * 200);

if (mgr.shouldArchive("entity_123", created_at, "user_personal_data")) {
    auto archive_handler = [](const std::string& id) {
        // Export to S3, Tape, etc.
        return true;
    };
    mgr.archiveEntity("entity_123", "user_personal_data", archive_handler);
}

if (mgr.shouldPurge("entity_456", created_at, "user_personal_data")) {
    auto purge_handler = [](const std::string& id) {
        // Delete from DB
        return true;
    };
    mgr.purgeEntity("entity_456", "user_personal_data", purge_handler);
}
```

---

## 5. End-to-End-Beispiel

### Szenario: DSGVO-konforme Patientendaten-Verarbeitung

#### 1. Daten empfangen & klassifizieren

```bash
curl -X POST http://localhost:8765/entities \
  -H "Content-Type: application/json" \
  -H "X-Data-Classification: geheim" \
  -H "X-Governance-Mode: enforce" \
  -d '{
    "object_type": "patient",
    "name": "Anna Schmidt",
    "email": "anna.schmidt@example.de",
    "ssn": "123-45-6789",
    "credit_card": "4532123456789010",
    "diagnosis": "Diabetes Typ 2",
    "created_at": 1730505600
  }'
```

**Governance-Response:**
```json
{
  "status": "ok",
  "entity_id": "patient_789",
  "governance": {
    "classification": "geheim",
    "encryption_required": true,
    "retention_days": 365,
    "allow_ann": false,
    "audit_logged": true
  }
}
```

#### 2. PII-Detection im Hintergrund

```cpp
// Server-seitig automatisch
auto findings = pii_detector.detectInJson(request_body);
// Findings: EMAIL, SSN, CREDIT_CARD

for (const auto& finding : findings) {
    nlohmann::json audit_pii;
    audit_pii["action"] = "PII_DETECTED";
    audit_pii["type"] = finding.type;
    audit_pii["path"] = finding.json_path;
    audit_pii["entity_id"] = "patient_789";
    audit_logger->logEvent(audit_pii);
}
```

#### 3. Verschlüsselte Speicherung

```cpp
// Sensitive Felder verschlüsseln
EncryptedField<std::string> encrypted_ssn(key_provider, "patient_key");
encrypted_ssn.encrypt("123-45-6789");

EncryptedField<std::string> encrypted_cc(key_provider, "patient_key");
encrypted_cc.encrypt("4532123456789010");

// In DB speichern
entity.setField("ssn_encrypted", encrypted_ssn.toBase64());
entity.setField("credit_card_encrypted", encrypted_cc.toBase64());
```

#### 4. Audit-Log bei Zugriff

```cpp
// Bei jedem Zugriff
nlohmann::json access_event;
access_event["action"] = "DATA_ACCESS";
access_event["user_id"] = request.headers["X-User-ID"];
access_event["entity_id"] = "patient_789";
access_event["classification"] = "geheim";
access_event["timestamp"] = std::time(nullptr);
access_event["ip"] = request.remote_addr;

audit_logger->logEvent(access_event);
```

#### 5. Automatische Retention

Nach 180 Tagen (archive_after):
```
[INFO] [Retention] Archive entity patient_789
[INFO] Audit: {"action":"RETENTION_ARCHIVE","entity_id":"patient_789"}
```

Nach 365 Tagen (retention_period):
```
[INFO] [Retention] Purge entity patient_789
[INFO] Audit: {"action":"RETENTION_PURGE","entity_id":"patient_789"}
```

---

## 6. Compliance-Checkliste

### DSGVO (Art. 5, 17, 25, 32)

- ✅ **Datenminimierung**: PII-Detection hilft, nur nötige Daten zu erheben
- ✅ **Speicherbegrenzung**: Retention-Policies mit auto-purge
- ✅ **Recht auf Vergessenwerden**: Purge-Handler löscht Daten unwiderruflich
- ✅ **Privacy by Design**: Verschlüsselung per Governance-Policy erzwungen
- ✅ **Sicherheit der Verarbeitung**: AES-256-GCM + PKI-Signierung
- ✅ **Rechenschaftspflicht**: Audit-Logs mit Zeitstempel + Integrität

### eIDAS (Vertrauensdienste)

- ✅ **Signatur**: PKI-Client für qualifizierte Signaturen
- ✅ **Zeitstempel**: Audit-Events mit präziser Zeiterfassung
- ✅ **Langzeitarchivierung**: Archive-Handler für 7-10 Jahre Retention
- ✅ **Nachweisbarkeit**: Encrypt-then-Sign für manipulationssichere Logs

### HGB §257 (Aufbewahrungsfristen)

- ✅ **Geschäftsbriefe**: 6 Jahre (konfigurierbar via YAML)
- ✅ **Buchungsbelege**: 10 Jahre (transaction_logs Policy)
- ✅ **Inventare**: 10 Jahre (inventory Policy)

---

## 7. Monitoring & Debugging

### Governance-Metriken

```bash
curl http://localhost:8765/metrics | grep governance
```

```
themis_governance_requests_total{classification="geheim"} 1523
themis_governance_encryption_enforced_total 892
themis_governance_ann_blocked_total 234
```

### Retention-Statistiken

```cpp
auto stats = retention_mgr.getPolicyStats("user_personal_data");
std::cout << "Archived: " << stats.archived_count << "\n";
std::cout << "Purged: " << stats.purged_count << "\n";
std::cout << "Retained: " << stats.retained_count << "\n";
```

### Audit-Log-Analyse

```bash
# Anzahl PII-Detections
grep "PII_DETECTED" data/logs/audit.jsonl | wc -l

# Retention-Aktionen
grep "RETENTION_" data/logs/retention_audit.jsonl | jq '.action' | sort | uniq -c
```

---

## 8. Best Practices

### 1. Klassifizierung früh festlegen
```cpp
// Bei Entity-Erstellung
entity.setField("_classification", "geheim");
entity.setField("_created_at", std::time(nullptr));
```

### 2. PII-Detection in CI/CD
```bash
# Pre-commit Hook
./bin/pii_scan --config=pii_detection.yaml --input=dump.json
```

### 3. Retention-Tests
```cpp
// Unit-Test für Policy-Compliance
TEST(RetentionTest, GDPR_Article17_RightToErasure) {
    RetentionManager mgr;
    auto policy = mgr.getPolicy("user_personal_data");
    ASSERT_TRUE(policy->auto_purge_enabled);
    ASSERT_EQ(policy->retention_period, std::chrono::hours(24 * 365));
}
```

### 4. Audit-Log-Rotation
```bash
# Logrotate-Config
/var/log/themis/audit.jsonl {
    daily
    rotate 365
    compress
    delaycompress
    notifempty
    create 0640 themis themis
}
```

---

## 9. Troubleshooting

### PII nicht erkannt

**Problem**: Kreditkarte `4532-1234-5678-9010` wird nicht erkannt

**Lösung**: Luhn-Validierung prüfen
```cpp
auto is_valid = vcc::validateLuhn("4532123456789010"); // true
auto is_invalid = vcc::validateLuhn("1234123412341234"); // false
```

### Retention läuft nicht

**Problem**: Keine Retention-Logs im Server

**Lösung**: Config prüfen
```json
{
  "features": {
    "retention": {
      "enabled": true  // <- muss true sein
    }
  }
}
```

### Audit-Log verschlüsselt nicht

**Problem**: Plaintext in audit.jsonl

**Lösung**: encrypt_then_sign aktivieren
```cpp
AuditLoggerConfig cfg;
cfg.encrypt_then_sign = true; // <- wichtig!
cfg.enabled = true;
```

---

## 10. Weiterführende Ressourcen

- [DSGVO-Volltext](https://dsgvo-gesetz.de/)
- [eIDAS-Verordnung](https://eur-lex.europa.eu/legal-content/DE/TXT/?uri=CELEX:32014R0910)
- [BSI IT-Grundschutz](https://www.bsi.bund.de/DE/Themen/Unternehmen-und-Organisationen/Standards-und-Zertifizierung/IT-Grundschutz/it-grundschutz_node.html)
- [HGB Aufbewahrungsfristen](https://www.gesetze-im-internet.de/hgb/__257.html)

---

**Version**: 0.1.0  
**Letztes Update**: 1. November 2025  
**Maintainer**: Themis Compliance Team
