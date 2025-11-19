# Docker Build - Status und nächste Schritte

## Problem

vcpkg-basierte Docker-Builds sind derzeit instabil:
- Download-Fehler (boost-utility, patchelf)
- Build-Fehler (xsimd, boost-headers)
- Build-Zeit: >30 Minuten für alle Dependencies
- Netzwerk-abhängig und fehleranfällig

## Durchgeführte Ansätze

### 1. vcpkg Manifest Mode ❌
- Problem: Baseline `beea85fdd8bf6b...` existiert nicht mehr in vcpkg
- Lösung: Baseline entfernt, dynamisch generiert
- Resultat: Download-Fehler bei Boost-Paketen

### 2. vcpkg Stable Release (2024.12.16) ❌
- Problem: boost-headers build failure
- Fehler: Ninja configuration error
- Resultat: Build schlägt fehl

### 3. Reduzierte Dependencies (vcpkg.docker.json) ⚠️
- opentelemetry-cpp entfernt
- Arrow Features reduziert (json, filesystem statt parquet, compute)
- gtest, benchmark entfernt
- Resultat: Noch in Tests

## Alternative Lösungen

### Option A: GitHub Actions mit Pre-Built Binaries ⭐ EMPFOHLEN
1. GitHub Actions Workflow erstellen
2. vcpkg mit Binary Caching in CI
3. Artifact Upload der kompilierten Binaries
4. Dockerfile lädt Pre-Built Binaries herunter
5. Nur themis_server wird im Dockerfile kompiliert

**Vorteile:**
- Schneller Build (~5 Min statt 30+ Min)
- Stabil, reproduzierbar
- Binary Cache wiederverwendbar

**Umsetzung:**
```yaml
# .github/workflows/vcpkg-build.yml
name: vcpkg Dependencies
on:
  push:
    paths: ['vcpkg.json', 'vcpkg-configuration.json']
  workflow_dispatch:

jobs:
  build-linux-deps:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Bootstrap vcpkg
        run: |
          git clone https://github.com/microsoft/vcpkg.git
          ./vcpkg/bootstrap-vcpkg.sh
      - name: Build dependencies
        run: |
          ./vcpkg/vcpkg install --triplet=x64-linux
          tar czf vcpkg-installed.tar.gz vcpkg_installed/
      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: vcpkg-deps-ubuntu22
          path: vcpkg-installed.tar.gz
```

### Option B: Multi-Stage Build mit System Packages + Ausgewählte vcpkg
1. System Packages wo möglich (librocksdb-dev, libtbb-dev, etc.)
2. Nur kritische Pakete via vcpkg (simdjson, spdlog)
3. Arrow optional oder minimal

**Vorteile:**
- Schneller als voller vcpkg-Build
- Weniger Download-Fehler

**Nachteile:**
- Versionskonflikte möglich
- Ubuntu 22.04 Pakete teilweise veraltet

### Option C: Verwendung von Conan statt vcpkg
Conan hat besseres Binary Caching und stabilere Builds.

**Vorteile:**
- Robustes Binary Caching
- Pre-Built Binaries für viele Pakete

**Nachteile:**
- Große Änderung an Build-System
- Lernkurve

### Option D: Base Image mit Dependencies
Eigenes Base-Image erstellen mit allen Dependencies, davon ableiten.

**Vorteile:**
- Einmalige Build-Zeit
- Schnelle themis-Builds danach

**Nachteile:**
- Separates Image pflegen
- Größer Initial-Setup

## Aktueller Stand

- ✅ Dockerfile-Struktur erstellt (Multi-Stage)
- ✅ vcpkg.docker.json mit reduzierten Dependencies
- ✅ CMakeLists.txt Anpassungen für optionale Pakete (teilweise)
- ❌ Stabiler vcpkg-Build im Docker noch nicht erreicht
- ⚠️ Lokaler Build auf Windows funktioniert weiterhin

## Nächste Schritte (EMPFEHLUNG)

1. **GitHub Actions Workflow implementieren** (Option A)
   - vcpkg Binary Cache in CI
   - Artifact Upload nach ghcr.io oder GitHub Packages
   
2. **Dockerfile anpassen**
   - Pre-Built Binaries von GitHub Actions laden
   - Nur themis_server kompilieren
   
3. **Testen und Pushen**
   - Image bauen: `docker build -t themisdb/themis:v0.1.0-alpha .`
   - Image pushen: `docker push themisdb/themis:v0.1.0-alpha`
   - Tags: qnap-latest, latest

4. **QNAP Deployment Test**
   - docker-compose.qnap.yml verwenden
   - Funktionstest auf QNAP

## Geschätzte Zeit

- Option A (GitHub Actions): 4-6 Stunden Setup, dann <10 Min Builds
- Option B (System Packages): 2-3 Stunden, 10-15 Min Builds
- Option C (Conan): 8-10 Stunden Migration
- Option D (Base Image): 6-8 Stunden Setup

## Fallback: Direkt auf QNAP kompilieren

Falls Docker-Build zu komplex:
1. Dependencies auf QNAP installieren
2. Direkt auf QNAP kompilieren
3. Systemd service einrichten
4. Später Docker-Image erstellen

## Kontakt

Bei Fragen: GitHub Issues oder Discussions in Repository
