# PII Detection Engine Signing

## Overview

All PII detection engines must be signed with PKI signatures before they can be loaded by the PIIDetector orchestrator. This ensures:

- **Integrity**: Engine configurations cannot be tampered with
- **Authenticity**: Only trusted entities can create detection engines
- **Auditability**: All signature verifications are logged
- **Non-repudiation**: Signer identity is cryptographically verified

## Security Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  PII Detection Engine Loading Flow (with PKI Verification)  │
└─────────────────────────────────────────────────────────────┘

1. Load YAML Config
   ↓
2. Extract Engine Config + Signature
   ↓
3. Compute Config Hash (SHA-256)
   ├─ Normalize to JSON (deterministic)
   ├─ Exclude signature block
   └─ Hash with SHA-256
   ↓
4. Verify PKI Signature
   ├─ Decode base64 signature
   ├─ Verify with public key
   └─ Compare with computed hash
   ↓
5. [PASS] Initialize Engine  OR  [FAIL] Reject + Log
```

## Quick Start

### 1. Generate Test Keys (Development Only)

```bash
cd tools
python sign_pii_engine.py keygen --output-dir ../config/keys

# Output:
# [✓] Generated test key pair:
#     Private key: ../config/keys/private_key.pem
#     Public key: ../config/keys/public_key.pem
# [!] WARNING: These are test keys. Use HSM for production!
```

### 2. Sign an Engine Configuration

```bash
python sign_pii_engine.py sign \
    --config ../config/pii_patterns.yaml \
    --engine regex \
    --key ../config/keys/private_key.pem \
    --output ../config/pii_patterns_signed.yaml \
    --signer "VCC Security Team"

# Output:
# [*] Found regex engine configuration
# [*] Configuration hash (SHA-256): e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
# [*] Generated signature: BASE64_ENCODED_SIGNATURE...
# [✓] Signed configuration written to ../config/pii_patterns_signed.yaml
# [✓] Signature ID: pii-regex-engine-v1.0.0
# [✓] Signed by: VCC Security Team
# [✓] Signed at: 2025-11-01T12:34:56+00:00
```

### 3. Use Signed Configuration in C++

```cpp
#include "utils/pii_detector.h"
#include "utils/pki_client.h"

// Initialize PKI client with public key
PKIConfig pki_config;
pki_config.public_key_path = "config/keys/public_key.pem";
auto pki_client = std::make_shared<VCCPKIClient>(pki_config);

// Create PII detector with PKI verification
PIIDetector detector("config/pii_patterns_signed.yaml", pki_client);

// All engines are now verified with PKI signatures!
auto findings = detector.detectInText("Contact alice@example.com");
```

## Production Deployment

### Hardware Security Module (HSM)

For production environments, private keys should be stored in an HSM:

```cpp
// Production PKI client with HSM backing
PKIConfig pki_config;
pki_config.use_hsm = true;
pki_config.hsm_slot = 0;
pki_config.hsm_pin = std::getenv("HSM_PIN");
pki_config.public_key_id = "VCC-PKI-001";

auto pki_client = std::make_shared<VCCPKIClient>(pki_config);
PIIDetector detector("config/pii_patterns.yaml", pki_client);
```

### CI/CD Integration

Add signing to your deployment pipeline:

```yaml
# .github/workflows/deploy.yml
- name: Sign PII Engine Configurations
  run: |
    python tools/sign_pii_engine.py sign \
      --config config/pii_patterns.yaml \
      --engine regex \
      --key ${{ secrets.PII_SIGNING_KEY }} \
      --output config/pii_patterns_signed.yaml \
      --signer "VCC CI/CD Pipeline"
    
- name: Verify Signatures
  run: |
    python tools/verify_pii_signatures.py \
      --config config/pii_patterns_signed.yaml \
      --public-key config/keys/public_key.pem
```

## Signature Format

### In YAML Configuration

```yaml
detection_engines:
  - type: "regex"
    version: "1.0.0"
    enabled: true
    
    # PKI Signature Block
    signature:
      config_hash: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
      signature: "BASE64_ENCODED_SIGNATURE"
      signature_id: "pii-regex-engine-v1.0.0"
      cert_serial: "VCC-PKI-001"
      signed_at: "2025-11-01T12:34:56Z"
      signer: "VCC Security Team"
    
    # Engine configuration (included in hash)
    settings:
      min_confidence: 0.75
      # ...
    patterns:
      - name: EMAIL
        # ...
```

### Signature Verification in C++

```cpp
// In PIIDetectionEngineFactory::createSigned()

// 1. Extract signature from config
auto sig_node = config["signature"];
PluginSignature signature;
signature.config_hash = sig_node["config_hash"];
signature.signature = sig_node["signature"];
signature.signature_id = sig_node["signature_id"];
// ...

// 2. Compute hash of config (excluding signature)
auto config_copy = config;
config_copy.erase("signature");
std::string computed_hash = PluginSignature::computeConfigHash(config_copy);

// 3. Verify signature
if (computed_hash != signature.config_hash) {
    error_msg = "Config hash mismatch";
    return nullptr;
}

if (!pki_client.verifyHash(signature.config_hash, signature.signature)) {
    error_msg = "PKI signature verification failed";
    return nullptr;
}

// 4. Create engine (signature is valid)
auto engine = createUnsigned(engine_type);
engine->initialize(config);
```

## Security Considerations

### Threat Model

| Threat | Mitigation |
|--------|-----------|
| Tampered engine config | Config hash mismatch detected, engine rejected |
| Unsigned engine | No signature block, engine rejected (unless fallback enabled) |
| Expired signature | Signature age check in global_settings.max_signature_age_days |
| Untrusted signer | Signer whitelist in global_settings.pki_verification.trusted_signers |
| Key compromise | Rotate keys, re-sign all engines, deploy new public key |
| Replay attack | Signature includes timestamp, old signatures rejected |

### Key Rotation Procedure

1. Generate new key pair (keep old keys for transition period)
2. Re-sign all engine configurations with new key
3. Deploy signed configs + new public key to all instances
4. Monitor logs for verification failures
5. After transition period, revoke old keys

### Audit Logging

All signature verification attempts are logged:

```
[INFO] PIIDetector: Verifying signature for engine 'regex' v1.0.0
[INFO] PKI signature verification succeeded for 'pii-regex-engine-v1.0.0'
[INFO] Engine 'regex' loaded successfully (signed by: VCC Security Team)
```

Failures trigger alerts:

```
[ERROR] PKI signature verification FAILED for engine 'regex'
[ERROR] Config hash mismatch: expected e3b0c44..., computed a1b2c3...
[ERROR] Falling back to embedded unsigned defaults
```

## Troubleshooting

### "Config hash mismatch" Error

**Cause:** Engine configuration was modified after signing.

**Solution:** Re-sign the configuration:
```bash
python tools/sign_pii_engine.py sign --config pii_patterns.yaml --engine regex --key private_key.pem --output pii_patterns_signed.yaml
```

### "PKI signature verification failed" Error

**Cause:** Signature doesn't match public key (wrong key, corrupted signature, etc.)

**Solution:** 
1. Verify public key matches private key used for signing
2. Check signature wasn't corrupted (base64 encoding issues)
3. Re-sign with correct key

### "Untrusted signer" Error

**Cause:** Signer not in `trusted_signers` list.

**Solution:** Add signer to `global_settings.pki_verification.trusted_signers` in YAML:
```yaml
global_settings:
  pki_verification:
    trusted_signers:
      - "VCC Security Team"
      - "Your New Signer"  # Add here
```

### Fallback to Unsigned Defaults

If all signed engines fail to load, the detector falls back to embedded unsigned regex patterns:

```
[WARN] All signed engines failed PKI verification
[WARN] Falling back to embedded unsigned RegexDetectionEngine
[INFO] Using 7 embedded regex patterns (EMAIL, PHONE, SSN, ...)
```

To disable fallback (enforce signature requirement):
```yaml
global_settings:
  pki_verification:
    allow_embedded_fallback: false
```

## Advanced Usage

### Multiple Signers (Chain of Trust)

For critical environments, require multiple signatures:

```yaml
signature:
  signatures:  # Array of signatures
    - signer: "VCC Security Team"
      signature: "..."
      signed_at: "2025-11-01T12:00:00Z"
    
    - signer: "VCC Compliance Officer"
      signature: "..."
      signed_at: "2025-11-01T12:30:00Z"
```

### Signature Revocation

Maintain a revocation list:

```yaml
global_settings:
  pki_verification:
    revoked_signatures:
      - "pii-regex-engine-v0.9.0"  # Old version, revoked
      - "pii-ner-compromised-2024"  # Compromised key
```

### Emergency Unsigned Mode

For disaster recovery, temporarily disable PKI verification:

```cpp
// Emergency bypass (log extensively!)
PIIDetector detector("config/pii_patterns.yaml", nullptr);  // nullptr = no PKI client
spdlog::critical("EMERGENCY: Running without PKI verification!");
```

## References

- PKI Client Implementation: `include/utils/pki_client.h`
- Plugin Interface: `include/utils/pii_detection_engine.h`
- Orchestrator: `include/utils/pii_detector.h`
- YAML Configuration: `config/pii_patterns.yaml`
