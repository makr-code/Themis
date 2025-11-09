# PKI-Signaturen (OpenSSL)

Diese Seite beschreibt die Signatur- und Verifikationsfunktionen in ThemisDB mittels OpenSSL.

## Überblick

- Komponente: `utils::VCCPKIClient`
- Aufgabe: Signieren und Verifizieren von bereits gehashten Daten (z. B. SHA-256 über Payloads)
- Algorithmen: `RSA-SHA256` (Standard), optional `RSA-SHA384`, `RSA-SHA512`
- Betriebsarten:
  - "Real" (mit Key/Cert): RSA-Signatur mit privatem Schlüssel, Verifikation mit X.509-Zertifikat
  - "Stub" (Fallback): Kein Key/Cert oder Hashlänge passt nicht → Signatur ist `base64(hash)`; Verifikation vergleicht entsprechend

> WARNUNG (MVP): Es erfolgt aktuell KEINE Zertifikatsketten-, Ablauf-, KeyUsage- oder Revocation-Prüfung. Der im Real-Modus geladene Public Key wird direkt für `RSA_verify` benutzt. Für eIDAS-/Rechtskonformität sind zusätzliche Prüfungen notwendig (siehe Hardening Abschnitt).

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
- Für `RSA-SHA256` muss der Eingabe-Hash 32 Byte lang sein (SHA-256). Analog: 48 Byte (SHA-384), 64 Byte (SHA-512).
- Stimmen Algorithmus und Hash-Länge nicht überein oder sind Key/Cert nicht gesetzt, wird automatisch der Stub-Modus genutzt (Base64-Echo, nicht kryptographisch sicher).
- Im Stub-Modus ist Integrität rein kosmetisch; Audit-Logs sind manipulierbar.

## Tests

- `tests/test_pki_client.cpp`
  - `SignVerify_StubMode_Base64Echo` → Fallback ohne Key/Cert
  - `SignVerify_RSA_SHA256_Succeeds` → Laufzeitgenerierter RSA-Key + Self-Signed-Cert; vollständiger Sign-Verify-Test
  - `SignVerify_AlgoMismatch_FallsBackStub` → Falsche Hashlänge → Fallback

**Status (MVP):** Alle PKI-Client-Tests laufen erfolgreich (`PKIClientTest.*` - 3 Tests PASSED). Tests decken Real-Signatur (lokaler Key) und Stub-Fallback ab. Compliance-Prüfungen (Chain/Revocation) fehlen vollständig.

### Tests ausführen

```powershell
.\build\Release\themis_tests.exe --gtest_filter=PKIClientTest.*
```

## Sicherheitsempfehlungen

- Produktionsbetrieb ausschließlich mit echten, geschützten Schlüsseln (HSM/PKCS#11 optional)
- Zertifikatskette validieren (aktuell nur Einzel-Zertifikat geladen — Hardening erforderlich)
- Revocation prüfen (CRL oder OCSP) vor Annahme einer Signatur
- Schlüssel und Zertifikate über Secret-Management bereitstellen; nicht im Repo ablegen
- Canonicalisierung der zu signierenden Payload (stabile JSON-Reihenfolge) hinzufügen, um Replay/Manipulation zu verhindern
- Audit-Metriken erweitern: `pki_signature_mode{mode="stub|real"}`, `pki_verify_failure_total{reason}`

### Bekannte Limitierungen (Stand 09.11.2025)
| Bereich | Limitation | Risiko |
|--------|------------|--------|
| Chain Validation | Keine CA/Intermediate Prüfung | Akzeptiert gefälschtes Self-Signed Zertifikat |
| Revocation | Keine CRL/OCSP | Widerrufene Zertifikate weiter gültig |
| KeyUsage/EKU | Nicht geprüft | Falscher Zertifikatstyp nutzbar |
| Canonicalisierung | Fehlt | Signatur kann bei alternativer Serialisierung brechen |
| Error Reporting | `SignatureResult.ok` meist true | Silent Failures möglich |
| Mode Kennzeichnung | Kein Feld | Unterscheidung Audit schwierig |
| Timestamping | Kein Signatur-Zeitstempel | Forensische Beweisführung erschwert |
| Hash Enforcement | Caller muss korrekte Länge liefern | Falsche Hash-Algorithmen unbemerkt |

### Hardening Roadmap
1. Signatur-Kontext erweitern: `mode`, `error_message`, `ts_signed` (ms)
2. CA Chain Load & `X509_verify_cert` + Ablauf-/KU/EKU-Prüfung
3. Optional CRL/OCSP Checks (konfigurierbar)
4. Canonical JSON (UTF-8 Normalisierung, Schlüsselsortierung)
5. Revocation Negative Cache + Telemetrie (Histogramm für Latenz)
6. Ed25519/ECDSA Unterstützung (Performance & moderne Kryptographie)

## Roadmap

- Anbindung an externen PKI-Service (REST/mTLS)
- Unterstützung für Hardware-Keys/HSM
- Audit-Logging von Signatur-Operationen (inkl. Modus und Error Codes)
- Zertifikatsketten-Validierung & Revocation (CRL/OCSP)
- Canonicalisierung & Signatur über normalisierte Payload
- Erweiterte Algorithmen (Ed25519/ECDSA) für schnelleres Signing
