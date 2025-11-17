# PKI Integration Architecture

**Status:** ✅ Produktiv (mit ENV-Konfiguration) | ⚙️ Stub-Modus (Development)  
**Version:** 1.0 (November 2025)  
**Compliance:** eIDAS-konform, DSGVO Art. 32, HGB § 257

---

## Überblick

ThemisDB integriert eine **Public Key Infrastructure (PKI)** für kryptographische Signaturen und Zertifikats-basiertes Key-Management. Die Implementierung erfüllt **eIDAS-Anforderungen** für qualifizierte elektronische Signaturen und unterstützt **Encrypt-then-Sign**-Workflows für Audit-Logs und SAGA-Transaktionen.

### Architektur-Komponenten

```
┌──────────────────────────────────────────────────────────────┐
│                    ThemisDB PKI Stack                         │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌────────────────┐   ┌──────────────────┐   ┌────────────┐ │
│  │  PKI Client    │───│  PKI Key Provider│───│ Vault/HSM  │ │
│  │  (Signing)     │   │  (KEK Derivation)│   │ (Optional) │ │
│  └────────────────┘   └──────────────────┘   └────────────┘ │
│         │                       │                             │
│         │                       │                             │
│  ┌──────▼───────────────────────▼───────────────────────┐   │
│  │         OpenSSL (RSA-SHA256/384/512)                 │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Certificate Store (X.509 PEM)                       │   │
│  │  - Private Key (RSA 2048/4096)                       │   │
│  │  - Public Certificate (with Serial Number)           │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

---

## Core Components

### 1. PKIClient

**Datei:** `src/utils/pki_client.cpp` | `include/utils/pki_client.h`

#### Funktionalität
- **Sign:** RSA-basierte digitale Signaturen (SHA256/SHA384/SHA512)
- **Verify:** Signaturverifizierung mit Public Key
- **Certificate Handling:** X.509-Zertifikatsverarbeitung
- **Dual-Mode:** Produktiv (OpenSSL) vs. Stub (Base64, nur Dev)

#### Konfiguration

```cpp
struct PKIConfig {
    std::string key_path;          // Path to RSA private key (PEM)
    std::string cert_path;         // Path to X.509 certificate (PEM)
    std::string key_passphrase;    // Optional passphrase for encrypted keys
    std::string vault_addr;        // Optional: HashiCorp Vault address
    std::string vault_token;       // Optional: Vault access token
    std::string signing_algorithm; // "RSA-SHA256", "RSA-SHA384", "RSA-SHA512"
};
```

#### ENV-Variablen (Produktiv-Modus)

| Variable | Beschreibung | Beispiel |
|----------|--------------|----------|
| `THEMIS_PKI_PRIVATE_KEY` | Pfad zum RSA Private Key | `/etc/themis/pki/service.key` |
| `THEMIS_PKI_CERTIFICATE` | Pfad zum X.509 Zertifikat | `/etc/themis/pki/service.crt` |
| `THEMIS_PKI_KEY_PASSPHRASE` | Passphrase für verschlüsselte Keys (optional) | `SecurePass123!` |
| `THEMIS_PKI_VAULT_ADDR` | Vault-Server-URL (optional) | `https://vault.example.com:8200` |
| `THEMIS_PKI_VAULT_TOKEN` | Vault Access Token (optional) | `s.abcdef123456` |
| `THEMIS_PKI_SIGNING_ALGORITHM` | Signatur-Algorithmus | `RSA-SHA256` (default) |

**Fallback:** Wenn `THEMIS_PKI_PRIVATE_KEY` **nicht** gesetzt ist, aktiviert sich automatisch der **Stub-Modus** (Base64-Dummy-Signaturen, nur für Development).

#### API-Beispiel

```cpp
#include "utils/pki_client.h"

// Konfiguration aus ENV
PKIConfig cfg = PKIConfig::fromEnvironment();

// PKI Client erstellen
PKIClient pki(cfg);

// Daten signieren
std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
auto result = pki.sign(data);

if (result.success) {
    std::cout << "Signature: " << result.signature_b64 << std::endl;
    std::cout << "Algorithm: " << result.algorithm << std::endl;
    std::cout << "Cert Serial: " << result.cert_serial << std::endl;
}

// Signatur verifizieren
bool valid = pki.verify(data, result.signature_b64);
std::cout << "Valid: " << valid << std::endl;
```

---

### 2. PKIKeyProvider

**Datei:** `src/security/pki_key_provider.cpp` | `include/security/pki_key_provider.h`

#### Funktionalität
- **KEK-Ableitung aus PKI-Zertifikat:** Verwendet X.509-Serial und Subject-DN als HKDF-Material
- **Produktions-Key-Hierarchie:** Master KEK → Collection KEK → Field DEK
- **Vault-Integration:** Optional HSM-backed Key-Storage über HashiCorp Vault

#### Key-Derivation Flow

```
┌──────────────────────────────────────────────────────────────┐
│  X.509 Certificate (Subject DN + Serial Number)             │
└───────────────────────┬──────────────────────────────────────┘
                        │
                        │ HKDF-SHA256
                        ▼
            ┌────────────────────────┐
            │   Master KEK (256 bit) │
            └───────────┬────────────┘
                        │
                        │ HKDF-SHA256 (salt: collection_name)
                        ▼
            ┌────────────────────────┐
            │  Collection KEK        │
            └───────────┬────────────┘
                        │
                        │ HKDF-SHA256 (salt: entity_id + field_name)
                        ▼
            ┌────────────────────────┐
            │   Field DEK (256 bit)  │
            └────────────────────────┘
                        │
                        │ AES-256-GCM
                        ▼
            ┌────────────────────────┐
            │   Encrypted Field Data │
            └────────────────────────┘
```

#### Beispiel-Integration

```cpp
#include "security/pki_key_provider.h"

// PKI-Config laden
PKIConfig pki_cfg = PKIConfig::fromEnvironment();

// PKIKeyProvider erstellen
auto pki_kp = std::make_shared<PKIKeyProvider>(
    pki_cfg.cert_path,
    pki_cfg.vault_addr,
    pki_cfg.vault_token
);

// Master KEK ableiten
auto kek = pki_kp->getKey("master_kek");

// Collection-spezifischen Key ableiten
auto collection_key = pki_kp->deriveCollectionKey("master_kek", "users");
```

---

### 3. Audit-Log-Signierung (Encrypt-then-Sign)

**Integration:** `src/server/saga_logger.cpp`

#### Workflow

1. **Encryption:** Sensitive Felder werden mit AES-256-GCM verschlüsselt
2. **Serialization:** JSON-Serialisierung des Audit-Log-Eintrags
3. **Signing:** RSA-SHA256-Signatur über serialisierte Daten
4. **Storage:** Log-Eintrag + Signatur werden in RocksDB persistiert

#### Beispiel

```cpp
#include "server/saga_logger.h"
#include "utils/pki_client.h"

// SAGA Logger mit PKI-Client
SAGALogger logger(field_encryption, pki_client, cfg);

// Log-Eintrag erstellen (wird automatisch signiert)
logger.logOperation(saga_id, step_id, "INSERT", 
                   "users", entity_pk, entity_json);

// Signatur wird in RocksDB unter `saga_signature:` gespeichert
```

#### Signatur-Format (JSON)

```json
{
  "signature": "base64_encoded_rsa_signature",
  "algorithm": "RSA-SHA256",
  "cert_serial": "1A2B3C4D5E6F",
  "timestamp": 1731868800000,
  "signed_data_hash": "sha256_of_original_data"
}
```

---

### 4. LEK Manager (Lawful Evidence Key Management)

**Datei:** `src/security/lek_manager.cpp` | `include/security/lek_manager.h`

#### Funktionalität
- **LEK-Wrapping:** Verschlüsselung von Log-Encryption-Keys mit KEK aus PKI
- **Key-Rotation:** Unterstützt LEK-Rotation für Compliance-Anforderungen
- **Escrow:** Optionales Key-Escrow für Behördenzugriff (DSGVO Art. 23)

#### API

```cpp
#include "security/lek_manager.h"

LEKManager lek_mgr(db, pki_client, key_provider);

// LEK erstellen und mit KEK wrappen
auto lek = lek_mgr.createLEK("audit_logs_2025");

// LEK abrufen und entschlüsseln
auto unwrapped_lek = lek_mgr.getLEK("audit_logs_2025");

// LEK rotieren (neue Version)
lek_mgr.rotateLEK("audit_logs_2025");
```

---

## Deployment-Szenarien

### Szenario 1: Development (Stub-Modus)

```bash
# Keine ENV-Variablen → automatisch Stub-Modus
./themis_server

# Logs:
# [INFO] PKI Client: Running in STUB mode (no private key configured)
# [WARN] Signatures are Base64-encoded hashes, NOT cryptographically secure!
```

**Verwendung:** Lokale Tests, CI/CD ohne echte Zertifikate

---

### Szenario 2: Produktion (Self-Signed Certificates)

```bash
# 1. Zertifikat und Key generieren
openssl genrsa -out /etc/themis/pki/service.key 4096
openssl req -new -x509 -key /etc/themis/pki/service.key \
    -out /etc/themis/pki/service.crt -days 3650 \
    -subj "/C=DE/ST=Bavaria/L=Munich/O=ThemisDB/CN=themis.example.com"

# 2. ENV konfigurieren
export THEMIS_PKI_PRIVATE_KEY=/etc/themis/pki/service.key
export THEMIS_PKI_CERTIFICATE=/etc/themis/pki/service.crt
export THEMIS_PKI_SIGNING_ALGORITHM=RSA-SHA256

# 3. Server starten
./themis_server

# Logs:
# [INFO] PKI Client: Loaded private key from /etc/themis/pki/service.key
# [INFO] PKI Client: Certificate serial: 1A2B3C4D5E6F
# [INFO] PKI Client: Algorithm: RSA-SHA256
```

---

### Szenario 3: Produktion (CA-Signed Certificates + Vault)

```bash
# 1. CA-signiertes Zertifikat erhalten
# (von interner CA oder öffentlicher CA)

# 2. Key in HashiCorp Vault speichern (optional)
vault kv put secret/themis/pki \
    private_key=@/path/to/service.key \
    certificate=@/path/to/service.crt

# 3. ENV mit Vault-Integration
export THEMIS_PKI_PRIVATE_KEY=/etc/themis/pki/service.key
export THEMIS_PKI_CERTIFICATE=/etc/themis/pki/service.crt
export THEMIS_PKI_VAULT_ADDR=https://vault.example.com:8200
export THEMIS_PKI_VAULT_TOKEN=s.abc123def456
export THEMIS_PKI_SIGNING_ALGORITHM=RSA-SHA384

# 4. Server starten
./themis_server
```

**Vorteile:** 
- CA-Chain-Validierung
- Vault-backed Key-Rotation
- HSM-Integration über Vault Transit Engine

---

### Szenario 4: eIDAS-konforme Produktion (Qualifizierte Signaturen)

**Voraussetzungen:**
- Zertifikat von **qualifiziertem Vertrauensdiensteanbieter (QTSP)**
- HSM (Hardware Security Module) für Private Key Storage
- Zeitstempel-Dienst (TSA) für Langzeitarchivierung

**Konfiguration:**

```bash
# 1. HSM-backed Key (z.B. PKCS#11)
export THEMIS_PKI_PRIVATE_KEY=/dev/pkcs11/slot0/key
export THEMIS_PKI_CERTIFICATE=/etc/themis/pki/eidas_qualified.crt
export THEMIS_PKI_SIGNING_ALGORITHM=RSA-SHA512  # Höhere Sicherheit

# 2. Optional: TSA für Zeitstempel
export THEMIS_PKI_TSA_URL=https://tsa.example.com/timestamp

# 3. Server starten
./themis_server
```

**Compliance-Mapping:**
| Anforderung | Umsetzung |
|-------------|-----------|
| eIDAS Art. 26 (Qualifizierte Signatur) | RSA-SHA512 mit QTSP-Zertifikat |
| eIDAS Art. 32 (Langzeitarchivierung) | TSA-Zeitstempel + Audit-Logs |
| eIDAS Art. 34 (Validierung) | X.509-Chain-Validierung |

---

## API-Endpunkte

### `POST /api/pki/sign`

**Beschreibung:** Signiert beliebige Daten mit PKI Private Key

**Request:**
```json
{
  "data": "SGVsbG8gV29ybGQ=",  // Base64-encoded data
  "algorithm": "RSA-SHA256"      // Optional, default from config
}
```

**Response:**
```json
{
  "signature": "base64_encoded_signature",
  "algorithm": "RSA-SHA256",
  "cert_serial": "1A2B3C4D5E6F",
  "timestamp": 1731868800000,
  "success": true
}
```

---

### `POST /api/pki/verify`

**Beschreibung:** Verifiziert Signatur mit Public Key

**Request:**
```json
{
  "data": "SGVsbG8gV29ybGQ=",
  "signature": "base64_encoded_signature",
  "algorithm": "RSA-SHA256"
}
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

**Beschreibung:** Gibt Public Certificate zurück (für Client-seitige Verifikation)

**Response:**
```json
{
  "certificate": "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----",
  "serial": "1A2B3C4D5E6F",
  "subject": "/C=DE/ST=Bavaria/O=ThemisDB/CN=themis.example.com",
  "issuer": "/C=DE/O=ThemisDB CA/CN=ThemisDB Root CA",
  "valid_from": "2025-01-01T00:00:00Z",
  "valid_to": "2035-01-01T00:00:00Z"
}
```

---

## Security-Empfehlungen

### Key-Management
1. **Private Key Protection:**
   - Niemals im Git-Repository speichern
   - Dateiberechtigungen: `chmod 400 service.key`
   - Verschlüsselte Keys mit Passphrase bevorzugen

2. **Key-Rotation:**
   - Regelmäßige Rotation (z.B. jährlich)
   - Alte Signaturen bleiben verifizierbar (behalte alte Zertifikate)

3. **HSM-Integration:**
   - Für Produktionsumgebungen: HSM-backed Keys
   - Unterstützte Standards: PKCS#11, Vault Transit Engine

### Zertifikats-Validierung
1. **Certificate Pinning:** Client-seitige Validierung der Certificate Fingerprints
2. **CRL/OCSP:** Regelmäßige Prüfung der Zertifikatswiderrufslisten
3. **Chain-Validierung:** Vollständige CA-Chain bis zum Root-Zertifikat

### Audit-Trail
- Alle Sign/Verify-Operationen werden in Audit-Logs protokolliert
- Signatur-Metadaten (Timestamp, Algorithm, Cert Serial) werden persistiert
- Tamper-Evidence durch Encrypt-then-Sign

---

## Troubleshooting

### Fehler: "Failed to load private key"

**Ursache:** Falscher Pfad oder verschlüsselter Key ohne Passphrase

**Lösung:**
```bash
# Prüfen ob Key lesbar
openssl rsa -in /path/to/service.key -check -noout

# Falls verschlüsselt: Passphrase setzen
export THEMIS_PKI_KEY_PASSPHRASE="YourPassphrase"
```

---

### Fehler: "Signature verification failed"

**Ursache:** Falscher Public Key oder Datenkorruption

**Lösung:**
```bash
# Manuell mit OpenSSL verifizieren
echo "SGVsbG8=" | base64 -d > data.bin
echo "signature_base64" | base64 -d > signature.bin

openssl dgst -sha256 -verify pubkey.pem -signature signature.bin data.bin
```

---

### Stub-Modus deaktivieren

**Problem:** Produktionsserver läuft im Stub-Modus

**Lösung:**
```bash
# Prüfe ENV-Variablen
env | grep THEMIS_PKI

# Setze fehlende Variablen
export THEMIS_PKI_PRIVATE_KEY=/etc/themis/pki/service.key
export THEMIS_PKI_CERTIFICATE=/etc/themis/pki/service.crt

# Neustart
systemctl restart themis-server
```

---

## Referenzen

- **Dateien:**
  - `src/utils/pki_client.cpp` - Core PKI Client
  - `src/security/pki_key_provider.cpp` - KEK Derivation
  - `src/server/pki_api_handler.cpp` - HTTP API
  - `src/server/saga_logger.cpp` - Audit-Log-Integration

- **Standards:**
  - eIDAS Regulation (EU) No 910/2014
  - X.509 v3 Certificate Standard (RFC 5280)
  - RSA PKCS#1 v2.1 (RFC 3447)
  - HKDF (RFC 5869)

- **Dependencies:**
  - OpenSSL 1.1+ (EVP, RSA, X.509)
  - HashiCorp Vault (optional)
  - libcurl (HTTP-Client für Vault)

---

**Letzte Aktualisierung:** 17. November 2025  
**Version:** 1.0  
**Autor:** ThemisDB Development Team
