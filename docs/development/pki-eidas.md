# PKI / eIDAS-konforme Signaturen

Ziel: Implementierung einer eIDAS-kompatiblen Signatur-Engine für Dokumente und API-Antworten.

Aufwandsschätzung: 3–5 Tage

DoD (Definition of Done):
- Erzeugung und Verifikation eIDAS-konformer Signaturen (CMS/PKCS#7 oder CAdES-Lite)
- Integration mit KeyProvider / HSM / Vault (private key storage)
- Audit-Logging für Signatur-Operationen
- Beispiel-HTTP-API und Unit/Integration-Tests
- Kurz-Dokumentation mit Beispiel-Aufruf

Vorschlag Implementierungsschritte:
1. Analyse: Prüfe bestehende `utils/pki_client.cpp` und `include/security/*` für KeyProvider-Integrationen.
2. API-Design: `SigningService`-Interface (sign/verify) + HTTP-Handler `pki_api_handler`.
3. Implementierung: Beispiel-Backend `MockSigningProvider` + `VaultSigningProvider` (optional).
4. Tests: Unit-Tests für Signatur/Verifikation; Integrationstest mit `MockKeyProvider`.
5. Doku: `docs/development/pki-eidas.md` Update mit Usage und Compliance-Notes.

Benchmarks / Performance:
- Latency: Signatur-Erzeugung von kleinen Dokumenten sollte <50ms auf Standard-VM liegen (ohne HSM-Latenz).

Risiken:
- Externe HSM/KMS Integrationen können lange Latenzen haben. Implementiere asynchrone/queued Signing-Flows falls nötig.

