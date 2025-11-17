# PKI Signatures - Technical Reference

**Status:** ✅ Produktiv  
**Version:** 1.0 (November 2025)  
**Compliance:** eIDAS Art. 26-35, DSGVO Art. 32

---

## Überblick

Die PKI-Signatur-Implementierung in ThemisDB ermöglicht **kryptographisch sichere digitale Signaturen** für:
- **Audit-Logs** (Manipulationsschutz)
- **SAGA-Transaktionen** (Nachvollziehbarkeit)
- **Content-Blobs** (Authentizität)
- **Backup-Manifeste** (Integritätssicherung)

---

## Signatur-Algorithmen

### Unterstützte Algorithmen

| Algorithmus | Hash-Funktion | Key-Größe | Output-Größe | Empfehlung |
|-------------|---------------|-----------|--------------|------------|
| **RSA-SHA256** | SHA-256 | 2048-4096 bit | 256-512 Bytes | ✅ Standard (Produktion) |
| **RSA-SHA384** | SHA-384 | 2048-4096 bit | 256-512 Bytes | ✅ Höhere Sicherheit |
| **RSA-SHA512** | SHA-512 | 4096 bit | 512 Bytes | ✅ eIDAS-qualifiziert |

**Konfiguration:**
```bash
export THEMIS_PKI_SIGNING_ALGORITHM=RSA-SHA256  # Default
export THEMIS_PKI_SIGNING_ALGORITHM=RSA-SHA512  # Für eIDAS-konforme Signaturen
```

---

## Signatur-Workflow

### 1. Sign (Signieren)

```
┌─────────────────────┐
│   Input Data        │
│   (arbitrary bytes) │
└──────────┬──────────┘
           │
           │ SHA256/384/512
           ▼
┌─────────────────────┐
│   Hash (32/48/64 B) │
└──────────┬──────────┘
           │
           │ RSA Private Key Sign
           ▼
┌─────────────────────┐
│  Signature (Base64) │
└──────────┬──────────┘
           │
           │ Attach Metadata
           ▼
┌─────────────────────────────────────┐
│  SignatureResult                    │
│  - signature_b64: "ABC..."          │
│  - algorithm: "RSA-SHA256"          │
│  - cert_serial: "1A2B3C4D"          │
│  - signature_id: "sig_a1b2c3d4"     │
│  - timestamp: 1731868800000         │
└─────────────────────────────────────┘
```

### 2. Verify (Verifizieren)

```
┌─────────────────────┐
│   Input Data        │
│   (same as signed)  │
└──────────┬──────────┘
           │
           │ SHA256/384/512
           ▼
┌─────────────────────┐
│   Hash (computed)   │
└──────────┬──────────┘
           │
           │ RSA Public Key Verify
           │ against signature_b64
           ▼
┌─────────────────────┐
│  Valid: true/false  │
└─────────────────────┘
```

---

## C++ API Reference

### Sign

```cpp
#include "utils/pki_client.h"

VCCPKIClient pki(cfg);

// Daten vorbereiten
std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};

// Signieren
SignatureResult result = pki.sign(data);

if (result.ok) {
    std::cout << "Signature: " << result.signature_b64 << std::endl;
    std::cout << "Algorithm: " << result.algorithm << std::endl;
    std::cout << "Cert Serial: " << result.cert_serial << std::endl;
    std::cout << "Timestamp: " << result.timestamp << std::endl;
} else {
    std::cerr << "Signing failed: " << result.error_message << std::endl;
}
```

**Output:**
```
Signature: cGFkZGluZy4uLnNpZ25hdHVyZS4uLnBhZGRpbmc=
Algorithm: RSA-SHA256
Cert Serial: 1A2B3C4D5E6F
Timestamp: 1731868800000
```

---

### Verify

```cpp
#include "utils/pki_client.h"

VCCPKIClient pki(cfg);

std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
std::string signature_b64 = "cGFkZGluZy4uLnNpZ25hdHVyZS4uLnBhZGRpbmc=";

// Verifizieren
bool valid = pki.verify(data, signature_b64);

if (valid) {
    std::cout << "✅ Signature is valid" << std::endl;
} else {
    std::cout << "❌ Signature is INVALID" << std::endl;
}
```

---

### Sign Hash (Erweitert)

Für bereits gehashte Daten (z.B. von externer Quelle):

```cpp
// SHA256-Hash bereits berechnet
std::vector<uint8_t> hash_bytes = {
    0x2c, 0xf2, 0x4d, 0xba, // ... 32 Bytes total
};

SignatureResult result = pki.signHash(hash_bytes);
```

**Achtung:** Hash-Länge muss zum Algorithmus passen:
- SHA256: 32 Bytes
- SHA384: 48 Bytes
- SHA512: 64 Bytes

---

### Verify Hash (Erweitert)

```cpp
std::vector<uint8_t> hash_bytes = {/* 32 bytes */};
std::string signature_b64 = "...";

bool valid = pki.verifyHash(hash_bytes, signature_b64);
```

---

## HTTP API Reference

### `POST /api/pki/sign`

**Request:**
```bash
curl -X POST http://localhost:8080/api/pki/sign \
  -H "Content-Type: application/json" \
  -d '{
    "data": "SGVsbG8gV29ybGQ=",
    "algorithm": "RSA-SHA256"
  }'
```

**Response:**
```json
{
  "signature_b64": "cGFkZGluZy4uLnNpZ25hdHVyZS4uLnBhZGRpbmc=",
  "signature_id": "sig_a1b2c3d4",
  "algorithm": "RSA-SHA256",
  "cert_serial": "1A2B3C4D5E6F",
  "timestamp": 1731868800000,
  "ok": true
}
```

---

### `POST /api/pki/verify`

**Request:**
```bash
curl -X POST http://localhost:8080/api/pki/verify \
  -H "Content-Type: application/json" \
  -d '{
    "data": "SGVsbG8gV29ybGQ=",
    "signature_b64": "cGFkZGluZy4uLnNpZ25hdHVyZS4uLnBhZGRpbmc=",
    "algorithm": "RSA-SHA256"
  }'
```

**Response:**
```json
{
  "valid": true,
  "cert_serial": "1A2B3C4D5E6F",
  "message": "Signature verification successful"
}
```

---

### `GET /api/pki/certificate`

**Request:**
```bash
curl http://localhost:8080/api/pki/certificate
```

**Response:**
```json
{
  "certificate": "-----BEGIN CERTIFICATE-----\nMIIDXTCCAkWgAwIBAgIJAKl...\n-----END CERTIFICATE-----",
  "serial": "1A2B3C4D5E6F",
  "subject": "/C=DE/ST=Bavaria/O=ThemisDB/CN=themis.example.com",
  "issuer": "/C=DE/O=ThemisDB CA/CN=ThemisDB Root CA",
  "valid_from": "2025-01-01T00:00:00Z",
  "valid_to": "2035-01-01T00:00:00Z",
  "key_size": 4096
}
```

---

## Audit-Log-Integration

### Encrypt-then-Sign Pattern

Audit-Logs verwenden das **Encrypt-then-Sign**-Muster für Compliance:

```
┌─────────────────┐
│  Sensitive Data │
└────────┬────────┘
         │
         │ AES-256-GCM Encrypt
         ▼
┌─────────────────┐
│ Encrypted Data  │
└────────┬────────┘
         │
         │ Serialize to JSON
         ▼
┌─────────────────┐
│  JSON Log Entry │
└────────┬────────┘
         │
         │ RSA-SHA256 Sign
         ▼
┌─────────────────────────┐
│ Log Entry + Signature   │
│ RocksDB: saga_log:*     │
│ RocksDB: saga_signature:│
└─────────────────────────┘
```

### Beispiel: SAGA Logger

```cpp
#include "server/saga_logger.h"

SAGALogger logger(field_encryption, pki_client, config);

// Log-Operation (automatisch encrypted + signed)
logger.logOperation(
    "saga_001",                     // SAGA ID
    "step_1",                       // Step ID
    "INSERT",                       // Operation
    "users",                        // Collection
    "user:alice",                   // Entity PK
    entity.toJson()                 // Entity JSON
);
```

**Gespeicherte Signatur:**
```json
{
  "saga_id": "saga_001",
  "step_id": "step_1",
  "signature": "cGFkZGluZy4uLnNpZ25hdHVyZS4uLnBhZGRpbmc=",
  "algorithm": "RSA-SHA256",
  "cert_serial": "1A2B3C4D5E6F",
  "timestamp": 1731868800000,
  "signed_data_hash": "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
}
```

**RocksDB Keys:**
- Log: `saga_log:saga_001:step_1`
- Signatur: `saga_signature:saga_001:step_1`

---

## Vault-Integration (Optional)

Für **HSM-backed Signing** kann ThemisDB HashiCorp Vault verwenden:

### Konfiguration

```bash
export THEMIS_PKI_VAULT_ADDR=https://vault.example.com:8200
export THEMIS_PKI_VAULT_TOKEN=s.abc123def456
export THEMIS_PKI_VAULT_SIGN_PATH=transit/sign/themis-key
```

### Remote-Signing-Workflow

```
┌──────────────┐
│ ThemisDB     │
└──────┬───────┘
       │
       │ POST /v1/transit/sign/themis-key
       │ { "hash_b64": "...", "algorithm": "rsa-2048" }
       ▼
┌──────────────┐
│ Vault        │
│ (HSM-backed) │
└──────┬───────┘
       │
       │ Signature (Base64)
       ▼
┌──────────────┐
│ ThemisDB     │
│ (Receive Sig)│
└──────────────┘
```

**Vorteile:**
- Private Key verbleibt im HSM
- Audit-Trail in Vault
- Multi-Tenant-Key-Management

---

## Compliance-Mapping

### eIDAS-Anforderungen

| Artikel | Anforderung | Umsetzung in ThemisDB |
|---------|-------------|----------------------|
| **Art. 26** | Qualifizierte Signatur | RSA-SHA512 mit QTSP-Zertifikat |
| **Art. 27** | Signaturprüfung | `POST /api/pki/verify` |
| **Art. 28** | Signaturformat | Base64-encoded PKCS#1 v1.5 |
| **Art. 32** | Langzeitarchivierung | Timestamp + Audit-Logs |
| **Art. 34** | Validierung | X.509-Chain-Validierung (optional) |

### DSGVO-Anforderungen

| Artikel | Anforderung | Umsetzung |
|---------|-------------|-----------|
| **Art. 32** | Sicherheit der Verarbeitung | Encrypt-then-Sign, RSA-2048+ |
| **Art. 25** | Datenschutz durch Technik | PKI-basierte Authentizität |
| **Art. 5(1)(f)** | Integrität und Vertraulichkeit | Signatur-basierte Tamper-Evidence |

---

## Performance-Benchmarks

### Sign-Operationen

| Key-Größe | Algorithmus | Ops/Sekunde | Latenz (ms) |
|-----------|-------------|-------------|-------------|
| 2048 bit | RSA-SHA256 | ~500 | ~2 |
| 4096 bit | RSA-SHA256 | ~150 | ~7 |
| 4096 bit | RSA-SHA512 | ~145 | ~7 |

**Hardware:** Intel Xeon E5-2680 v4 (2.4 GHz), 64 GB RAM

### Verify-Operationen

| Key-Größe | Algorithmus | Ops/Sekunde | Latenz (ms) |
|-----------|-------------|-------------|-------------|
| 2048 bit | RSA-SHA256 | ~15,000 | ~0.07 |
| 4096 bit | RSA-SHA256 | ~6,000 | ~0.17 |
| 4096 bit | RSA-SHA512 | ~5,900 | ~0.17 |

**Optimierung:** Verify ist ~30x schneller als Sign (Public Key Operation)

---

## Fehlerbehandlung

### Fehlerszenarien

#### 1. Invalid Private Key

**Symptom:**
```
[ERROR] PKI Client: Failed to load private key from /etc/themis/pki/service.key
```

**Lösung:**
```bash
# Prüfe Key-Format
openssl rsa -in /etc/themis/pki/service.key -check -noout

# Konvertiere falls nötig
openssl rsa -in service.key.old -out service.key -outform PEM
```

---

#### 2. Signature Verification Failed

**Symptom:**
```
[WARN] PKI Client: Signature verification failed for data hash
```

**Ursachen:**
- Daten wurden modifiziert
- Falscher Public Key
- Algorithmus-Mismatch

**Debug:**
```cpp
// Enable debug-Logging
std::setenv("THEMIS_DEBUG_PKI", "1", 1);

// Manuell verifizieren
openssl dgst -sha256 -verify pubkey.pem -signature sig.bin data.bin
```

---

#### 3. Certificate Serial Mismatch

**Symptom:**
```
[ERROR] PKI Client: Certificate serial does not match expected value
```

**Lösung:**
```bash
# Zeige Zertifikats-Serial
openssl x509 -in service.crt -serial -noout

# Erwarteter Serial muss mit PKI-Config übereinstimmen
```

---

## Best Practices

### 1. Key-Größe

- **Minimum:** 2048 bit (für Entwicklung)
- **Empfohlen:** 4096 bit (für Produktion)
- **eIDAS-qualifiziert:** 4096 bit mit SHA-512

### 2. Signatur-Algorithmus

- **Standard:** RSA-SHA256 (Balance aus Sicherheit und Performance)
- **High-Security:** RSA-SHA512 (eIDAS-konform)
- **Legacy:** Avoid SHA1 (deprecated)

### 3. Timestamp-Handling

Speichere immer Timestamp mit Signatur:
```cpp
SignatureResult result = pki.sign(data);
// result.timestamp ist automatisch gesetzt (Unix-Millisekunden)
```

### 4. Certificate Rotation

Bei Zertifikats-Rotation:
1. Alte Signaturen bleiben verifizierbar (mit altem Zertifikat)
2. Neue Signaturen verwenden neues Zertifikat
3. Speichere Zertifikats-Serial mit jeder Signatur

### 5. Audit-Trail

Jede Signatur-Operation sollte geloggt werden:
```cpp
logger.logSignatureOperation(
    signature_id,
    algorithm,
    cert_serial,
    data_hash,
    timestamp
);
```

---

## Testing

### Unit-Tests

```cpp
#include <gtest/gtest.h>
#include "utils/pki_client.h"

TEST(PKIClientTest, SignAndVerify) {
    PKIConfig cfg = PKIConfig::fromEnvironment();
    VCCPKIClient pki(cfg);
    
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
    
    auto result = pki.sign(data);
    ASSERT_TRUE(result.ok);
    ASSERT_FALSE(result.signature_b64.empty());
    
    bool valid = pki.verify(data, result.signature_b64);
    EXPECT_TRUE(valid);
}

TEST(PKIClientTest, VerifyFailsOnModifiedData) {
    PKIConfig cfg = PKIConfig::fromEnvironment();
    VCCPKIClient pki(cfg);
    
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
    auto result = pki.sign(data);
    
    // Modify data
    data[0] = 'h';
    
    bool valid = pki.verify(data, result.signature_b64);
    EXPECT_FALSE(valid); // Should fail
}
```

### Integration-Tests

```bash
# Test mit echtem Zertifikat
export THEMIS_PKI_PRIVATE_KEY=/tmp/test.key
export THEMIS_PKI_CERTIFICATE=/tmp/test.crt

# Generiere Test-Zertifikat
openssl genrsa -out /tmp/test.key 2048
openssl req -new -x509 -key /tmp/test.key -out /tmp/test.crt -days 365 \
    -subj "/C=DE/O=ThemisDB/CN=test"

# Run Tests
./build/tests/test_pki_client
```

---

## Referenzen

- **Standards:**
  - eIDAS Regulation (EU) No 910/2014
  - PKCS#1 v2.1: RSA Cryptography Standard (RFC 3447)
  - X.509 v3 Certificate Standard (RFC 5280)
  - TSP (Time-Stamp Protocol) RFC 3161

- **Dateien:**
  - `src/utils/pki_client.cpp` - Core Implementation
  - `include/utils/pki_client.h` - Public API
  - `src/server/pki_api_handler.cpp` - HTTP-Endpunkte
  - `tests/test_pki_client.cpp` - Unit-Tests

- **Dependencies:**
  - OpenSSL 1.1+ (EVP, RSA, X.509, PEM)
  - libcurl 7.x+ (Vault-Integration)
  - nlohmann/json 3.x+ (JSON-Serialisierung)

---

**Letzte Aktualisierung:** 17. November 2025  
**Version:** 1.0  
**Autor:** ThemisDB Development Team
