# Distribution Package Maintainer Information

This document provides essential information for distribution package maintainers.

## Project Information

- **Project Name**: ThemisDB
- **Description**: Multi-model database system with ACID transactions
- **License**: MIT
- **Homepage**: https://github.com/makr-code/ThemisDB
- **Documentation**: https://makr-code.github.io/ThemisDB/
- **Source**: https://github.com/makr-code/ThemisDB

## Current Maintainers

### Official Packages

If you are maintaining ThemisDB for a distribution, please add yourself here via pull request:

- **Debian/Ubuntu**: (seeking maintainer)
- **Fedora/RHEL/CentOS**: (seeking maintainer)
- **Arch Linux (AUR)**: (seeking maintainer)
- **Chocolatey**: (seeking maintainer)
- **WinGet**: (seeking maintainer)
- **Homebrew**: (seeking maintainer)

## Package Metadata

### Short Description
Multi-model database with relational, graph, vector, and time-series support

### Long Description
ThemisDB is a high-performance multi-model database system that provides:
- Relational data with secondary indexes (single, composite, range, geo, fulltext, TTL)
- Graph database with BFS, Dijkstra, and A* traversals
- Vector search using HNSW index (L2, Cosine, Dot Product metrics)
- Time-series data with Gorilla compression and continuous aggregates
- Document storage with JSON support
- Full ACID transactions with MVCC (Snapshot Isolation)
- Advanced Query Language (AQL) for hybrid queries across all models
- Enterprise security: TLS 1.3, RBAC, audit logging, secrets management
- Observability: OpenTelemetry tracing and Prometheus metrics
- Change Data Capture (CDC) for real-time event streaming

### Keywords/Tags
database, multi-model, graph, vector, timeseries, nosql, acid, mvcc, aql, opentelemetry, prometheus

### Categories
- Debian: database, devel
- Fedora: Applications/Databases
- Arch: database
- Homebrew: database

## Runtime Dependencies

### Required Libraries (all platforms)
- OpenSSL >= 1.1.1 or 3.x
- RocksDB >= 6.11
- Intel TBB >= 2020.0
- Apache Arrow >= 10.0.0
- Boost.System >= 1.71.0
- spdlog >= 1.8.0
- libcurl >= 7.68.0
- yaml-cpp >= 0.6.0
- zstd >= 1.4.0

### Optional Dependencies
- Prometheus (for metrics collection)
- HashiCorp Vault (for secrets management)
- OpenTelemetry Collector (for distributed tracing)

## Build Dependencies

In addition to runtime dependencies:
- CMake >= 3.20
- C++20 compatible compiler (GCC 11+, Clang 14+, MSVC 2019+)
- Ninja (recommended) or Make
- Git
- pkg-config
- nlohmann-json >= 3.9.0 (header-only, not runtime dep)

## Build Flags

### Recommended Build Configuration
```cmake
-DCMAKE_BUILD_TYPE=Release
-DTHEMIS_BUILD_TESTS=OFF
-DTHEMIS_BUILD_BENCHMARKS=OFF
-DTHEMIS_ENABLE_GPU=OFF
-DTHEMIS_STRICT_BUILD=OFF
-DBUILD_SHARED_LIBS=OFF
```

### Optional Features
- `-DTHEMIS_ENABLE_GPU=ON` - Enable GPU acceleration (requires CUDA/Faiss)
- `-DTHEMIS_ENABLE_TRACING=ON` - Enable OpenTelemetry tracing (default: ON)
- `-DTHEMIS_BUILD_TESTS=ON` - Build unit tests
- `-DTHEMIS_BUILD_BENCHMARKS=ON` - Build performance benchmarks

## Installation Layout

### Linux (FHS compliant)
- Binaries: `/usr/bin/themis_server`
- Libraries: `/usr/lib/libthemis_core.a`
- Headers: `/usr/include/themis/`
- Config: `/etc/themisdb/config.yaml`
- Data: `/var/lib/themisdb/`
- Systemd service: `/lib/systemd/system/themisdb.service`

### Windows
- Binaries: `%ProgramFiles%\ThemisDB\bin\themis_server.exe`
- Config: `%ProgramData%\ThemisDB\config.yaml`
- Data: `%ProgramData%\ThemisDB\data\`

### macOS (Homebrew)
- Binaries: `$(brew --prefix)/bin/themis_server`
- Config: `$(brew --prefix)/etc/themisdb/config.yaml`
- Data: `$(brew --prefix)/var/lib/themisdb/`

## User and Permissions

### Linux
- System user: `themisdb` (created during installation)
- System group: `themisdb`
- Home directory: `/var/lib/themisdb`
- Shell: `/sbin/nologin` or `/bin/false`

### Permissions
- Config directory: 750 (root:themisdb)
- Config file: 640 (root:themisdb)
- Data directory: 750 (themisdb:themisdb)
- Log files: 640 (themisdb:themisdb)

## Service Configuration

### systemd Service
- Service name: `themisdb.service`
- Type: `simple`
- Restart policy: `on-failure`
- Security: NoNewPrivileges, PrivateTmp, ProtectSystem=strict

### launchd (macOS)
- Service name: `homebrew.mxcl.themisdb`
- KeepAlive: true
- RunAtLoad: true

### Windows Service
- Service name: `ThemisDB`
- Display name: `ThemisDB Database Server`
- Start type: Automatic
- Recovery: Restart on failure

## Ports and Network

### Default Ports
- HTTP API: 8765 (TCP)
- Health check: Same as API port (`/health` endpoint)

### Firewall Considerations
- Packages should NOT automatically open firewall ports
- Document port requirements in README/post-install message

## Configuration

### Default Configuration
A default configuration file is provided at `config/config.yaml`. Package maintainers should:
1. Install as `/etc/themisdb/config.yaml` (Linux) or equivalent
2. Mark as configuration file (won't be overwritten on upgrade)
3. Adjust paths for the target platform

### Minimal Configuration
```yaml
storage:
  rocksdb_path: /var/lib/themisdb

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 8
```

## Security Considerations

### TLS/SSL
- ThemisDB supports TLS 1.3 for API connections
- Certificates should be managed by system administrators
- No certificates included in package

### Secrets Management
- Supports HashiCorp Vault integration
- Environment variable fallback for development
- No default secrets in configuration

### SELinux (RHEL/CentOS/Fedora)
- May require policy module for custom ports
- File contexts for `/var/lib/themisdb/`

## Upgrade Path

### Data Compatibility
- Data format is backward compatible within major versions
- Breaking changes only on major version bumps
- Migration scripts provided when needed

### Configuration Changes
- New configuration options use sensible defaults
- Old configuration files remain valid
- Deprecation warnings logged when using old options

## Testing

### Post-Install Verification
```bash
# Check service status
systemctl status themisdb  # Linux
brew services list | grep themisdb  # macOS

# Test health endpoint
curl http://localhost:8765/health

# Expected response: {"status":"ok","version":"1.0.0"}
```

### Basic Functionality Test
```bash
# Create an entity
curl -X PUT http://localhost:8765/entities/test:1 \
  -H "Content-Type: application/json" \
  -d '{"blob":"{\"value\":\"test\"}"}'

# Retrieve entity
curl http://localhost:8765/entities/test:1

# Clean up
curl -X DELETE http://localhost:8765/entities/test:1
```

## Known Issues

### Platform-Specific Issues

#### Debian/Ubuntu
- On Ubuntu 20.04, may need newer Arrow from PPA
- RocksDB 6.11+ recommended for optimal performance

#### Fedora/RHEL/CentOS
- EPEL required for some dependencies on RHEL/CentOS
- SELinux may need customization for non-standard ports

#### Arch Linux
- All dependencies available in official repos
- No known issues

#### macOS
- Apple Silicon: Uses native ARM64 binaries
- Intel: Uses x86_64 binaries
- OpenSSL from Homebrew required (system OpenSSL deprecated)

#### Windows
- Requires Visual C++ Redistributable 2019 or later
- Windows Defender may scan database files (performance impact)

## Release Schedule

- **Major releases**: Annually (1.x.0)
- **Minor releases**: Quarterly (x.1.0)
- **Patch releases**: As needed for critical bugs/security (x.x.1)
- **Pre-release testing**: 2 weeks before release
- **Security updates**: Within 48 hours of disclosure

## Communication Channels

### For Package Maintainers
- **Email**: info@themisdb.org
- **GitHub Issues**: Tag issues with `packaging` label
- **GitHub Discussions**: For general questions

### Coordinating Releases
We will:
- Notify maintainers 2 weeks before release
- Provide release candidates for testing
- Document breaking changes in CHANGELOG.md
- Tag maintainers in release announcements

### Security Updates
- **Critical**: Immediate notification via email + GitHub Security Advisory
- **High**: Notification within 24 hours
- **Medium/Low**: Included in regular release notes

## Resources

### Documentation
- **Packaging Guide**: `docs/packaging.md`
- **Quick Reference**: `docs/PACKAGING-QUICKREF.md`
- **Main Docs**: https://makr-code.github.io/ThemisDB/

### Source Files
- **Debian**: `debian/*`
- **RPM**: `themisdb.spec`
- **Arch**: `PKGBUILD`
- **Chocolatey**: `packaging/chocolatey/`
- **WinGet**: `packaging/winget/`
- **Homebrew**: `packaging/homebrew/themisdb.rb`

### CI/CD
- **GitHub Actions**: `.github/workflows/build-packages.yml`
- Builds packages for all platforms on release
- Artifacts uploaded to GitHub Releases

## License

ThemisDB is licensed under the MIT License. See LICENSE file for full text.

---

## Questions or Corrections?

If you find any inaccuracies in this document or have questions, please:
1. Open a GitHub issue with the `packaging` label
2. Email info@themisdb.org
3. Submit a pull request with corrections

Thank you for helping distribute ThemisDB! 🚀
