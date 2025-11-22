# ThemisDB Package Maintainer Quick Reference

This is a quick reference guide for package maintainers. For detailed instructions, see [packaging.md](packaging.md).

## Version Update Checklist

When preparing a new release:

```bash
# Linux/macOS
./scripts/prepare-release.sh 1.0.1

# Windows
.\scripts\prepare-release.ps1 -Version 1.0.1
```

Manual updates if needed:
- [ ] `CMakeLists.txt` - project version
- [ ] `vcpkg.json` - package version
- [ ] `debian/changelog` - add new entry
- [ ] `themisdb.spec` - Version field
- [ ] `PKGBUILD` - pkgver and reset pkgrel to 1
- [ ] `packaging/chocolatey/themisdb.nuspec` - version tag
- [ ] `packaging/homebrew/themisdb.rb` - url with new tag
- [ ] WinGet manifests - PackageVersion in all YAML files

## Build Commands Quick Reference

### Debian/Ubuntu
```bash
dpkg-buildpackage -us -uc -b
# Output: ../themisdb_VERSION-1_amd64.deb
```

### Fedora/RHEL/CentOS
```bash
rpmdev-setuptree
wget https://github.com/makr-code/ThemisDB/archive/vVERSION.tar.gz -P ~/rpmbuild/SOURCES/
cp themisdb.spec ~/rpmbuild/SPECS/
rpmbuild -ba ~/rpmbuild/SPECS/themisdb.spec
# Output: ~/rpmbuild/RPMS/x86_64/themisdb-VERSION-1.x86_64.rpm
```

### Arch Linux
```bash
makepkg -si
# Output: themisdb-VERSION-1-x86_64.pkg.tar.zst
```

### Chocolatey
```powershell
cd packaging/chocolatey
choco pack
# Output: themisdb.VERSION.nupkg
```

### Homebrew
```bash
brew install --build-from-source packaging/homebrew/themisdb.rb
brew test themisdb
```

## Dependencies by Platform

### Debian/Ubuntu Runtime
- libssl3 | libssl1.1
- librocksdb8.1 | librocksdb6.11
- libtbb12 | libtbb2
- libarrow1500+ (or 1300, 1000)
- libboost-system1.74.0+
- libspdlog1+
- libcurl4
- libyaml-cpp0.7 | libyaml-cpp0.6
- libzstd1

### Fedora/RHEL/CentOS Runtime
- openssl-libs
- rocksdb
- tbb
- arrow-libs
- boost-system
- spdlog
- libcurl
- yaml-cpp
- libzstd

### Arch Linux Runtime
- openssl
- rocksdb
- intel-tbb
- arrow
- boost-libs
- spdlog
- curl
- yaml-cpp
- zstd

### macOS Homebrew
- openssl@3
- rocksdb
- tbb
- apache-arrow
- boost
- spdlog
- nlohmann-json
- curl
- yaml-cpp
- zstd

## Installation Paths

### Linux (FHS Standard)
- Binary: `/usr/bin/themis_server`
- Headers: `/usr/include/`
- Static lib: `/usr/lib/libthemis_core.a`
- Config: `/etc/themisdb/config.yaml`
- Data: `/var/lib/themisdb/`
- Service: `/lib/systemd/system/themisdb.service`
- Logs: systemd journal (`journalctl -u themisdb`)

### Windows
- Binary: `C:\ProgramData\chocolatey\lib\themisdb\tools\bin\themis_server.exe`
- Config: `C:\ProgramData\ThemisDB\config.yaml`
- Data: `C:\ProgramData\ThemisDB\data\`
- Service: Windows Service "ThemisDB"

### macOS (Homebrew)
- Binary: `/opt/homebrew/bin/themis_server` (Apple Silicon) or `/usr/local/bin/themis_server` (Intel)
- Headers: `/opt/homebrew/include/` or `/usr/local/include/`
- Config: `/opt/homebrew/etc/themisdb/config.yaml` or `/usr/local/etc/themisdb/config.yaml`
- Data: `/opt/homebrew/var/lib/themisdb/` or `/usr/local/var/lib/themisdb/`
- Service: launchd plist
- Logs: `/opt/homebrew/var/log/themisdb.log` or `/usr/local/var/log/themisdb.log`

## Service Management

### systemd (Linux)
```bash
sudo systemctl start themisdb
sudo systemctl enable themisdb
sudo systemctl status themisdb
sudo journalctl -u themisdb -f
```

### Windows Service
```powershell
Start-Service ThemisDB
Set-Service ThemisDB -StartupType Automatic
Get-Service ThemisDB
```

### launchd (macOS)
```bash
brew services start themisdb
brew services list
```

## Security Notes

### User/Group Creation
- **Linux**: User `themisdb`, group `themisdb`, home `/var/lib/themisdb`
- **Windows**: Runs as LocalSystem (or configure specific user)
- **macOS**: Runs as current user via launchd

### File Permissions
- **Config**: 640 (root:themisdb on Linux)
- **Data dir**: 750 (themisdb:themisdb on Linux)
- **Binary**: 755 (root:root on Linux)

## Testing Package Builds

### Debian/Ubuntu
```bash
# Install in clean container
docker run -it --rm ubuntu:22.04
apt-get update && apt-get install -y ./themisdb_VERSION_amd64.deb
systemctl status themisdb
```

### Fedora
```bash
# Install in clean container
docker run -it --rm fedora:39
dnf install -y ./themisdb-VERSION.rpm
systemctl status themisdb
```

### Arch
```bash
# Install in clean container
docker run -it --rm archlinux:latest
pacman -U themisdb-VERSION.pkg.tar.zst
```

## Hash Calculation

### Linux/macOS
```bash
sha256sum themisdb-VERSION.tar.gz
```

### Windows
```powershell
Get-FileHash themisdb-VERSION.zip -Algorithm SHA256
```

## Repository Submission URLs

- **Debian PPA**: https://launchpad.net/
- **Fedora Copr**: https://copr.fedorainfracloud.org/
- **AUR**: https://aur.archlinux.org/
- **Chocolatey**: https://community.chocolatey.org/
- **WinGet**: https://github.com/microsoft/winget-pkgs
- **Homebrew Core**: https://github.com/Homebrew/homebrew-core

## Automated Builds

GitHub Actions workflow `.github/workflows/build-packages.yml` automatically builds packages when:
- A new release is published
- Manually triggered via workflow_dispatch

Artifacts are uploaded to the GitHub release.

## Support

- GitHub Issues: https://github.com/makr-code/ThemisDB/issues
- Packaging Guide: https://github.com/makr-code/ThemisDB/blob/main/docs/packaging.md
- Main Documentation: https://makr-code.github.io/ThemisDB/
