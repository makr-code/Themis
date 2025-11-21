# Certificate Pinning - HSM/TSA TLS Hardening

Erweiterte TLS-Sicherheit für ausgehende Verbindungen zu HSM (Hardware Security Module) und TSA (Timestamp Authority) durch Certificate Pinning.

## Übersicht

Certificate Pinning bietet zusätzliche Sicherheit gegenüber Standard-TLS:
- **Trust-on-First-Use (TOFU)**: Vertraue nur bekannten Zertifikaten
- **Man-in-the-Middle Protection**: Verhindert MITM-Angriffe trotz kompromittierter CAs
- **SHA256 Fingerprint Verification**: Verifiziere exakte Zertifikats-Identität
- **Leaf oder Chain Pinning**: Flexibles Pinning von Leaf-Cert oder Intermediate-CA

## Funktionsweise

### Standard TLS (ohne Pinning)

1. Client verbindet zu Server
2. Server sendet Zertifikat
3. Client prüft Zertifikat gegen System-CA-Store
4. ✅ Verbindung etabliert (wenn CA-signiert)

**Problem**: Kompromittierte CA kann gefälschte Zertifikate ausstellen.

### TLS mit Certificate Pinning

1. Client verbindet zu Server
2. Server sendet Zertifikat
3. Client prüft Zertifikat gegen System-CA-Store
4. **Client berechnet SHA256-Fingerprint des Zertifikats**
5. **Client vergleicht Fingerprint mit Whitelist**
6. ✅ Verbindung nur bei Match etabliert

**Vorteil**: Selbst kompromittierte CAs können Server nicht impersonieren.

## Konfiguration

### Environment Variables

```bash
# Certificate Pinning aktivieren
export THEMIS_PKI_ENABLE_CERT_PINNING=true

# Pinned Fingerprints (SHA256, hex, komma-separiert)
export THEMIS_PKI_PINNED_CERTS="a1b2c3d4...,e5f6g7h8..."

# Nur Leaf-Zertifikat pinnen (Standard: false = gesamte Chain)
export THEMIS_PKI_PIN_LEAF_ONLY=false
```

### Programmatische Konfiguration

```cpp
#include "utils/pki_client.h"

using namespace themis::utils;

PKIConfig config;
config.endpoint = "https://hsm.example.com:8443/api/v1";
config.cert_path = "/etc/themis/client.crt";
config.key_path = "/etc/themis/client.key";

// Certificate Pinning aktivieren
config.enable_cert_pinning = true;

// SHA256-Fingerprints der erlaubten Zertifikate
config.pinned_cert_fingerprints = {
    "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2",
    "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"
};

// Optional: Nur Leaf-Cert pinnen (nicht Intermediate/Root)
config.pin_leaf_only = false;

VCCPKIClient client(config);
```

## Fingerprint-Generierung

### OpenSSL Command-Line

```bash
# SHA256-Fingerprint eines Zertifikats
openssl x509 -in server.crt -noout -fingerprint -sha256

# Output:
# SHA256 Fingerprint=A1:B2:C3:D4:E5:F6:...:01:02

# Hex ohne Doppelpunkte (für Konfiguration)
openssl x509 -in server.crt -noout -fingerprint -sha256 | \
  sed 's/.*=//;s/://g' | tr '[:upper:]' '[:lower:]'

# Output:
# a1b2c3d4e5f6...0102
```

### Fingerprint vom laufenden Server abrufen

```bash
# Verbinde und zeige Zertifikat
echo | openssl s_client -connect hsm.example.com:8443 2>/dev/null | \
  openssl x509 -noout -fingerprint -sha256

# Oder mit curl
curl --insecure -v https://hsm.example.com:8443 2>&1 | \
  grep -A 20 "Server certificate"
```

### Programmatisch (C++)

```cpp
#include <openssl/x509.h>
#include <openssl/sha.h>

std::string compute_cert_fingerprint(X509* cert) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    unsigned int n = 0;
    
    if (!X509_digest(cert, EVP_sha256(), md, &n)) {
        return "";
    }
    
    std::ostringstream oss;
    for (unsigned int i = 0; i < n; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') 
            << static_cast<int>(md[i]);
    }
    
    return oss.str();
}
```

## Verwendungsszenarien

### HSM-Verbindung (Hardware Security Module)

```cpp
PKIConfig hsm_config;
hsm_config.endpoint = "https://hsm.internal.corp:8443";
hsm_config.enable_cert_pinning = true;
hsm_config.pinned_cert_fingerprints = {
    // HSM-Server-Zertifikat SHA256
    "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"
};

VCCPKIClient hsm_client(hsm_config);

// Signing-Operation
auto result = hsm_client.signHash(hash_bytes);
// -> Verbindung nur bei korrektem Fingerprint
```

### TSA-Verbindung (Timestamp Authority)

```cpp
PKIConfig tsa_config;
tsa_config.endpoint = "https://tsa.example.com:443/rfc3161";
tsa_config.enable_cert_pinning = true;
tsa_config.pinned_cert_fingerprints = {
    // TSA-Server-Zertifikat SHA256
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    // Backup-TSA (für Redundanz)
    "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdef"
};

// Timestamp-Request
// (TSA-Client würde hier ähnlich zu PKI-Client funktionieren)
```

### Multiple Environments (Dev/Prod)

```cpp
PKIConfig config;
config.endpoint = std::getenv("PKI_ENDPOINT");
config.enable_cert_pinning = true;

// Verschiedene Fingerprints für Dev/Prod
if (is_production) {
    config.pinned_cert_fingerprints = {
        "prod_cert_fingerprint_sha256..."
    };
} else {
    config.pinned_cert_fingerprints = {
        "dev_cert_fingerprint_sha256..."
    };
}
```

## Leaf vs. Chain Pinning

### Leaf Pinning (`pin_leaf_only = true`)

**Vorteile**:
- Spezifischste Kontrolle
- Einfache Rotation (nur Leaf-Cert austauschen)

**Nachteile**:
- Muss bei jedem Cert-Renewal aktualisiert werden

**Verwendung**:
```cpp
config.pin_leaf_only = true;
config.pinned_cert_fingerprints = {
    "leaf_cert_sha256..."  // Nur Server-Zertifikat
};
```

### Chain Pinning (`pin_leaf_only = false`)

**Vorteile**:
- Flexibler bei Cert-Renewal
- Pin Intermediate-CA statt Leaf

**Nachteile**:
- Weniger spezifisch

**Verwendung**:
```cpp
config.pin_leaf_only = false;
config.pinned_cert_fingerprints = {
    "intermediate_ca_sha256...",  // Intermediate CA
    "root_ca_sha256..."            // Optional: Root CA
};
```

## Security Best Practices

### 1. Mindestens 2 Fingerprints pinnen

Für Redundanz bei Zertifikatsrotation:

```cpp
config.pinned_cert_fingerprints = {
    "current_cert_sha256...",
    "next_cert_sha256..."  // Für nahtlose Rotation
};
```

### 2. Regelmäßige Rotation

Plane Zertifikatsrotation im Voraus:

```bash
# 30 Tage vor Ablauf: Neues Zertifikat generieren
openssl x509 -in current.crt -noout -enddate

# Fingerprint des neuen Zertifikats zur Whitelist hinzufügen
# BEVOR altes Zertifikat abläuft
```

### 3. Out-of-Band Verification

Verifiziere Fingerprints über sicheren Kanal:

```bash
# Fingerprint per SSH/VPN abrufen
ssh admin@hsm.internal.corp "openssl x509 -in /etc/ssl/server.crt -fingerprint -sha256"

# Fingerprint in Konfiguration eintragen
```

### 4. Monitoring für Pin-Failures

```cpp
try {
    auto result = pki_client.signHash(hash);
} catch (const std::exception& e) {
    // Log Certificate Pinning Fehler
    audit_logger.logSecurityEvent(
        SecurityEventType::INTEGRITY_VIOLATION,
        "system",
        "pki_client",
        {{"error", e.what()}, {"endpoint", config.endpoint}}
    );
    
    // Alert Operations Team
    alert_ops("Certificate pinning failure detected!");
}
```

### 5. Backup-Kommunikationskanal

Für den Fall, dass alle Pins ungültig werden:

```cpp
// Fallback: Manual verification mode
if (config.pinned_cert_fingerprints.empty()) {
    THEMIS_WARN("Certificate pinning disabled - manual verification required");
    // Erzwinge manuelle Bestätigung durch Operator
}
```

## Troubleshooting

### Verbindung schlägt fehl: "Certificate pinning failed"

**Problem**: Fingerprint stimmt nicht überein.

**Diagnose**:
```bash
# Aktueller Server-Fingerprint
echo | openssl s_client -connect hsm.example.com:8443 2>/dev/null | \
  openssl x509 -noout -fingerprint -sha256

# Vergleich mit gepinntem Fingerprint
grep THEMIS_PKI_PINNED_CERTS /etc/themis/config
```

**Lösung**:
1. Fingerprint korrekt? → Update Konfiguration
2. Server-Zertifikat geändert? → Neuen Fingerprint hinzufügen
3. MITM-Angriff? → Security Incident Response

### CURL-Error: "SSL certificate problem"

**Problem**: Standard SSL-Verifikation schlägt fehl.

**Lösung**:
```bash
# CA-Bundle aktualisieren
sudo update-ca-certificates

# Oder: Spezifisches CA-Cert hinzufügen
curl --cacert /path/to/ca.crt https://hsm.example.com:8443
```

### Fingerprint-Mismatch bei Rotation

**Problem**: Zertifikat wurde rotiert, Fingerprint nicht aktualisiert.

**Lösung**:
```cpp
// Temporär: Beide Fingerprints erlauben
config.pinned_cert_fingerprints = {
    "old_cert_sha256...",
    "new_cert_sha256..."
};

// Nach erfolgreicher Rotation: Alten Fingerprint entfernen
```

## Performance

### Overhead

- **Fingerprint-Berechnung**: ~0.1ms (SHA256)
- **Whitelist-Check**: O(n) mit n = Anzahl gepinnter Zertifikate
- **Typischer Overhead**: < 1ms für 5 gepinnte Certs

**Empfehlung**: Max. 5-10 gepinnte Fingerprints.

### Caching

CURL cached SSL-Sessions automatisch:

```cpp
// SSL Session Cache (automatisch in CURL)
// Nachfolgende Requests nutzen bestehende Session
// -> Kein erneuter Pinning-Check nötig
```

## Compliance

### FIPS 140-2/3

Certificate Pinning ist kompatibel mit FIPS-Modus:

```bash
# FIPS-Mode aktivieren (OpenSSL)
export OPENSSL_FIPS=1

# SHA256 ist FIPS-approved
```

### PCI DSS

- Erfüllt PCI DSS 3.2.1 Requirement 4.1 (Strong Cryptography)
- Additional Layer of Defense

### SOC 2

- Trust Services Criteria: CC6.1 (Logical and Physical Access Controls)
- Defense in Depth gegen CA-Kompromittierung

## Alternativen & Vergleich

| Methode | Sicherheit | Flexibilität | Wartungsaufwand |
|---------|-----------|--------------|-----------------|
| **Certificate Pinning** | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐ (regelmäßige Updates) |
| **mTLS (Client Certs)** | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ (Client-Cert-Management) |
| **Standard TLS** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ (automatische CA-Updates) |
| **IP Whitelisting** | ⭐⭐ | ⭐⭐ | ⭐⭐⭐ (statische IPs erforderlich) |

**Empfehlung**: Kombiniere Certificate Pinning + mTLS für maximale Sicherheit.

## Migration

### Von Standard TLS zu Pinning

1. **Fingerprints sammeln**:
   ```bash
   openssl s_client -connect hsm.example.com:8443 < /dev/null 2>/dev/null | \
     openssl x509 -fingerprint -sha256 -noout
   ```

2. **Test-Environment**:
   ```cpp
   // Aktiviere Pinning in Dev/Test
   config.enable_cert_pinning = true;
   config.pinned_cert_fingerprints = {"dev_fingerprint..."};
   ```

3. **Monitoring**:
   ```cpp
   // Log Pinning-Erfolge/Fehler
   THEMIS_INFO("Certificate pinning: {}", 
       pin_valid ? "PASSED" : "FAILED");
   ```

4. **Rollout Produktion**:
   ```bash
   # Konfiguration aktualisieren
   export THEMIS_PKI_ENABLE_CERT_PINNING=true
   export THEMIS_PKI_PINNED_CERTS="prod_fingerprint..."
   
   # Service neu starten
   sudo systemctl restart themis
   ```

## Weitere Informationen

- [PKI Client Implementation](../src/utils/pki_client.cpp)
- [TLS Setup](TLS_SETUP.md)
- [HSM Integration](HSM_INTEGRATION.md)
- [RFC 7469 - Public Key Pinning](https://tools.ietf.org/html/rfc7469)
