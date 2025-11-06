# PKI-Signaturen (OpenSSL)

Diese Seite beschreibt die Signatur- und Verifikationsfunktionen in ThemisDB mittels OpenSSL.

## Überblick

- Komponente: `utils::VCCPKIClient`
- Aufgabe: Signieren und Verifizieren von bereits gehashten Daten (z. B. SHA-256 über Payloads)
- Algorithmen: `RSA-SHA256` (Standard), optional `RSA-SHA384`, `RSA-SHA512`
- Betriebsarten:
  - "Real" (mit Key/Cert): RSA-Signatur mit privatem Schlüssel, Verifikation mit X.509-Zertifikat
  - "Stub" (Fallback): Kein Key/Cert → Signatur ist `base64(hash)`; Verifikation vergleicht entsprechend

## Konfiguration (`PKIConfig`)

- `service_id`: frei wählbare ID
- `endpoint`: reserviert für spätere Remote-PKI-Integration
- `cert_path`: Pfad zu X.509-Zertifikat (PEM)
- `key_path`: Pfad zu privatem Schlüssel (PEM)
- `key_passphrase`: optionale Passphrase für den Key
- `signature_algorithm`: `RSA-SHA256` | `RSA-SHA384` | `RSA-SHA512`

Beispiel:

```json
{
  "cert_path": "config/pki/test_cert.pem",
  "key_path": "config/pki/test_key.pem",
  "signature_algorithm": "RSA-SHA256"
}
```

## Nutzung

```cpp
#include "utils/pki_client.h"
using namespace themis::utils;

PKIConfig cfg;
cfg.key_path = "config/pki/key.pem";
cfg.cert_path = "config/pki/cert.pem";
cfg.signature_algorithm = "RSA-SHA256";

VCCPKIClient client(cfg);
std::vector<uint8_t> sha256_hash = /* 32 bytes */;

auto sig = client.signHash(sha256_hash);
bool ok = client.verifyHash(sha256_hash, sig);
```

Hinweise:
- Für `RSA-SHA256` muss der Eingabe-Hash 32 Byte lang sein (SHA-256).
- Stimmen Algorithmus und Hash-Länge nicht überein oder sind Key/Cert nicht gesetzt, wird automatisch der Stub-Modus genutzt.

## Tests

- `tests/test_pki_client.cpp`
  - `SignVerify_StubMode_Base64Echo` → Fallback ohne Key/Cert
  - `SignVerify_RSA_SHA256_Succeeds` → Laufzeitgenerierter RSA-Key + Self-Signed-Cert; vollständiger Sign-Verify-Test
  - `SignVerify_AlgoMismatch_FallsBackStub` → Falsche Hashlänge → Fallback

**Status:** Alle PKI-Client-Tests laufen erfolgreich (`PKIClientTest.*` - 3 Tests PASSED).

### Tests ausführen

```powershell
.\build\Release\themis_tests.exe --gtest_filter=PKIClientTest.*
```

## Sicherheitsempfehlungen

- Produktionsbetrieb ausschließlich mit echten, geschützten Schlüsseln (HSM/PKCS#11 optional)
- Zertifikatskette validieren (aktuell wird nur das bereitgestellte Zertifikat verwendet)
- Schlüssel und Zertifikate über Secret-Management bereitstellen; nicht im Repo ablegen

## Roadmap

- Anbindung an externen PKI-Service (REST/mTLS)
- Unterstützung für Hardware-Keys/HSM
- Audit-Logging von Signatur-Operationen
