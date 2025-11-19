# ThemisDB - Build & Deployment Guide (v0.1.0_alpha)

Vollständige Anleitung für Build und Deployment von ThemisDB auf Windows, Linux/WSL und QNAP (Docker).

## 📋 Inhaltsverzeichnis

1. [Systemvoraussetzungen](#systemvoraussetzungen)
2. [Windows Build](#windows-build-powershell)
3. [Linux/WSL Build](#linuxwsl-build-bash)
4. [Docker Build & Deployment](#docker-build--deployment)
5. [QNAP Deployment](#qnap-deployment)
6. [Konfiguration](#konfiguration)
7. [Production Deployment](#production-deployment)
8. [Monitoring & Observability](#monitoring--observability)
9. [Backup & Recovery](#backup--recovery)
10. [Troubleshooting](#troubleshooting)

## Systemvoraussetzungen

### Minimum Requirements
- **CPU**: 4 cores (x86_64 oder ARM64)
- **RAM**: 2 GB (1 GB für RocksDB, 1 GB für System/Buffers)
- **Disk**: 20 GB SSD (NVMe empfohlen)
- **OS**: Windows 10/11, Linux (Ubuntu 20.04+), macOS 12+

### Recommended Production
- **CPU**: 8+ cores (Intel Xeon, AMD EPYC)
- **RAM**: 8 GB (4 GB block cache, 1 GB memtable, 3 GB system)
- **Disk**: 100 GB+ NVMe SSD (read: 3000 MB/s, write: 1500 MB/s)
- **OS**: Linux (kernel 5.10+) für Production Workloads
- **Network**: 10 Gbps (low-latency network)

### Build-Abhängigkeiten
- **CMake**: 3.20+
- **Compiler**: 
  - Windows: MSVC 2019+ (C++20)
  - Linux: GCC 11+ oder Clang 14+
- **vcpkg**: Package Manager (wird automatisch via setup-Scripts installiert)

## Windows Build (PowerShell)

### 1. Ersteinrichtung

```powershell
# Repository klonen
git clone https://github.com/makr-code/ThemisDB.git
cd ThemisDB

# vcpkg und Abhängigkeiten installieren
.\setup.ps1
# Installiert: vcpkg, RocksDB, OpenSSL, Boost, Arrow, spdlog, etc.
```

### 2. Build-Varianten

#### Debug Build (Visual Studio)
```powershell
# Standard: Visual Studio Generator, build-msvc/ Verzeichnis
.\build.ps1 -BuildType Debug

# Build-Artefakte:
# - build-msvc/Debug/themis_server.exe
# - build-msvc/Debug/themis_tests.exe
```

#### Release Build mit Tests
```powershell
.\build.ps1 -BuildType Release -RunTests

# Führt automatisch alle Tests aus nach dem Build
```

#### Ninja Build (schneller)
```powershell
# Ninja verwenden statt Visual Studio
.\build.ps1 -BuildType Release -Generator Ninja

# Build-Artefakte in build-ninja/
```

#### Build mit Benchmarks
```powershell
.\build.ps1 -BuildType Release -EnableBenchmarks

# Erstellt Benchmark-Binaries:
# - bench_crud.exe
# - bench_index_rebuild.exe
# - bench_vector_search.exe
```

#### Clean Build
```powershell
# Alle Build-Verzeichnisse löschen und neu bauen
.\build.ps1 -Clean -BuildType Release
```

### 3. Server starten (Windows)

```powershell
# Mit YAML-Config (empfohlen)
.\build-msvc\Release\themis_server.exe --config config.yaml

# Mit JSON-Config
.\build-msvc\Release\themis_server.exe --config config.json

# Mit CLI-Argumenten
.\build-msvc\Release\themis_server.exe --port 8765 --host 0.0.0.0
```

### 4. Tests ausführen

```powershell
# Alle Tests
.\build-msvc\Release\themis_tests.exe

# Spezifische Test-Suite
.\build-msvc\Release\themis_tests.exe --gtest_filter="TransactionTests.*"
```

## Linux/WSL Build (Bash)

### 1. Ersteinrichtung

```bash
# Repository klonen
git clone https://github.com/makr-code/ThemisDB.git
cd ThemisDB

# Abhängigkeiten installieren (vcpkg + System-Pakete)
./setup.sh
```

### 2. Build-Varianten

#### Debug Build
```bash
./build.sh BUILD_TYPE=Debug

# Build-Artefakte in build-wsl/ (WSL) oder build/ (Linux)
```

#### Release Build mit Tests
```bash
./build.sh BUILD_TYPE=Release RUN_TESTS=1
```

#### Custom Build-Verzeichnis
```bash
BUILD_DIR=my-build ./build.sh BUILD_TYPE=Release
```

#### Ninja Generator
```bash
GENERATOR=Ninja ./build.sh BUILD_TYPE=Release
```

#### GPU-Support aktivieren
```bash
ENABLE_GPU=1 ./build.sh BUILD_TYPE=Release
# Benötigt: CUDA Toolkit, cuDNN
```

#### Benchmarks aktivieren
```bash
ENABLE_BENCHMARKS=1 ./build.sh BUILD_TYPE=Release
```

#### Strict Build (Warnings as Errors)
```bash
STRICT=1 ./build.sh BUILD_TYPE=Release
```

### 3. Server starten (Linux/WSL)

```bash
# Standard (WSL: build-wsl/, Linux: build/)
./build-wsl/themis_server --config config.yaml

# Oder mit systemd Service (siehe Production Deployment)
sudo systemctl start themisdb
```

### 4. Umgebungsvariablen (Build)

Alle Build-Optionen als Umgebungsvariablen:

```bash
export BUILD_DIR=build-custom
export BUILD_TYPE=Release
export GENERATOR=Ninja
export ENABLE_TESTS=1
export ENABLE_BENCHMARKS=1
export ENABLE_GPU=0
export STRICT=1

./build.sh
```

## Docker Build & Deployment

### 1. Container Images

ThemisDB bietet Multi-Arch Docker Images für x86_64 und ARM64:

#### GitHub Container Registry (empfohlen)
```bash
docker pull ghcr.io/makr-code/themis:latest
docker pull ghcr.io/makr-code/themis:v0.1.0-alpha
```

#### Docker Hub
```bash
docker pull themisdb/themis:latest
docker pull themisdb/themis:v0.1.0-alpha
```

### 2. Container starten

#### Einfaches Deployment
```bash
docker run -d \
  --name themisdb \
  -p 8765:8765 \
  -v themis-data:/data \
  ghcr.io/makr-code/themis:latest
```

#### Mit Custom Config
```bash
docker run -d \
  --name themisdb \
  -p 8765:8765 \
  -v themis-data:/data \
  -v $(pwd)/config.yaml:/etc/themis/config.yaml \
  ghcr.io/makr-code/themis:latest --config /etc/themis/config.yaml
```

#### Docker Compose
```yaml
# docker-compose.yml
version: '3.8'
services:
  themisdb:
    image: ghcr.io/makr-code/themis:latest
    ports:
      - "8765:8765"
    volumes:
      - themis-data:/data
      - ./config.yaml:/etc/themis/config.yaml
    environment:
      - THEMIS_LOG_LEVEL=info
    restart: unless-stopped

volumes:
  themis-data:
```

```bash
docker-compose up -d
```

### 3. Eigenes Docker-Image bauen

#### Standard Build
```bash
# Vollständiger Build (Build + Runtime)
docker build -t themisdb:local .

# Nur Build-Stage testen
docker build --target build -t themisdb-build-test .
```

#### Multi-Arch Build (Buildx)
```bash
# Setup Buildx
docker buildx create --name multiarch --use

# Build für AMD64 und ARM64
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  -t themisdb/themis:v0.1.0-alpha \
  --push \
  .
```

### 4. Docker Build-Konfiguration

**Basis-Image:** Ubuntu 22.04 (GLIBC 2.35)  
**Dependency Manager:** vcpkg (vollständiger Clone)  
**Build System:** CMake + Ninja  
**Compiler:** GCC 11.4.0

**Kern-Abhängigkeiten** (aus `vcpkg.docker.json`):
- OpenSSL (TLS/Crypto)
- RocksDB mit LZ4 + ZSTD (Storage Engine)
- simdjson (Fast JSON Parsing)
- Intel TBB (Threading)
- Apache Arrow (JSON + Filesystem)
- spdlog (Logging)
- yaml-cpp (Config)
- Boost (Asio + Beast)
- nlohmann-json (JSON)
- hnswlib (Vector Search)

**Build-Zeiten:**
- vcpkg Clone: ~40s
- vcpkg Install: ~20-30 Minuten (alle Pakete kompilieren)
- CMake Build: ~5 Minuten
- **Gesamt:** ~30-40 Minuten

**Optimierungen:**
- Multi-stage Build: Runtime-Image ist minimal (~300 MB)
- Layer Caching: vcpkg-Manifest zuerst kopieren
- Reduced Arrow: Nur JSON + Filesystem Features

## QNAP Deployment

### 1. Pull von Registry

```bash
# SSH auf QNAP
ssh admin@qnap-ip

# Container Station öffnen oder via CLI:
docker pull ghcr.io/makr-code/themis:latest
```

### 2. QNAP Docker Compose

Verwende `docker-compose.qnap.yml`:

```yaml
# docker-compose.qnap.yml
version: '3.8'
services:
  themisdb:
    image: ghcr.io/makr-code/themis:latest
    container_name: themisdb
    ports:
      - "8765:8765"
    volumes:
      - /share/Container/themisdb/data:/data
      - /share/Container/themisdb/config.yaml:/etc/themis/config.yaml
    environment:
      - TZ=Europe/Berlin
      - THEMIS_LOG_LEVEL=info
    restart: always
```

```bash
# Deployment
docker-compose -f docker-compose.qnap.yml up -d

# Logs anzeigen
docker-compose -f docker-compose.qnap.yml logs -f

# Status prüfen
docker-compose -f docker-compose.qnap.yml ps
```

### 3. QNAP Container Station UI

1. **Container Station** öffnen
2. **Create** → **Create Application**
3. `docker-compose.qnap.yml` hochladen
4. **Validate and Apply**
5. Container sollte automatisch starten

### 4. QNAP-spezifische Konfiguration

**Empfohlene Volumes:**
- `/share/Container/themisdb/data` - Persistente Daten
- `/share/Container/themisdb/logs` - Log-Dateien
- `/share/Container/themisdb/config.yaml` - Konfiguration

**Performance-Tuning für QNAP:**
```yaml
# config.yaml (QNAP-optimiert)
storage:
  rocksdb_path: /data/rocksdb
  memtable_size_mb: 128    # Reduziert für QNAP RAM
  block_cache_size_mb: 512 # Anpassen je nach QNAP-Modell
  max_open_files: 5000

server:
  worker_threads: 4        # Anpassen an QNAP CPU
  port: 8765
  host: 0.0.0.0

logging:
  level: info
  file: /data/logs/themis.log
```

## Konfiguration

ThemisDB unterstützt **YAML** (empfohlen) und **JSON** Konfigurationsdateien.

### Standard-Suchpfade (in dieser Reihenfolge):
1. `./config.yaml`, `./config.yml`, `./config.json`
2. `./config/config.yaml`, `./config/config.yml`, `./config/config.json`
3. `/etc/themis/config.yaml`, `/etc/themis/config.yml`, `/etc/themis/config.json`

### Minimal-Konfiguration

**config.yaml:**
```yaml
storage:
  rocksdb_path: ./data/themis_server

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 8

logging:
  level: info
```

### Production-Konfiguration

**config.yaml (Production):**
```yaml
storage:
  rocksdb_path: /var/lib/themisdb/data
  memtable_size_mb: 256
  block_cache_size_mb: 1024
  max_open_files: 10000
  enable_statistics: true
  compression:
    default: lz4
    bottommost: zstd

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 16
  request_timeout_ms: 30000
  max_request_size_mb: 10

security:
  tls:
    enabled: true
    cert_file: /etc/themisdb/certs/server.crt
    key_file: /etc/themisdb/certs/server.key
    min_version: "1.3"
  rate_limiting:
    enabled: true
    requests_per_minute: 100
  rbac:
    enabled: true
    roles_file: /etc/themisdb/rbac/roles.yaml

logging:
  level: info
  file: /var/log/themisdb/server.log
  rotation_size_mb: 100
  max_files: 10
  format: json

vector_index:
  engine: hnsw
  hnsw_m: 16
  hnsw_ef_construction: 200
  persistence_path: /var/lib/themisdb/hnsw

features:
  semantic_cache: true
  cdc: true
  timeseries: true

tracing:
  enabled: true
  service_name: themisdb-prod
  otlp_endpoint: http://jaeger:4318
```

### Environment Variables Override

```bash
export THEMIS_SERVER_PORT=9000
export THEMIS_STORAGE_PATH=/mnt/nvme/themisdb
export THEMIS_LOG_LEVEL=debug
export THEMIS_WORKER_THREADS=16

./themis_server --config config.yaml
# Überschreibt Port, Storage-Pfad, Log-Level und Worker-Threads
```

## Production Deployment

### Linux Systemd Service

**`/etc/systemd/system/themisdb.service`:**
```ini
[Unit]
Description=ThemisDB Multi-Model Database
After=network.target

[Service]
Type=simple
User=themisdb
Group=themisdb
WorkingDirectory=/opt/themisdb
ExecStart=/opt/themisdb/bin/themis_server --config /etc/themisdb/config.yaml
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

# Security Hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/themisdb /var/log/themisdb

# Resource Limits
LimitNOFILE=65536
LimitNPROC=4096

[Install]
WantedBy=multi-user.target
```

```bash
# Service aktivieren und starten
sudo systemctl daemon-reload
sudo systemctl enable themisdb
sudo systemctl start themisdb

# Status prüfen
sudo systemctl status themisdb

# Logs anzeigen
sudo journalctl -u themisdb -f
```

### Nginx Reverse Proxy

```nginx
upstream themisdb {
    server 127.0.0.1:8765;
}

server {
    listen 80;
    server_name db.example.com;
    return 301 https://$server_name$request_uri;
}

server {
    listen 443 ssl http2;
    server_name db.example.com;

    ssl_certificate /etc/letsencrypt/live/db.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/db.example.com/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;

    location / {
        proxy_pass http://themisdb;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    location /metrics {
        proxy_pass http://themisdb/metrics;
        allow 10.0.0.0/8;    # Prometheus Server
        deny all;
    }
}
```

## Monitoring & Observability

### Prometheus Scraping

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'themisdb'
    static_configs:
      - targets: ['localhost:8765']
    metrics_path: /metrics
    scrape_interval: 15s
```

### Health Checks

```bash
# Health Endpoint
curl http://localhost:8765/health
# Response: {"status": "ok", "version": "v0.1.0_alpha"}

# Stats Endpoint
curl http://localhost:8765/stats | jq
# Liefert Server- und RocksDB-Statistiken

# Metrics Endpoint (Prometheus)
curl http://localhost:8765/metrics
```

## Backup & Recovery

### RocksDB Checkpoint Backup

```bash
# Backup erstellen (via API)
curl -X POST http://localhost:8765/admin/backup \
  -H "Content-Type: application/json" \
  -d '{"backup_path": "/backup/themisdb-$(date +%Y%m%d)"}'

# Manuelles Backup (Server muss gestoppt sein)
sudo systemctl stop themisdb
cp -r /var/lib/themisdb/data /backup/themisdb-data-$(date +%Y%m%d)
sudo systemctl start themisdb
```

### Automated Backup Script

```bash
#!/bin/bash
# /usr/local/bin/themisdb-backup.sh

BACKUP_DIR="/backup/themisdb"
RETENTION_DAYS=7
DATE=$(date +%Y%m%d_%H%M%S)

# Create backup via API
curl -X POST http://localhost:8765/admin/backup \
  -H "Content-Type: application/json" \
  -d "{\"backup_path\": \"$BACKUP_DIR/backup-$DATE\"}"

# Delete old backups
find $BACKUP_DIR -type d -name "backup-*" -mtime +$RETENTION_DAYS -exec rm -rf {} \;
```

```bash
# Cronjob (täglich um 2 Uhr)
0 2 * * * /usr/local/bin/themisdb-backup.sh >> /var/log/themisdb-backup.log 2>&1
```

## Troubleshooting

### Server startet nicht

**Problem:** Port bereits belegt
```bash
# Port-Usage prüfen
sudo netstat -tulpn | grep 8765

# Anderen Port verwenden
./themis_server --port 9000
```

**Problem:** Datenbank-Pfad nicht beschreibbar
```bash
# Permissions prüfen
ls -la /var/lib/themisdb/

# Owner anpassen
sudo chown -R themisdb:themisdb /var/lib/themisdb/
```

### Build-Fehler

**Problem:** vcpkg baseline not found
```bash
# vcpkg aktualisieren
cd $VCPKG_ROOT
git pull
vcpkg x-update-baseline
```

**Problem:** Compiler-Version zu alt
```bash
# GCC Version prüfen
gcc --version  # Sollte >= 11.0

# GCC 11 installieren (Ubuntu)
sudo apt install gcc-11 g++-11
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110
```

### Performance-Probleme

**Problem:** Hohe Latenz bei Writes
```yaml
# config.yaml anpassen
storage:
  memtable_size_mb: 512  # Größerer Memtable
  max_background_jobs: 8  # Mehr Compaction-Threads
```

**Problem:** Hoher Memory-Usage
```yaml
storage:
  block_cache_size_mb: 512  # Block-Cache reduzieren
  max_open_files: 5000      # Weniger offene Files
```

### Docker-Probleme

**Problem:** Container crasht nach Start
```bash
# Logs anzeigen
docker logs themisdb

# Container interaktiv starten
docker run -it --rm ghcr.io/makr-code/themis:latest /bin/bash
```

**Problem:** Volumes nicht persistent
```bash
# Named Volume verwenden
docker volume create themis-data
docker run -v themis-data:/data ...
```

---

## Siehe auch

- [README.md](../README.md) - Projekt-Übersicht
- [DOCKER_BUILD.md](../DOCKER_BUILD.md) - Docker Build Details
- [Security Hardening](security_hardening_guide.md) - Sicherheitskonfiguration
- [Operations Runbook](operations_runbook.md) - Day-2 Operations
