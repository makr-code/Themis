# Themis – Sicherheits-Audit Checkliste

Diese Checkliste unterstützt ein wiederholbares Sicherheits-Audit für Themis-Server und Admin-Tools.

## 1) Architektur & Threat Modeling
- Datenflüsse und Vertrauensgrenzen dokumentiert (Client ↔ Server ↔ Storage)
- Angriffsflächen identifiziert (HTTP-API, Admin-Tools, Datei-Importe)
- Missbrauchsfälle (Abuse Cases) erfasst (API-Scraping, DoS, unautorisierte Exporte)

## 2) Abhängigkeiten & Vulnerability-Scan
- .NET: `dotnet list package --vulnerable` in allen Tools
- C++/vcpkg: Versionen und CVEs prüfen (vcpkg-Baseline aktuell, Release Notes)
- Container-Images (falls genutzt): Trivy/Grype Scan
- Repo-Scan: `security-scan.ps1` ausführen (C/C++ Risky-APIs, Secret-Pattern, .NET Vulnerabilities)

## 3) Build/Compiler-Härtung
- C++: Warnings auf Maximum, Sanitizer im CI-Testlauf (ASAN/UBSAN) aktivierbar
- Windows: ASLR/DEP standardmäßig aktiv, Code Signing für EXEs/Installer
- Release-Builds reproduzierbar (vcpkg Baseline, deterministische Flags)

## 4) Authentifizierung & Autorisierung
- Admin-APIs nur für authentisierte Nutzer (Token/Bearer, mTLS oder Reverse Proxy Auth)
- Rollen/Rechte getrennt (View vs. Export vs. Löschfunktionen)
- Sensitive Aktionen (PII-Delete, Key-Rotation) auditierbar

## 5) Transport-Sicherheit
- TLS erzwingen (Reverse Proxy wie Nginx/Traefik vor Themis-Server)
- Sichere Cipher Suites, HSTS, TLS 1.2+
- Interne Admin-Tools: Kommunikation nur über HTTPS

## 6) Input-Validierung & Serialisierung
- Strikte Schema-Validierung für JSON-Inputs
- Limitierung von Eingabegrößen (Body Size, Felder)
- Schutz gegen Path Traversal, SSRF, Open Redirects (URL/Path-Validierung)

## 7) Ratenbegrenzung & Ressourcen-Schutz
- Rate Limiting / Backoff bei teuren Endpoints (Export, Query)
- Timeouts, maximale Parallelität, Queue-Limits
- DoS-Schutz auf Proxy-Ebene

## 8) Logging & Audit
- Security-relevante Events protokolliert (Login, Export, Löschungen, Rotation)
- Log-Integrität (WORM/zentral, manipulationssicher)
- PII-Logging minimieren, keine sensiblen Daten im Klartext

## 9) Secrets & Konfiguration
- Keine Secrets im Repo (API Keys, Zertifikate) – Geheimnis-Scan
- Konfiguration via Umgebung/geschützte Stores (Windows DPAPI/KeyVault)
- Rotationspläne und Notfall-Rollback definiert

## 10) Privacy & Compliance
- Data Minimization, Zweckbindung, Löschkonzepte (Art. 17)
- Export-/Reporting-Pfade DSGVO-konform (Berechtigungen, Pseudonymisierung)
- Auftragsverarbeitung und TOMs dokumentiert

## 11) Testen & Review
- Security Code Review (C++/C#) gegen diese Checkliste
- Fuzzing-Kampagnen (Parser, Query, Import)
- Penetration-Test gegen die bereitgestellte Staging-Umgebung

## 12) Release-Gates
- Build/Lint/Tests PASS
- Vulnerability-Scan: keine kritischen offenen CVEs
- Signierte Artefakte (Code Signing), Hashes veröffentlicht
 - Geheimnis-Scan ohne Treffer (oder Findings adressiert): `security-scan.ps1`
