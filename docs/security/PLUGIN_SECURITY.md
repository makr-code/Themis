# Plugin Security - ThemisDB

**Version:** 1.0  
**Status:** Production  
**Last Updated:** 20. November 2025  
**Sicherheitsstufe:** KRITISCH

---

## Executive Summary

Hardware-Beschleunigungsplugins werden als **optionale DLLs/Shared Libraries** zur Laufzeit geladen. Diese Plugins haben Zugriff auf:
- GPU/Hardware-Ressourcen
- Systemspeicher (VRAM/RAM)
- Datenbank-interne Daten (Vektoren, Graphen, Geo-Daten)
- Potenziell sensitive Informationen

**Sicherheitsrisiken:**
- ⚠️ Malicious Code Injection
- ⚠️ Man-in-the-Middle Attacks (DLL Hijacking)
- ⚠️ Supply Chain Attacks
- ⚠️ Data Exfiltration
- ⚠️ Privilege Escalation

**Lösung:** Multi-Layer Security mit digitalen Signaturen, Hash-Verifikation, Zertifikats-Validierung und Audit-Logging.

---

## Sicherheitsarchitektur

### Defense in Depth

```
┌─────────────────────────────────────────────┐
│  Layer 1: File Hash Verification (SHA-256) │
├─────────────────────────────────────────────┤
│  Layer 2: Digital Signature (RSA/ECDSA)    │
├─────────────────────────────────────────────┤
│  Layer 3: Certificate Chain Validation     │
├─────────────────────────────────────────────┤
│  Layer 4: Certificate Revocation (CRL/OCSP)│
├─────────────────────────────────────────────┤
│  Layer 5: Whitelist/Blacklist Check        │
├─────────────────────────────────────────────┤
│  Layer 6: Trust Level Enforcement          │
├─────────────────────────────────────────────┤
│  Layer 7: Audit Logging & Monitoring       │
└─────────────────────────────────────────────┘
```

### Komponenten

1. **PluginSecurityVerifier** - Verifikation vor dem Laden
2. **PluginSecurityPolicy** - Konfigurierbare Sicherheitsrichtlinien
3. **PluginSecurityAuditor** - Audit-Trail aller Security-Events
4. **PluginMetadata** - JSON-Sidecar mit Signatur-Informationen

---

## Security Policy

### Production Policy (Default)

```cpp
PluginSecurityPolicy productionPolicy;
productionPolicy.requireSignature = true;        // MANDATORY
productionPolicy.allowUnsigned = false;          // BLOCKED
productionPolicy.verifyFileHash = true;          // MANDATORY
productionPolicy.checkRevocation = true;         // MANDATORY
productionPolicy.minTrustLevel = TRUSTED;        // Only trusted issuers

productionPolicy.trustedIssuers = {
    "CN=ThemisDB Official Plugins, O=ThemisDB, C=DE"
};
```

### Development Policy

```cpp
PluginSecurityPolicy devPolicy;
devPolicy.requireSignature = false;    // Optional for local dev
devPolicy.allowUnsigned = true;        // Allow unsigned plugins
devPolicy.verifyFileHash = true;       // Still verify hash
devPolicy.checkRevocation = false;     // Skip revocation check
```

### Konfiguration (YAML)

```yaml
# config/plugin_security.yaml
security:
  require_signature: true
  allow_unsigned: false
  verify_file_hash: true
  check_revocation: true
  min_trust_level: TRUSTED
  
  trusted_issuers:
    - "CN=ThemisDB Official Plugins, O=ThemisDB, C=DE"
    - "CN=NVIDIA CUDA Plugins, O=NVIDIA, C=US"
    
  blacklist_hashes:
    - "a1b2c3d4..."  # Known malicious plugin
    
  whitelist_hashes:
    - "e5f6g7h8..."  # Explicitly allowed (bypasses other checks)
```

---

## Plugin Metadata Format

### JSON Sidecar File

Jedes Plugin benötigt eine `.json` Metadata-Datei:

```
themis_accel_cuda.dll
themis_accel_cuda.dll.json  ← Metadata + Signature
```

### Metadata Schema

```json
{
  "plugin": {
    "name": "CUDA Acceleration Plugin",
    "version": "1.0.0",
    "author": "ThemisDB Team",
    "description": "NVIDIA CUDA GPU acceleration",
    "license": "MIT",
    "build_date": "2025-11-20T19:00:00Z",
    "build_commit": "4bad6fd",
    
    "signature": {
      "sha256": "a1b2c3d4e5f6g7h8...",
      "signature": "BASE64_ENCODED_SIGNATURE",
      "certificate": "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----",
      "issuer": "CN=ThemisDB Official Plugins, O=ThemisDB, C=DE",
      "subject": "CN=CUDA Plugin v1.0.0",
      "timestamp": 1700507200,
      "algorithm": "RSA-SHA256"
    },
    
    "permissions": [
      "gpu_access",
      "memory_access"
    ],
    
    "requirements": {
      "cuda_version": "11.0+",
      "min_compute_capability": "7.0"
    }
  }
}
```

---

## Plugin Signierung

### Signatur-Prozess

```bash
# 1. Plugin erstellen
cmake --build build --target themis_accel_cuda

# 2. SHA-256 Hash berechnen
sha256sum themis_accel_cuda.dll > themis_accel_cuda.dll.hash

# 3. Signatur erstellen
openssl dgst -sha256 -sign private_key.pem \
  -out themis_accel_cuda.dll.sig \
  themis_accel_cuda.dll

# 4. Signatur Base64 kodieren
base64 themis_accel_cuda.dll.sig > themis_accel_cuda.dll.sig.b64

# 5. Metadata JSON erstellen
# (siehe Schema oben)
```

### Automatisches Signatur-Tool

```python
#!/usr/bin/env python3
# tools/sign_plugin.py

import hashlib
import subprocess
import json
import base64
from datetime import datetime

def sign_plugin(plugin_path, private_key_path, cert_path):
    # Calculate SHA-256
    with open(plugin_path, 'rb') as f:
        sha256_hash = hashlib.sha256(f.read()).hexdigest()
    
    # Sign with OpenSSL
    sig_file = plugin_path + '.sig'
    subprocess.run([
        'openssl', 'dgst', '-sha256',
        '-sign', private_key_path,
        '-out', sig_file,
        plugin_path
    ])
    
    # Read signature
    with open(sig_file, 'rb') as f:
        signature = base64.b64encode(f.read()).decode()
    
    # Read certificate
    with open(cert_path) as f:
        certificate = f.read()
    
    # Extract issuer/subject from certificate
    # ... (use OpenSSL commands)
    
    # Create metadata JSON
    metadata = {
        "plugin": {
            "signature": {
                "sha256": sha256_hash,
                "signature": signature,
                "certificate": certificate,
                "timestamp": int(datetime.now().timestamp()),
                "algorithm": "RSA-SHA256"
            }
        }
    }
    
    # Write metadata
    with open(plugin_path + '.json', 'w') as f:
        json.dump(metadata, f, indent=2)
    
    print(f"Plugin signed: {plugin_path}")
    print(f"Hash: {sha256_hash}")

if __name__ == '__main__':
    import sys
    sign_plugin(sys.argv[1], sys.argv[2], sys.argv[3])
```

### Verwendung

```bash
# Plugin signieren
python tools/sign_plugin.py \
  plugins/themis_accel_cuda.dll \
  certs/themis_plugin_key.pem \
  certs/themis_plugin_cert.pem
```

---

## Zertifikatsverwaltung

### Erstellen eines Signing-Zertifikats

```bash
# 1. Private Key erstellen
openssl genrsa -out themis_plugin_key.pem 4096

# 2. Certificate Signing Request (CSR)
openssl req -new -key themis_plugin_key.pem \
  -out themis_plugin.csr \
  -subj "/CN=ThemisDB Official Plugins/O=ThemisDB/C=DE"

# 3. Self-signed Zertifikat (für Entwicklung)
openssl x509 -req -days 365 \
  -in themis_plugin.csr \
  -signkey themis_plugin_key.pem \
  -out themis_plugin_cert.pem

# 4. Production: Von CA signieren lassen
# Sende themis_plugin.csr an vertrauenswürdige CA
```

### Zertifikatskette

**Empfohlene Struktur:**

```
Root CA (ThemisDB Root)
  └── Intermediate CA (ThemisDB Plugin Authority)
      └── Signing Certificate (Plugin Signer)
```

### Zertifikats-Speicherorte

| Platform | Pfad |
|----------|------|
| Windows | `C:/Program Files/ThemisDB/certs/` |
| Linux | `/etc/themis/certs/` |
| macOS | `/Library/Application Support/ThemisDB/certs/` |

---

## Certificate Revocation

### Certificate Revocation List (CRL)

```yaml
# config/plugin_security.yaml
security:
  crl:
    enabled: true
    url: "https://themisdb.org/certs/plugin_crl.pem"
    cache_ttl: 3600  # 1 hour
    
  ocsp:
    enabled: true
    responder_url: "http://ocsp.themisdb.org"
    timeout: 5  # seconds
```

### CRL Format

```
-----BEGIN X509 CRL-----
MIIBsDCCAVYCAQEwDQYJKoZIhvcNAQELBQAwgZExCzAJBgNVBAYTAkRFMRAwDgYD
VQQIDAdCYXZhcmlhMQ8wDQYDVQQHDAZNdW5pY2gxEjAQBgNVBAoMCVRoZW1pc0RC
...
-----END X509 CRL-----
```

---

## Whitelist/Blacklist Management

### Whitelist (Explizit erlaubte Plugins)

```json
{
  "whitelist": [
    {
      "hash": "a1b2c3d4e5f6...",
      "name": "CUDA Plugin v1.0.0",
      "added_by": "admin@themisdb.org",
      "added_date": "2025-11-20",
      "reason": "Official ThemisDB release"
    }
  ]
}
```

### Blacklist (Gesperrte Plugins)

```json
{
  "blacklist": [
    {
      "hash": "x9y8z7w6v5u4...",
      "name": "Malicious Plugin",
      "blocked_by": "security@themisdb.org",
      "blocked_date": "2025-11-15",
      "reason": "CVE-2025-12345: Remote Code Execution",
      "severity": "CRITICAL"
    }
  ]
}
```

### Update-Mechanismus

```bash
# Update Blacklist von zentralem Server
curl -o /etc/themis/plugin_blacklist.json \
  https://security.themisdb.org/plugin_blacklist.json

# Verify signature of blacklist itself
openssl dgst -sha256 -verify themisdb_public.pem \
  -signature plugin_blacklist.json.sig \
  plugin_blacklist.json
```

---

## Audit Logging

### Security Events

| Event Type | Severity | Beschreibung |
|------------|----------|--------------|
| PLUGIN_LOADED | INFO | Plugin erfolgreich geladen |
| PLUGIN_LOAD_FAILED | ERROR | Plugin-Laden fehlgeschlagen |
| SIGNATURE_VERIFIED | INFO | Signatur erfolgreich verifiziert |
| SIGNATURE_VERIFICATION_FAILED | ERROR | Signatur-Verifikation fehlgeschlagen |
| HASH_MISMATCH | CRITICAL | File-Hash stimmt nicht überein |
| BLACKLISTED | CRITICAL | Plugin ist auf Blacklist |
| UNTRUSTED_ISSUER | WARNING | Unbekannter Zertifikats-Aussteller |
| CERTIFICATE_EXPIRED | ERROR | Zertifikat abgelaufen |
| CERTIFICATE_REVOKED | CRITICAL | Zertifikat widerrufen |
| POLICY_VIOLATION | WARNING | Security-Policy verletzt |

### Audit-Log-Format

```json
{
  "timestamp": "2025-11-20T19:00:00Z",
  "event_type": "SIGNATURE_VERIFICATION_FAILED",
  "severity": "ERROR",
  "plugin_path": "./plugins/themis_accel_cuda.dll",
  "plugin_hash": "a1b2c3d4...",
  "message": "Certificate expired",
  "user": "system",
  "ip_address": "127.0.0.1"
}
```

### Export für SIEM

```bash
# Export Audit-Logs
curl http://localhost:8765/admin/plugin-security/audit-log > audit.json

# Integration mit Splunk/ELK
curl -X POST https://splunk.company.com/services/collector \
  -H "Authorization: Splunk TOKEN" \
  -d @audit.json
```

---

## Best Practices

### Für Plugin-Entwickler

1. **Immer signieren** - Auch interne Plugins sollten signiert werden
2. **Minimale Permissions** - Nur notwendige Berechtigungen anfordern
3. **Versionierung** - Semantic Versioning verwenden
4. **Dependencies dokumentieren** - Alle externen Abhängigkeiten auflisten
5. **Security Audit** - Regelmäßige Code-Reviews und Penetration-Tests

### Für Administratoren

1. **Production Policy erzwingen** - `requireSignature = true`
2. **Regelmäßige Updates** - Blacklist täglich aktualisieren
3. **Monitoring** - Audit-Logs überwachen (SIEM-Integration)
4. **Certificate Rotation** - Zertifikate alle 12 Monate erneuern
5. **Incident Response** - Plan für kompromittierte Plugins

### Für Endnutzer

1. **Nur offizielle Plugins** - Von vertrauenswürdigen Quellen
2. **Hash verifizieren** - Mit offiziellen Release-Notes abgleichen
3. **Updates zeitnah** - Sicherheitsupdates sofort installieren
4. **Suspicious Activity melden** - An security@themisdb.org

---

## Incident Response

### Bei kompromittiertem Plugin

1. **Sofort Blacklist** - Hash auf Blacklist setzen
2. **CRL aktualisieren** - Zertifikat widerrufen
3. **Notification** - Alle Nutzer informieren
4. **Forensics** - Incident analysieren
5. **Patch Release** - Sicheres Update bereitstellen

### Emergency Blacklist

```bash
# Emergency: Plugin sofort global sperren
curl -X POST https://security.themisdb.org/api/emergency-block \
  -H "Authorization: Bearer ADMIN_TOKEN" \
  -d '{
    "plugin_hash": "a1b2c3d4...",
    "reason": "CVE-2025-12345",
    "severity": "CRITICAL"
  }'
```

---

## Compliance

### DSGVO/GDPR

- ✅ Audit-Trail aller Plugin-Ladevorgänge
- ✅ Zugriffskontrolle auf Plugins
- ✅ Datenminimierung (nur notwendige Permissions)

### ISO 27001

- ✅ Access Control (A.9)
- ✅ Cryptography (A.10)
- ✅ System Acquisition (A.14.2)

### NIST Cybersecurity Framework

- ✅ Identify: Asset Management (ID.AM)
- ✅ Protect: Data Security (PR.DS)
- ✅ Detect: Security Monitoring (DE.CM)
- ✅ Respond: Incident Response (RS.RP)

---

## Weiterführende Dokumentation

- [Plugin Development Guide](../development/plugin_development.md)
- [Certificate Management](../security/certificate_management.md)
- [Incident Response Plan](../security/incident_response.md)
- [Security Audit Checklist](../security/audit_checklist.md)

---

**Security Contact:** security@themisdb.org  
**Version:** 1.0  
**Last Review:** 20. November 2025  
**Next Review:** 20. Februar 2026
