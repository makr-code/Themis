# eIDAS Qualified Electronic Signatures

## Overview

ThemisDB implements **eIDAS-compliant qualified electronic signatures** (QES) by combining hardware-backed signing (HSM via PKCS#11) with cryptographic timestamps (RFC 3161 TSA). This provides legally binding signatures recognized across the EU.

## What is eIDAS?

**eIDAS** (electronic IDentification, Authentication and trust Services) is the EU regulation (910/2014) establishing a framework for electronic signatures, seals, timestamps, and other trust services.

**Qualified Electronic Signatures** are the highest level of electronic signatures under eIDAS:
- Legally equivalent to handwritten signatures
- Require qualified certificates from QTSPs (Qualified Trust Service Providers)
- Must be created using hardware-backed Secure Signature Creation Devices (SSCD)
- Must include cryptographic timestamps to ensure long-term validity

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    ThemisDB PKI API                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌───────────────┐  ┌────────────────┐  ┌───────────────┐ │
│  │ HSMProvider   │  │ Timestamp      │  │ PkiApiHandler │ │
│  │ (PKCS#11)     │  │ Authority      │  │               │ │
│  │               │  │ (RFC 3161)     │  │ eIDAS Logic   │ │
│  └───────┬───────┘  └────────┬───────┘  └───────┬───────┘ │
│          │                   │                  │          │
└──────────┼───────────────────┼──────────────────┼──────────┘
           │                   │                  │
           ▼                   ▼                  ▼
  ┌─────────────────┐ ┌──────────────┐  ┌──────────────────┐
  │ Hardware HSM    │ │ FreeTSA /    │  │ HTTP REST API    │
  │ (SoftHSM2,      │ │ Enterprise   │  │                  │
  │  Luna, CloudHSM)│ │ TSA Server   │  │ /api/pki/eidas/* │
  └─────────────────┘ └──────────────┘  └──────────────────┘
```

## Components

### 1. HSMProvider (PKCS#11)

Provides hardware-backed signing using industry-standard PKCS#11 interface:

**Supported HSMs:**
- **SoftHSM2** (Development/Testing)
- **Thales Luna** (Production)
- **AWS CloudHSM** (Cloud Production)
- **Utimaco SecurityServer** (Production)
- **YubiHSM 2** (Small deployments)

**Key Features:**
- Private keys never leave HSM
- FIPS 140-2 Level 2/3 compliance
- Multi-user PIN/passphrase protection
- Audit logging
- Key backup/recovery

See: [HSM Integration Guide](hsm_integration.md)

### 2. TimestampAuthority (RFC 3161)

Provides cryptographic timestamps:

**Supported TSA Services:**
- **FreeTSA** (Free, public service)
- **Enterprise TSA** (e.g., Sectigo, DigiCert)
- **Self-hosted TSA** (OpenSSL based)

**Key Features:**
- SHA-256/384/512 hash algorithms
- Nonce generation for replay protection
- Certificate validation
- eIDAS timestamp validation
- Long-term timestamp verification

**Timestamp Token Structure (ASN.1):**
```asn1
TimeStampToken ::= ContentInfo
  -- contentType is id-signedData
  -- content is SignedData containing TSTInfo

TSTInfo ::= SEQUENCE {
  version                 INTEGER,
  policy                  TSAPolicyId,
  messageImprint          MessageImprint,
  serialNumber            INTEGER,
  genTime                 GeneralizedTime,
  accuracy                Accuracy OPTIONAL,
  ordering                BOOLEAN DEFAULT FALSE,
  nonce                   INTEGER OPTIONAL,
  tsa                     GeneralName OPTIONAL,
  extensions              Extensions OPTIONAL
}
```

### 3. PkiApiHandler

Orchestrates eIDAS qualified signature workflows:

**Endpoints:**
- `POST /api/pki/eidas/sign` - Create qualified signature
- `POST /api/pki/eidas/verify` - Verify qualified signature
- `POST /api/pki/hsm/sign` - Direct HSM signing
- `GET /api/pki/hsm/keys` - List HSM keys
- `POST /api/pki/timestamp` - Get timestamp token
- `POST /api/pki/timestamp/verify` - Verify timestamp
- `GET /api/pki/certificates` - List certificates
- `GET /api/pki/certificates/:id` - Get certificate details
- `GET /api/pki/status` - Health check

## API Usage

### 1. Create eIDAS Qualified Signature

**Request:**
```bash
curl -X POST http://localhost:8080/api/pki/eidas/sign \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $TOKEN" \
  -d '{
    "data_b64": "SGVsbG8sIFdvcmxkIQ=="
  }'
```

**Response:**
```json
{
  "qualified_signature": {
    "signature_b64": "MEUCIQDx...",
    "algorithm": "ECDSA-SHA256",
    "key_id": "hsm-key-001",
    "cert_serial": "1A2B3C4D5E",
    "timestamp_token_b64": "MIIDfQYJKoZI...",
    "timestamp_utc": "2025-06-15T10:30:45Z",
    "format": "eIDAS-QES",
    "version": "1.0"
  },
  "timestamped": true
}
```

### 2. Verify eIDAS Qualified Signature

**Request:**
```bash
curl -X POST http://localhost:8080/api/pki/eidas/verify \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $TOKEN" \
  -d '{
    "data_b64": "SGVsbG8sIFdvcmxkIQ==",
    "qualified_signature": {
      "signature_b64": "MEUCIQDx...",
      "algorithm": "ECDSA-SHA256",
      "timestamp_token_b64": "MIIDfQYJKoZI...",
      "format": "eIDAS-QES"
    }
  }'
```

**Response:**
```json
{
  "valid": true,
  "signature_valid": true,
  "timestamp_valid": true,
  "format": "eIDAS-QES",
  "algorithm": "ECDSA-SHA256",
  "timestamp_utc": "2025-06-15T10:30:45Z"
}
```

### 3. Direct HSM Signing (Lower Level)

**Request:**
```bash
curl -X POST http://localhost:8080/api/pki/hsm/sign \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $TOKEN" \
  -d '{
    "data_b64": "SGVsbG8sIFdvcmxkIQ=="
  }'
```

**Response:**
```json
{
  "signature_b64": "MEUCIQDx...",
  "algorithm": "ECDSA-SHA256",
  "key_id": "hsm-key-001",
  "cert_serial": "1A2B3C4D5E"
}
```

### 4. Get Cryptographic Timestamp

**Request:**
```bash
curl -X POST http://localhost:8080/api/pki/timestamp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $TOKEN" \
  -d '{
    "data_b64": "SGVsbG8sIFdvcmxkIQ=="
  }'
```

**Response:**
```json
{
  "timestamp_token_b64": "MIIDfQYJKoZI...",
  "timestamp_utc": "2025-06-15T10:30:45Z",
  "serial_number": "1234567890"
}
```

### 5. List HSM Keys

**Request:**
```bash
curl -X GET http://localhost:8080/api/pki/hsm/keys \
  -H "Authorization: Bearer $TOKEN"
```

**Response:**
```json
{
  "keys": [
    {
      "id": "hsm-key-001",
      "label": "production-signing-key",
      "type": "EC",
      "curve": "secp256r1",
      "certificate_serial": "1A2B3C4D5E"
    }
  ]
}
```

### 6. Check PKI System Status

**Request:**
```bash
curl -X GET http://localhost:8080/api/pki/status \
  -H "Authorization: Bearer $TOKEN"
```

**Response:**
```json
{
  "signing_service": "available",
  "hsm": "available",
  "tsa": "available",
  "hsm_keys_count": 3,
  "hsm_status": "connected",
  "tsa_status": "configured",
  "overall": "healthy"
}
```

## Configuration

### Environment Variables

```bash
# HSM Configuration
export THEMIS_HSM_LIBRARY="/usr/lib/softhsm/libsofthsm2.so"
export THEMIS_HSM_SLOT="0"
export THEMIS_HSM_PIN="1234"
export THEMIS_HSM_KEY_LABEL="production-key"
export THEMIS_HSM_ALGORITHM="ECDSA-SHA256"

# TSA Configuration
export THEMIS_TSA_URL="https://freetsa.org/tsr"
export THEMIS_TSA_HASH_ALGORITHM="SHA256"
export THEMIS_TSA_CERT_REQ="true"
export THEMIS_TSA_TIMEOUT_SECONDS="30"
```

### C++ API Configuration

```cpp
#include "server/pki_api_handler.h"
#include "security/hsm_provider.h"
#include "security/timestamp_authority.h"
#include "security/signing.h"

// Configure HSM
security::HSMConfig hsm_config;
hsm_config.library_path = "/usr/lib/softhsm/libsofthsm2.so";
hsm_config.slot_id = 0;
hsm_config.pin = "1234";
hsm_config.key_label = "production-key";
hsm_config.signature_algorithm = "ECDSA-SHA256";

auto hsm_provider = std::make_shared<security::HSMProvider>(hsm_config);
hsm_provider->initialize();

// Configure TSA
security::TSAConfig tsa_config;
tsa_config.url = "https://freetsa.org/tsr";
tsa_config.hash_algorithm = "SHA256";
tsa_config.cert_req = true;
tsa_config.timeout_seconds = 30;

auto tsa = std::make_shared<security::TimestampAuthority>(tsa_config);

// Create signing service
auto signing_service = std::make_shared<SigningService>();

// Create PKI API Handler with all components
auto pki_handler = std::make_shared<server::PkiApiHandler>(
    signing_service,
    hsm_provider,
    tsa
);
```

## eIDAS Compliance Requirements

### For Qualified Electronic Signatures (QES)

To meet eIDAS requirements, you need:

#### 1. Qualified Trust Service Provider (QTSP)
- Obtain qualified certificates from an eIDAS-accredited QTSP
- Examples: D-Trust, SwissSign, DigiCert (EU operations)
- Certificate must be marked as "qualified" in X.509 extensions

#### 2. Secure Signature Creation Device (SSCD)
- Use hardware HSM meeting Common Criteria EAL 4+ or FIPS 140-2 Level 3
- Examples: Thales Luna, Utimaco SecurityServer, AWS CloudHSM
- Private keys must be generated and stored in HSM (never exported)

#### 3. Qualified Timestamp
- Use timestamp authority recognized under eIDAS
- Timestamp must use qualified certificates
- Required for long-term signature validity (Art. 32 eIDAS)

#### 4. Certificate Chain Validation
- Implement full X.509 chain validation
- Check CRL/OCSP revocation status
- Validate against EU Trusted List (EUTL)

#### 5. Long-Term Validation (LTV)
- Archive signature data including:
  - Original document
  - Qualified signature
  - Certificate chain
  - Timestamp tokens
  - Revocation information (CRL/OCSP responses)
- Maintain archives for legal retention periods (typically 10-30 years)

### eIDAS Signature Formats

ThemisDB currently implements **CAdES-like** qualified signatures:

**Components:**
```json
{
  "signature_b64": "...",           // Digital signature (PKCS#1 or ECDSA)
  "algorithm": "ECDSA-SHA256",       // Signature algorithm
  "key_id": "hsm-key-001",           // HSM key identifier
  "cert_serial": "1A2B3C4D5E",       // Certificate serial number
  "timestamp_token_b64": "...",      // RFC 3161 timestamp token
  "timestamp_utc": "2025-06-15...",  // Human-readable timestamp
  "format": "eIDAS-QES",             // Format identifier
  "version": "1.0"                   // Schema version
}
```

**Future Enhancements:**
- Full CAdES-BES, CAdES-T, CAdES-X, CAdES-A support
- XAdES (XML Advanced Electronic Signatures)
- PAdES (PDF Advanced Electronic Signatures)
- Integration with EU Trusted Lists

## Production Deployment

### 1. Hardware HSM Setup

**Thales Luna Example:**
```bash
# Initialize Luna HSM
lunacm
> slot set -slot 0
> partition init -label "production"
> partition changePw -oldpw default -newpw <strong-password>

# Generate qualified key pair
cmu generatekeypair -modulusBits=2048 \
    -keyType=RSA \
    -sign=1 \
    -verify=1 \
    -label="production-signing-key"

# Configure ThemisDB
export THEMIS_HSM_LIBRARY="/usr/safenet/lunaclient/lib/libCryptoki2_64.so"
export THEMIS_HSM_SLOT="0"
export THEMIS_HSM_PIN="<strong-password>"
export THEMIS_HSM_KEY_LABEL="production-signing-key"
```

### 2. Enterprise TSA Configuration

**DigiCert TSA Example:**
```bash
export THEMIS_TSA_URL="https://timestamp.digicert.com"
export THEMIS_TSA_HASH_ALGORITHM="SHA256"
export THEMIS_TSA_CERT_REQ="true"
export THEMIS_TSA_TIMEOUT_SECONDS="30"
```

### 3. Certificate Management

**Obtain Qualified Certificate from QTSP:**
```bash
# Generate CSR (Certificate Signing Request) in HSM
pkcs11-tool --module /usr/lib/libCryptoki2_64.so \
    --slot 0 \
    --login \
    --pin <hsm-pin> \
    --keypairgen \
    --key-type RSA:2048 \
    --label production-key \
    --id 01

# Export public key for CSR
pkcs11-tool --module /usr/lib/libCryptoki2_64.so \
    --slot 0 \
    --read-object \
    --type pubkey \
    --id 01 \
    -o public.der

# Create CSR with OpenSSL
openssl req -new -engine pkcs11 \
    -keyform engine \
    -key slot_0-id_01 \
    -out request.csr \
    -subj "/C=DE/O=YourOrg/CN=production.example.com"

# Submit CSR to QTSP and import signed certificate
```

### 4. Security Hardening

**HSM Security:**
- Use strong PINs/passphrases (min 16 characters)
- Enable multi-factor authentication (MFA) for HSM access
- Implement M-of-N key ceremony for critical keys
- Regular HSM firmware updates
- Physical security controls for HSM hardware

**Network Security:**
- TLS 1.3 for all API communications
- Mutual TLS (mTLS) for production deployments
- HSM network segmentation (separate VLAN)
- Firewall rules restricting HSM access

**Operational Security:**
- Implement audit logging for all signing operations
- Monitor HSM health and capacity
- Backup HSM keys using secure key wrapping
- Test disaster recovery procedures
- Regular security audits

## Performance Considerations

### Typical Latencies

- **HSM Signing:** 10-50ms (depends on HSM model)
- **TSA Timestamp:** 100-500ms (network latency)
- **Total eIDAS Sign:** 150-600ms
- **Verification:** 50-200ms

### Optimization Strategies

**1. HSM Connection Pooling:**
```cpp
// Reuse HSM sessions
hsm_provider->initialize();  // Once at startup
// Multiple sign operations reuse the same session
```

**2. Timestamp Batching:**
```cpp
// For high-volume scenarios, batch timestamp requests
std::vector<std::vector<uint8_t>> signatures;
// ... collect signatures ...
auto batch_timestamp = tsa->batchGetTimestamps(signatures);
```

**3. Async Processing:**
```cpp
// Offload timestamp requests to background thread
std::future<TimestampToken> ts_future = std::async(
    std::launch::async,
    [&tsa, signature]() { return tsa->getTimestamp(signature); }
);
```

## Troubleshooting

### HSM Issues

**Problem:** `HSM_CKR_PIN_INCORRECT`
```bash
# Check PIN
softhsm2-util --show-slots

# Reset PIN (SoftHSM2)
softhsm2-util --init-token \
    --slot 0 \
    --label "test-token" \
    --so-pin 1234 \
    --pin 5678
```

**Problem:** `HSM_CKR_SESSION_HANDLE_INVALID`
```cpp
// Reinitialize HSM connection
hsm_provider->initialize();
```

### TSA Issues

**Problem:** Timestamp request timeout
```bash
# Test TSA connectivity
curl -I https://freetsa.org/tsr
# Increase timeout
export THEMIS_TSA_TIMEOUT_SECONDS="60"
```

**Problem:** Invalid timestamp token
```bash
# Verify TSA certificate chain
openssl ts -verify \
    -in timestamp.tsr \
    -data data.bin \
    -CAfile tsa-ca-chain.pem
```

## Testing

### Unit Tests

```bash
# Build tests
cd /workspaces/ThemisDB/build
cmake --build . --target test_hsm_provider
cmake --build . --target test_timestamp_authority
cmake --build . --target test_pki_api_handler

# Run tests
./test_hsm_provider
./test_timestamp_authority
./test_pki_api_handler
```

### Integration Tests

```bash
# Test eIDAS signature workflow
curl -X POST http://localhost:8080/api/pki/eidas/sign \
  -H "Content-Type: application/json" \
  -d '{"data_b64": "VGVzdCBEYXRh"}' \
  | jq . > signature.json

curl -X POST http://localhost:8080/api/pki/eidas/verify \
  -H "Content-Type: application/json" \
  -d "{\"data_b64\": \"VGVzdCBEYXRh\", \"qualified_signature\": $(cat signature.json | jq .qualified_signature)}" \
  | jq .
```

## Legal Considerations

**Disclaimer:** This documentation provides technical implementation guidance. Legal compliance requires consultation with qualified legal counsel and accredited trust service providers.

**Key Legal Requirements:**
- Obtain qualified certificates from eIDAS-accredited QTSPs
- Use certified SSCD (hardware HSM)
- Implement certificate chain validation
- Archive signatures with long-term validation data
- Comply with GDPR for signature metadata
- Meet industry-specific regulations (e.g., eIDAS Art. 25 for healthcare)

**Liability:** The signature creator is responsible for:
- Protecting HSM credentials
- Verifying signer identity
- Ensuring consent to sign
- Proper key lifecycle management

## References

### Standards

- **eIDAS Regulation:** EU 910/2014
- **ETSI EN 319 102-1:** Electronic Signatures and Infrastructures (ESI); Procedures for Creation and Validation of AdES Digital Signatures
- **ETSI EN 319 122-1:** CAdES (CMS Advanced Electronic Signatures)
- **RFC 3161:** Time-Stamp Protocol (TSP)
- **RFC 5652:** Cryptographic Message Syntax (CMS)
- **FIPS 140-2:** Security Requirements for Cryptographic Modules

### Resources

- **EU Trusted Lists:** https://eidas.ec.europa.eu/efda/tl-browser/
- **FreeTSA:** https://freetsa.org/
- **OASIS DSS:** https://www.oasis-open.org/committees/dss/
- **ThemisDB HSM Integration:** [hsm_integration.md](hsm_integration.md)

### QTSPs (Qualified Trust Service Providers)

- **D-Trust (Germany):** https://www.d-trust.net/
- **SwissSign (Switzerland):** https://www.swisssign.com/
- **Agencia de Tecnología y Certificación Electrónica (Spain):** https://www.sede.fnmt.gob.es/
- **Actalis (Italy):** https://www.actalis.com/

## Support

For technical questions or implementation support:
- GitHub Issues: https://github.com/yourusername/ThemisDB/issues
- Email: support@themisdb.example.com
- Documentation: https://docs.themisdb.example.com

---

**ThemisDB PKI/eIDAS Implementation**  
Version 1.0 - June 2025
