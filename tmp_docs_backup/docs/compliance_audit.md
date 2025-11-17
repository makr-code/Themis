# Compliance & Audit: PKI-Signing und Audit Logger

## Überblick

Dieses Dokument beschreibt die ersten Schritte der Compliance- und Audit-Implementierung:
- **PKI Client**: Minimale Schnittstelle zum Signieren und Verifizieren von Hashes (Stub).
- **AuditLogger**: Encrypt-then-Sign für strukturierte Audit-Logs.

## 1. PKI Client (`VCCPKIClient`)

### Zweck
Bietet eine zentrale Schnittstelle zum Signieren von Daten-Hashes (z. B. SHA-256 über verschlüsselte Audit-Logs). Später erweiterbar auf echte PKI-Backends (HSM, Remote-Signing-Dienst).

### Konfiguration
```cpp
PKIConfig pki_cfg;
pki_cfg.service_id = "audit_service";
pki_cfg.endpoint = "https://pki.example.com/api/v1";  // optional
pki_cfg.cert_path = "/path/to/cert.pem";              // optional
pki_cfg.key_path = "/path/to/key.pem";                // optional
pki_cfg.signature_algorithm = "RSA-SHA256";           // Standard
```

### Verwendung
```cpp
auto pki = std::make_shared<VCCPKIClient>(pki_cfg);

// 1. Berechne SHA-256 über Daten
std::vector<uint8_t> hash = sha256(ciphertext);

// 2. Signiere Hash
auto sig = pki->signHash(hash);
if (sig.ok) {
    // sig.signature_id, sig.signature_b64, sig.cert_serial verfügbar
}

// 3. Verifiziere Signatur
bool valid = pki->verifyHash(hash, sig);
```

### Stub-Verhalten (aktuell)
- **signHash**: Gibt Base64-kodierten Hash zurück (kein echtes Signing).
- **verifyHash**: Vergleicht Base64(hash) mit gespeicherter Signatur.
- Für Produktion: Echte Kryptografie über OpenSSL oder HSM integrieren.

## 2. AuditLogger

### Zweck
Strukturierte, nachvollziehbare Protokollierung sicherheitskritischer Ereignisse mit **Encrypt-then-Sign**:
1. Verschlüssle Event-JSON mit AES-256-GCM (`FieldEncryption`).
2. Hash Verschlüsselter Blob (iv || ciphertext || tag).
3. Signiere Hash über PKI Client.
4. Schreibe JSONL-Record mit Payload, Signatur und Metadaten.

### Konfiguration
```cpp
AuditLoggerConfig cfg;
cfg.enabled = true;
cfg.encrypt_then_sign = true;        // Aktiviere Encrypt-then-Sign
cfg.log_path = "data/logs/audit.jsonl";
cfg.key_id = "saga_log";             // Key für Log-Verschlüsselung

auto logger = std::make_shared<AuditLogger>(field_enc, pki, cfg);
```

### Verwendung
```cpp
nlohmann::json event = {
    {"user", "admin"},
    {"action", "read"},
    {"resource", "/content/doc123"},
    {"classification", "VS-NfD"},
    {"result", "success"},
    {"ip", "192.168.1.42"}
};

logger->logEvent(event);
```

### Log-Format

#### Encrypt-then-Sign (Standard)
```json
{
  "ts": 1700000000123,
  "category": "AUDIT",
  "payload": {
    "type": "ciphertext",
    "key_id": "saga_log",
    "key_version": 1,
    "iv_b64": "abc123...",
    "ciphertext_b64": "xyz789...",
    "tag_b64": "def456..."
  },
  "signature": {
    "ok": true,
    "id": "sig_a1b2c3d4",
    "algorithm": "RSA-SHA256",
    "sig_b64": "base64(...)",
    "cert_serial": "DEMO-CERT-SERIAL"
  }
}
```

#### Plaintext-Sign (für weniger sensitive Logs)
Setze `encrypt_then_sign = false`:
```json
{
  "ts": 1700000000456,
  "category": "AUDIT",
  "payload": {
    "type": "plaintext",
    "data_b64": "eyJ1c2VyIjoidXNlcjEiLCAi..."
  },
  "signature": {
    "ok": true,
    "id": "sig_xyz",
    ...
  }
}
```

### Wiederherstellung/Verifikation
```python
# Beispiel in Python (zur Demonstration)
import json
import base64
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

with open("data/logs/audit.jsonl") as f:
    for line in f:
        record = json.loads(line)
        
        # 1. Hole Ciphertext und Signatur
        payload = record["payload"]
        signature = record["signature"]
        
        # 2. Verifiziere Signatur über hash(iv||ct||tag)
        iv = base64.b64decode(payload["iv_b64"])
        ct = base64.b64decode(payload["ciphertext_b64"])
        tag = base64.b64decode(payload["tag_b64"])
        
        to_verify = iv + ct + tag
        hash_bytes = sha256(to_verify)
        
        # Verifikation via PKI (hier stub):
        # verify_signature(hash_bytes, signature["sig_b64"])
        
        # 3. Entschlüssele mit Key
        key = get_key(payload["key_id"], payload["key_version"])
        cipher = Cipher(algorithms.AES(key), modes.GCM(iv, tag))
        decryptor = cipher.decryptor()
        plaintext = decryptor.update(ct) + decryptor.finalize()
        
        event = json.loads(plaintext)
        print(event)  # Original Event
```

## Integration in HTTP Server

### Automatische Initialisierung
Der HTTP-Server initialisiert den AuditLogger automatisch beim Start:

```cpp
// Aus HttpServer::HttpServer() Konstruktor:
auto key_provider = std::make_shared<MockKeyProvider>();
key_provider->createKey("saga_log", 1);
auto field_enc = std::make_shared<FieldEncryption>(key_provider);

themis::utils::PKIConfig pki_cfg;
pki_cfg.service_id = "themis_server";
pki_cfg.signature_algorithm = "RSA-SHA256";
auto pki_client = std::make_shared<themis::utils::VCCPKIClient>(pki_cfg);

themis::utils::AuditLoggerConfig audit_cfg;
audit_cfg.enabled = true;
audit_cfg.encrypt_then_sign = true;
audit_cfg.log_path = "data/logs/audit.jsonl";
audit_cfg.key_id = "saga_log";

audit_logger_ = std::make_shared<themis::utils::AuditLogger>(
    field_enc, pki_client, audit_cfg);

// Verbinde mit PolicyEngine
policy_engine_->setAuditLogger(audit_logger_);
```

### Automatisches Logging durch PolicyEngine
Die `PolicyEngine` loggt automatisch alle Enforcement-Entscheidungen:

```cpp
// In PolicyEngine::evaluate():
if (audit_logger_ && d.mode == "enforce") {
    nlohmann::json audit_event = {
        {"event_type", "policy_evaluation"},
        {"route", route},
        {"classification", d.classification},
        {"mode", d.mode},
        {"require_content_encryption", d.require_content_encryption},
        {"encrypt_logs", d.encrypt_logs},
        {"redaction", d.redaction},
        {"retention_days", d.retention_days},
        {"timestamp", getCurrentTimeMs()}
    };
    
    if (headers.find("X-User-Id") != headers.end()) {
        audit_event["user_id"] = headers["X-User-Id"];
    }
    
    audit_logger_->logEvent(audit_event);
}
```

### Szenario: Log alle VS-NfD+ Content-Zugriffe
```cpp
// In handleContentImport:
if (audit_logger_ && (pdec.classification == "vs-nfd" || 
                      pdec.classification == "geheim" || 
                      pdec.classification == "streng-geheim")) {
    nlohmann::json audit_event = {
        {"event_type", "content_import"},
        {"classification", pdec.classification},
        {"mode", pdec.mode},
        {"require_encryption", pdec.require_content_encryption},
        {"content_id", content_id},
        {"timestamp", getCurrentTimeMs()}
    };
    
    if (user_id_header.has_value()) {
        audit_event["user_id"] = user_id_header.value();
    }
    
    audit_logger_->logEvent(audit_event);
}
```

### Szenario: Blob-Zugriff Logging
```cpp
// In handleGetContentBlob:
if (audit_logger_) {
    nlohmann::json audit_event = {
        {"event_type", "content_blob_access"},
        {"content_id", id},
        {"timestamp", getCurrentTimeMs()}
    };
    
    // Optional: User-ID aus Headers extrahieren
    if (req.contains("X-User-Id")) {
        audit_event["user_id"] = req["X-User-Id"];
    }
    
    audit_logger_->logEvent(audit_event);
}
```

## Retention & Archivierung (Roadmap)

### Retention Manager
```cpp
RetentionManager mgr(cfg.log_path, policy_engine);

// Archiviere Logs älter als 90 Tage (nach Klassifikation)
mgr.archiveOldLogs(90);

// Lösche nach 7 Jahren (gesetzlich)
mgr.purgeExpiredLogs();
```

### Signatur-Verkettung (Chain-of-Custody)
```cpp
// In logEvent:
// Berechne Hash über vorherigem Record + aktuellem Event
auto prev_hash = readLastRecordHash();
auto chain_input = prev_hash + current_event_json;
auto sig = pki_->signHash(sha256(chain_input));
```

## Testen
```bash
# AuditLogger Tests
ctest -C Release -R "^AuditLoggerTest\." --output-on-failure

# Governance + Audit Integration Tests
ctest -C Release -R "^HttpGovernanceTest\.|^AuditLoggerTest\." --output-on-failure
```

**Alle Tests bestehen** (19/19):
- AuditLogger-Tests (4/4):
  - `EncryptThenSignFlow`: Verschlüsselter Log mit Signatur
  - `PlaintextSignFlow`: Nur signierter Log
  - `DisabledLogger`: Logging deaktiviert (keine Datei)
  - `MultipleEvents`: Mehrere Events in JSONL
- Governance-Tests (15/15): Alle Klassifikationen, Resource-Mappings und Policy-Header validiert

### Live-Test: Audit-Logs überprüfen
```bash
# Server starten
./themis_server

# Content mit VS-NfD klassifizieren
curl -X POST http://localhost:8080/content/import \
  -H "Content-Type: application/json" \
  -H "X-Classification: VS-NfD" \
  -H "X-User-Id: admin" \
  -d '{
    "content": {
      "id": "test123",
      "mime_type": "text/plain",
      "category": "document"
    },
    "blob": "Sensitive document content"
  }'

# Audit-Log prüfen
cat data/logs/audit.jsonl
```

Erwartete Log-Einträge:
1. **Policy Evaluation** (automatisch durch PolicyEngine):
   ```json
   {
     "ts": 1700000000123,
     "category": "AUDIT",
     "payload": {
       "type": "ciphertext",
       "key_id": "saga_log",
       "key_version": 1,
       "iv_b64": "...",
       "ciphertext_b64": "...",
       "tag_b64": "..."
     },
     "signature": {
       "ok": true,
       "id": "sig_abc123",
       "algorithm": "RSA-SHA256",
       "sig_b64": "...",
       "cert_serial": "DEMO-CERT-SERIAL"
     }
   }
   ```

2. **Content Import** (bei VS-NfD+ Klassifikation):
   - Ähnliche Struktur mit verschlüsseltem Event-JSON

Entschlüsseltes Event-JSON (für Demonstration):
```json
{
  "event_type": "policy_evaluation",
  "route": "/content/import",
  "classification": "vs-nfd",
  "mode": "enforce",
  "require_content_encryption": true,
  "encrypt_logs": true,
  "redaction": "standard",
  "retention_days": 365,
  "timestamp": 1700000000123,
  "user_id": "admin"
}
```

## Nächste Schritte

1. **Echtes PKI-Signing**: OpenSSL RSA-Signaturen über SHA-256-Hash.
2. **PII Detection**: Automatische Erkennung und Markierung von PII-Feldern in Logs.
3. **Retention Manager**: Automatische Archivierung/Löschung basierend auf `retention_days`.
4. **Governance Integration**: Automatisches Logging bei enforce-Verstößen.
5. **Redaction**: PII-Redaction in Logs für niedrigere Klassifikationen.
6. **Compliance Reports**: Aggregierte Audit-Reports für Compliance-Checks.

## Referenzen
- `include/utils/pki_client.h`
- `src/utils/pki_client.cpp`
- `include/utils/audit_logger.h`
- `src/utils/audit_logger.cpp`
- `tests/test_audit_logger.cpp`
