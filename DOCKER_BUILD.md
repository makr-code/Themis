# ThemisDB Docker Build und Deployment

## Build-Prozess

### Docker-Image bauen
```powershell
# Vollständiges Build (Build + Runtime)
docker build -t themisdb/themis:v0.1.0-alpha .

# Nur Build-Stage testen
docker build --target build -t themisdb-test-build .
```

### Build-Konfiguration

- **Base Image**: Ubuntu 22.04 (GLIBC 2.35)
- **Dependency Manager**: vcpkg (vollständiger Clone)
- **Build System**: CMake + Ninja
- **Compiler**: GCC 11.4.0

### Abhängigkeiten (vcpkg.docker.json)

Kern-Bibliotheken:
- OpenSSL (TLS/Krypto)
- RocksDB mit LZ4 + ZSTD (Storage Engine)
- simdjson (Fast JSON Parsing)
- TBB (Threading)
- Arrow mit JSON + Filesystem (Columnar Data)
- spdlog (Logging)
- yaml-cpp (Konfiguration)
- Boost (Asio + Beast für Networking)
- nlohmann-json (JSON)
- hnswlib (Vector Search)

### Build-Zeiten

- vcpkg Clone: ~40s
- vcpkg Install: ~20-30 Minuten (alle Pakete kompilieren)
- CMake Build: ~5 Minuten
- **Gesamt**: ~30-40 Minuten

### Optimierungen

- Multi-stage Build: Runtime-Image ist minimal
- Layer Caching: vcpkg-Manifest zuerst kopieren
- Reduced Arrow: Nur JSON + Filesystem Features (nicht Parquet/Compute)

## Docker Hub Deployment

### Login
```powershell
docker login
```

### Tag und Push
```powershell
# Tag v0.1.0-alpha
docker tag themisdb/themis:v0.1.0-alpha themisdb/themis:v0.1.0-alpha
docker push themisdb/themis:v0.1.0-alpha

# Tag qnap-latest
docker tag themisdb/themis:v0.1.0-alpha themisdb/themis:qnap-latest
docker push themisdb/themis:qnap-latest

# Tag latest
docker tag themisdb/themis:v0.1.0-alpha themisdb/themis:latest
docker push themisdb/themis:latest
```

### Oder alle auf einmal
```powershell
docker build -t themisdb/themis:v0.1.0-alpha -t themisdb/themis:qnap-latest -t themisdb/themis:latest .
docker push themisdb/themis:v0.1.0-alpha
docker push themisdb/themis:qnap-latest
docker push themisdb/themis:latest
```

## Verwendung

### Container starten
```powershell
docker run -d `
  -p 8080:8080 `
  -p 18765:18765 `
  -v themis-data:/data `
  themisdb/themis:qnap-latest
```

### Mit custom config
```powershell
docker run -d `
  -p 8080:8080 `
  -v /pfad/zu/config.json:/etc/themis/config.json `
  -v themis-data:/data `
  themisdb/themis:qnap-latest --config /etc/themis/config.json
```

### Docker Compose (für QNAP)
Siehe `docker-compose.qnap.yml` für Production-Setup.

## Troubleshooting

### Build schlägt fehl: "shallow repository"
- vcpkg muss vollständig geklont werden (nicht --depth 1)
- Bereits im Dockerfile behoben

### Build schlägt fehl: "baseline not found"
- vcpkg-configuration.json wird dynamisch mit aktuellem Commit generiert
- Keine feste Baseline mehr nötig

### xsimd build error
- Arrow-Features reduziert auf json + filesystem
- parquet + compute Features benötigen xsimd (build-intensiv)

## Änderungen gegenüber lokalem Build

- `vcpkg.docker.json` statt `vcpkg.json` (reduzierte Dependencies)
- Keine opentelemetry-cpp (optional, build-intensiv)
- Arrow ohne parquet/compute Features
- Keine Benchmarks, Tests im Docker-Build
