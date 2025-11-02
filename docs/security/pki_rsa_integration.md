# PKI RSA-Integration (Plan)

Stand: 2. November 2025

Ziel: Ersetzung des Demo-Stubs in `src/utils/pki_client.cpp` durch echte eIDAS-konforme Signaturen (RSA-SHA256) inkl. Verifikation.

## Anforderungen
- RSA Sign/Verify mit OpenSSL (ohne Remote-PKI zunächst)
- Lade Private Key (PEM, optional passphrase) aus Dateipfad oder Secret-Provider
- Verwende X.509 Zertifikat (PEM) für Metadaten (cert_serial) und Verifikation
- Streaming-fähig: Eingabe ist bereits Hash (SHA-256) der Nutzdaten
- Thread-sicher (Client instanziiert pro Worker oder geschützt über Mutex)

## API (bleibt stabil)
- VCCPKIClient::signHash(hash_bytes) → SignatureResult
- VCCPKIClient::verifyHash(hash_bytes, sig) → bool

## Konfiguration (ENV, MVP)
- THEMIS_PKI_PRIVATE_KEY: Pfad zur PEM-Datei mit privatem Schlüssel
- THEMIS_PKI_PRIVATE_KEY_PASSPHRASE: Passphrase (falls gesetzt)
- THEMIS_PKI_CERTIFICATE: Pfad zur X.509 PEM-Zertifikatsdatei

Später (optional):
- Remote-PKI-Endpoint (REST/HSM), OAuth2/mTLS, KeyID statt Datei

## Implementierungs-Skizze

```cpp
// signHash()
// 1) Lade EVP_PKEY aus PEM (+Passphrase)
// 2) Erzeuge EVP_PKEY_CTX + RSA-PSS (oder PKCS#1 v1.5, je nach Vorgabe)
// 3) Setze Hash-Algorithmus (SHA-256)
// 4) Signiere hash_bytes (EVP_DigestSign* für v1.5; PSS erfordert directe Signatur über Hash via EVP_PKEY_sign)
// 5) Base64-Encode der Signatur
// 6) Lese Zertifikat, extrahiere Serial

// verifyHash()
// 1) Lade Zertifikat (oder Public Key)
// 2) Base64-Decode Signatur
// 3) Verifikation gegen hash_bytes
```

## Sicherheitsaspekte
- Key-Material nicht im Speicher persistieren (Zeroization wo möglich)
- Kein Logging von Passphrasen/Keys
- Integrierbar mit HSM (PKCS#11) in Folgeschritt

## DoD
- Unit-Tests: Sign→Verify True; Mutierte Daten → Verify False
- Interop-Test: OpenSSL CLI verifiziert erzeugte Signaturen
- Doku-Update: COMPLIANCE.md eIDAS-Status auf ✅

## Nächste Schritte
1) Implementierung in `pki_client.cpp` (hinter Feature-Flag THEMIS_PKI_OPENSSL=1)
2) ENV-Handling in `main_server.cpp` hinzufügen
3) Tests + Doku aktualisieren
