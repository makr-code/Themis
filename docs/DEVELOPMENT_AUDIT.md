<!-- Development audit generated: 2025-11-16 -->
# Development Audit — ThemisDB (Kurzbericht)

Version: wsl-stabilize (Arbeitsbranch)
Datum: 2025-11-16

Kurzfassung
-	Ziel: Vollständiger, handlungsorientierter Entwicklungs‑ und Sicherheits‑Audit zur Angleichung von Dokumentation, Build/Run‑Vorgehen und Sicherheits‑Hartening.
-	Scope: Build/Run (CMake), Tests (GoogleTest + WSL), Vault‑Integration, Docker runtime (Dockerfile.runtime), Abhängigkeiten und CI‑Empfehlungen.

Methodik
-	Quellcode‑Review der Start‑ und Server‑Entrypoints (`src/main.cpp`, `src/main_server.cpp`, `src/server/http_server.cpp`).
-	Analyse der Vault‑Integration (`src/security/vault_key_provider.cpp`) und der zugehörigen Tests (`tests/test_vault_key_provider*.cpp`).
-	Einsicht in `CMakeLists.txt`, `Dockerfile.runtime`, `.tools/vault_dev_run.ps1`, `README.md`, `CONTRIBUTING.md` und generierte Docs.
-	Ergebnis: ein priorisiertes Maßnahmen‑ und Dokumentations‑Set mit Aufwandsschätzungen.

Wesentliche Befunde
1) Build & Developer Workflow
 - CMake‑Ziel: `themis_server` (API) und `themis_tests` (GoogleTest). Build‑outputs werden in der Praxis unter `build-wsl/` geschrieben (WSL‑Konvention). `CMakeLists.txt` kopiert `config/` ins Build‑Verzeichnis — wichtig für lokale Runs/tests.
 - Empfohlen: Dokumentieren, dass lokale Tests im Build‑Verzeichnis laufen und welche Dateien erforderlich sind (`config/*.yaml|json`).

2) Tests & CI
 - Tests erzeugen JUnit XML in Windows/WSL (helper: `C:\Temp`). Vault‑abhängige Tests verlassen sich auf `VAULT_ADDR`/`VAULT_TOKEN` und KV v2 am Pfad `themis`.
 - Empfohlen: CI‑Matrix (Linux + Windows), Upload von JUnit XML, und ein Mock‑Mode für Vault‑abhängige Tests in PR‑Checks.

3) Vault Integration
 - Implementation erwartet 32‑Byte Rohschlüssel, Base64 in KV v2 unter `themis/keys/<id>`. Tests prüfen Key‑Länge, Rotation und Transit‑Signaturen (Retry/Backoff vorhanden).
 - Empfehlungen: Production: `verify_ssl = true`, AppRole/short‑lived tokens, und klare Anleitung zum Erstellen des 32‑Byte Keys.

4) Docker / Runtime
 - `Dockerfile.runtime` nutzt `ubuntu:22.04`, kopiert `build-wsl/themis_server`, installiert minimale Laufzeitpakete und erstellt non‑root user `themis`. Ports im Image: `8080` und `18765`; Server default port ist `8765` — mögliche Verwirrung bei Mapping.
 - Empfehlung: Multi‑stage Build → minimales Runtime image (distroless/slim), entferne nicht nötige Tools (curl/jq) aus Laufzeit, dokumentiere Port‑Mapping klar.

5) Abhängigkeiten & Sicherheit
 - Frühere lokale Scans zeigten MEDIUM‑Findings (z. B. `libpam`, `tar`) die aus Basis‑Image/OS‑Paketen stammen. `apt-get upgrade` im Dockerfile hilft nur wenn Repo patches verfügbar sind.
 - Empfehlung: automatische nächtliche Rebuilds, Trivy‑Scan in CI (fail on HIGH/CRITICAL), ggf. Wechsel zu kleinerem Base‑Image oder distroless.

Priorisierte Maßnahmen (Kurz, Medium, Langfristig)

P0 (Kurz, 0.5–1 Tag)
 - Dokumentation: Ergänze README/CONTRIBUTING mit Build/WSL‑Konventionen (`build-wsl`), Test XML Pfad (`C:\Temp`) und Port defaults (`8765`) — erledigt (erste Patches angewendet).
 - Runtime: Ensure container runs as non‑root (bereits im Dockerfile), dokumentiere `--security-opt=no-new-privileges` und `--cap-drop=ALL` als Deploy‑Best‑Practice.

P1 (Medium, 1–3 Tage)
 - Dockerfile → Multi‑stage minimal runtime: erstelle Patch, ersetze Ubuntu mit `debian:slim` oder distroless, nur benötigte libs kopieren.
 - CI: GitHub Actions workflow (matrix: ubuntu/windows), build+test, JUnit upload, Trivy image scan for `:release` images; fail on HIGH/CRITICAL.
 - Tests: Add Vault mock mode or test tag to skip integration tests in PRs by default; provide a dedicated integration pipeline that runs Vault‑tests.

P2 (Langfristig, 3–8 Tage)
 - Consider static linking or fully distroless image if license/compatibility allow — reduces shared library CVE surface.
 - Implement automated nightly base image rebuilds and notify on new HIGH/CRITICAL findings.

Konkrete Vorschläge und Snippets

- Minimal CI (Übersicht): build, test, trivy

  1) Build + Test matrix (ubuntu/windows) → upload JUnit
  2) Build Docker image (linux runner) → run Trivy (JSON output) → fail on HIGH/CRITICAL

- Runtime flags (Dokumentations‑Snip):

```powershell
docker run --rm -u themis --security-opt=no-new-privileges --cap-drop=ALL \
  -v C:\data\themis:/data:rw themis:release
```

- Trivy quick commands (Dokumentation / CI):

```bash
trivy image --format json --output trivy_results.json myrepo/themis:release
trivy image --severity CRITICAL,HIGH myrepo/themis:release
```

Dokumentation: Quick Wins
-	Beschreibe Port‑Mapping explizit in `README.md` (server default `8765` vs. image `8080`/`18765`).
-	Ergänze `docs/VAULT.md` um explizite `vault kv put` Beispiele (openssl rand -base64 32) — bereits angelegt.
-	Führe ein `CONTRIBUTING.md` Abschnitt „Running tests under WSL“ mit konkreten WSL‑Befehlen und Pfaden — bereits ergänzt.

Nächste Schritte (Empfohlen)
1) Sofort: Add CI job skeleton (GitHub Actions) that runs tests on Windows + Linux and uploads JUnit artifacts. (Aufwand: ~1 Tag)
2) Kurz: Implement multi‑stage `Dockerfile.runtime` patch and document runtime flags. (Aufwand: ~1–2 Tage)
3) Mittel: Add Trivy scanning to CI and nightly rebuild pipeline. (Aufwand: ~1–2 Tage)
4) Lang: Consider reducing image base to distroless and exploring static linking. (Aufwand: >3 Tage)

Anhänge / Artefakte
- `docs/VAULT.md` (erzeugt) — Vault helper & steps
- `CONTRIBUTING.md` (erweitert) — WSL/Test Hinweise
- `README.md` (kleine Ergänzung) — Developer quickstart

Schlussbemerkung
- Der Kern des Projekts (CMake, Tests, Vault‑Integration) ist konsistent und gut strukturiert. Die wichtigsten Investitionen rentieren sich in kurz‑ bis mittelfristiger Reduktion des Sicherheitsrisikos (CI‑Scans, minimaler Runtime‑Image) und reproduzierbaren Builds.

Wenn du möchtest, generiere ich jetzt: a) den Multi‑stage `Dockerfile.runtime` Patch (als Entwurf), oder b) das GitHub Actions CI‑Workflow‑Template für build+test+trivy. Sag kurz, welche Option ich als nächsten Schritt umsetzen soll.

*** Ende des Berichts
