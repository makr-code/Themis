# ThemisDB Packaging Guide

This document describes how to build and publish ThemisDB packages for various operating systems and package managers.

## Table of Contents

- [Linux Distributions](#linux-distributions)
  - [Debian/Ubuntu (.deb)](#debianubuntu-deb)
  - [Fedora/RHEL/CentOS (.rpm)](#fedorarhel-centos-rpm)
  - [Arch Linux (PKGBUILD)](#arch-linux-pkgbuild)
- [Windows](#windows)
  - [Chocolatey](#chocolatey)
  - [WinGet](#winget)
- [macOS](#macos)
  - [Homebrew](#homebrew)
- [Distribution Submission](#distribution-submission)

---

## Linux Distributions

### Debian/Ubuntu (.deb)

ThemisDB provides Debian packaging files in the `debian/` directory for building `.deb` packages compatible with Debian, Ubuntu, and derivatives.

#### Prerequisites

```bash
sudo apt-get install -y debhelper devscripts build-essential \
    cmake ninja-build pkg-config \
    libssl-dev librocksdb-dev libtbb-dev libarrow-dev \
    libboost-system-dev libspdlog-dev nlohmann-json3-dev \
    libcurl4-openssl-dev libyaml-cpp-dev libzstd-dev
```

#### Building the Package

```bash
# From the repository root
dpkg-buildpackage -us -uc -b

# Or using debuild
debuild -us -uc -b
```

The built packages will be placed in the parent directory:
- `themisdb_1.0.0-1_amd64.deb` - Main package
- `themisdb-dev_1.0.0-1_amd64.deb` - Development headers and libraries

#### Installing the Package

```bash
sudo dpkg -i ../themisdb_1.0.0-1_amd64.deb
sudo apt-get install -f  # Fix any dependency issues
```

#### Using the Installed Package

```bash
# Start the service
sudo systemctl start themisdb
sudo systemctl enable themisdb

# Check status
sudo systemctl status themisdb

# View logs
sudo journalctl -u themisdb -f

# Configuration file
sudo nano /etc/themisdb/config.yaml

# Data directory
ls -la /var/lib/themisdb/
```

#### Submitting to Debian/Ubuntu Repositories

1. **Debian**: Follow the [Debian New Maintainer's Guide](https://www.debian.org/doc/manuals/maint-guide/)
2. **Ubuntu PPA**: Create a PPA on [Launchpad](https://launchpad.net/)

```bash
# Sign the package
debuild -S -sa

# Upload to PPA
dput ppa:your-ppa-name themisdb_1.0.0-1_source.changes
```

---

### Fedora/RHEL/CentOS (.rpm)

ThemisDB provides an RPM spec file at `themisdb.spec` for building RPM packages.

#### Prerequisites

```bash
# Fedora/RHEL/CentOS
sudo dnf install -y rpm-build rpmdevtools
sudo dnf install -y gcc-c++ cmake ninja-build git pkg-config \
    openssl-devel rocksdb-devel tbb-devel arrow-devel \
    boost-devel spdlog-devel json-devel \
    libcurl-devel yaml-cpp-devel libzstd-devel
```

#### Setting Up Build Environment

```bash
# Create RPM build directory structure
rpmdev-setuptree

# Download source tarball
cd ~/rpmbuild/SOURCES
wget https://github.com/makr-code/ThemisDB/archive/v1.0.0.tar.gz

# Copy spec file
cp /path/to/ThemisDB/themisdb.spec ~/rpmbuild/SPECS/
```

#### Building the Package

```bash
cd ~/rpmbuild/SPECS
rpmbuild -ba themisdb.spec
```

Built packages are placed in:
- `~/rpmbuild/RPMS/x86_64/themisdb-1.0.0-1.x86_64.rpm` - Main package
- `~/rpmbuild/RPMS/x86_64/themisdb-devel-1.0.0-1.x86_64.rpm` - Development package

#### Installing the Package

```bash
sudo dnf install ~/rpmbuild/RPMS/x86_64/themisdb-1.0.0-1.x86_64.rpm
```

#### Using the Installed Package

```bash
# Start the service
sudo systemctl start themisdb
sudo systemctl enable themisdb

# Check status
sudo systemctl status themisdb

# Configuration file
sudo nano /etc/themisdb/config.yaml

# Data directory
ls -la /var/lib/themisdb/
```

#### Submitting to Fedora/RHEL Repositories

1. **Fedora**: Follow the [Fedora Package Maintainer Guide](https://docs.fedoraproject.org/en-US/package-maintainers/)
2. **EPEL**: Submit to [EPEL](https://docs.fedoraproject.org/en-US/epel/) for RHEL/CentOS compatibility
3. **Copr**: Create a repository on [Copr](https://copr.fedorainfracloud.org/)

---

### Arch Linux (PKGBUILD)

ThemisDB provides a `PKGBUILD` file for building Arch Linux packages.

#### Prerequisites

```bash
sudo pacman -S base-devel git cmake ninja
sudo pacman -S openssl rocksdb intel-tbb arrow boost spdlog \
               nlohmann-json curl yaml-cpp zstd
```

#### Building the Package

```bash
# Create a build directory
mkdir -p ~/build/themisdb
cd ~/build/themisdb

# Copy PKGBUILD and service file
cp /path/to/ThemisDB/PKGBUILD .
cp /path/to/ThemisDB/debian/themisdb.service .

# Build the package
makepkg -si
```

This will download the source, build ThemisDB, and install it automatically.

#### Manual Installation

```bash
# Build without installing
makepkg

# Install the built package
sudo pacman -U themisdb-1.0.0-1-x86_64.pkg.tar.zst
```

#### Using the Installed Package

```bash
# Start the service
sudo systemctl start themisdb
sudo systemctl enable themisdb

# Check status
sudo systemctl status themisdb

# Configuration file
sudo nano /etc/themisdb/config.yaml
```

#### Submitting to AUR

1. Create an account on [AUR](https://aur.archlinux.org/)
2. Clone the AUR repository:
   ```bash
   git clone ssh://aur@aur.archlinux.org/themisdb.git
   cd themisdb
   ```
3. Add files:
   ```bash
   cp /path/to/PKGBUILD .
   cp /path/to/themisdb.service .
   
   # Generate .SRCINFO
   makepkg --printsrcinfo > .SRCINFO
   ```
4. Commit and push:
   ```bash
   git add PKGBUILD .SRCINFO themisdb.service
   git commit -m "Initial commit: themisdb 1.0.0"
   git push
   ```

---

## Windows

### Chocolatey

ThemisDB provides Chocolatey packaging files in `packaging/chocolatey/`.

#### Prerequisites

- Windows PowerShell or PowerShell Core
- [Chocolatey](https://chocolatey.org/install) installed
- Visual Studio 2019+ or Build Tools

#### Building the Package

1. Build ThemisDB for Windows:
   ```powershell
   .\build.ps1 -BuildType Release
   ```

2. Create release archive:
   ```powershell
   # Create distribution directory
   $version = "1.0.0"
   $distDir = "dist\themisdb-$version-win64"
   New-Item -ItemType Directory -Force -Path $distDir\bin
   
   # Copy binaries
   Copy-Item build-msvc\Release\themis_server.exe $distDir\bin\
   Copy-Item config\config.yaml $distDir\
   
   # Create ZIP archive
   Compress-Archive -Path $distDir\* -DestinationPath "themisdb-$version-win64.zip"
   ```

3. Calculate SHA256 checksum:
   ```powershell
   Get-FileHash themisdb-$version-win64.zip -Algorithm SHA256
   ```

4. Update `packaging/chocolatey/tools/chocolateyinstall.ps1` with the checksum

5. Build Chocolatey package:
   ```powershell
   cd packaging\chocolatey
   choco pack
   ```

#### Testing the Package Locally

```powershell
choco install themisdb -s . -f
```

#### Submitting to Chocolatey Community Repository

1. Create an account on [Chocolatey Community](https://community.chocolatey.org/)
2. Get your API key from your account settings
3. Push the package:
   ```powershell
   choco apikey --key YOUR-API-KEY --source https://push.chocolatey.org/
   choco push themisdb.1.0.0.nupkg --source https://push.chocolatey.org/
   ```

---

### WinGet

ThemisDB provides WinGet manifest files in `packaging/winget/manifests/`.

#### Prerequisites

- Windows 10 1809+ or Windows 11
- [WinGet](https://github.com/microsoft/winget-cli) installed

#### Testing the Manifest Locally

```powershell
# Validate manifest
winget validate --manifest packaging\winget\manifests\t\ThemisDB\ThemisDB\1.0.0\

# Install from local manifest
winget install --manifest packaging\winget\manifests\t\ThemisDB\ThemisDB\1.0.0\ThemisDB.ThemisDB.yaml
```

#### Submitting to WinGet Community Repository

1. Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs)
2. Create a new branch:
   ```bash
   git checkout -b themisdb-1.0.0
   ```
3. Copy manifest files:
   ```bash
   mkdir -p manifests/t/ThemisDB/ThemisDB/1.0.0
   cp packaging/winget/manifests/t/ThemisDB/ThemisDB/1.0.0/* \
      manifests/t/ThemisDB/ThemisDB/1.0.0/
   ```
4. Update the SHA256 hash in the installer manifest
5. Commit and create a pull request to the upstream repository

---

## macOS

### Homebrew

ThemisDB provides a Homebrew Formula at `packaging/homebrew/themisdb.rb`.

#### Prerequisites

- macOS 11+
- [Homebrew](https://brew.sh/) installed
- Xcode Command Line Tools

#### Building from Formula

```bash
# Install dependencies
brew install cmake ninja pkg-config openssl@3 rocksdb tbb \
             apache-arrow boost spdlog nlohmann-json curl yaml-cpp zstd

# Build from local formula
brew install --build-from-source packaging/homebrew/themisdb.rb
```

#### Testing the Formula

```bash
# Test the formula
brew test themisdb

# Audit the formula
brew audit --strict themisdb
```

#### Using the Installed Package

```bash
# Start the service
brew services start themisdb

# Check status
brew services list | grep themisdb

# View logs
tail -f /opt/homebrew/var/log/themisdb.log

# Configuration file
nano /opt/homebrew/etc/themisdb/config.yaml
```

#### Submitting to Homebrew Core

1. Fork [Homebrew/homebrew-core](https://github.com/Homebrew/homebrew-core)
2. Create a new branch:
   ```bash
   git checkout -b themisdb
   ```
3. Add the formula:
   ```bash
   cp packaging/homebrew/themisdb.rb Formula/themisdb.rb
   ```
4. Update the SHA256 hash:
   ```bash
   # Download source tarball
   curl -L https://github.com/makr-code/ThemisDB/archive/v1.0.0.tar.gz \
        -o themisdb-1.0.0.tar.gz
   
   # Calculate hash
   shasum -a 256 themisdb-1.0.0.tar.gz
   
   # Update in Formula/themisdb.rb
   ```
5. Test the formula:
   ```bash
   brew install --build-from-source Formula/themisdb.rb
   brew test themisdb
   brew audit --strict themisdb
   ```
6. Commit and create a pull request

---

## Distribution Submission

### Pre-Submission Checklist

Before submitting to any distribution repository:

- [ ] Verify all dependencies are correctly listed
- [ ] Test package installation on a clean system
- [ ] Test package upgrade from previous version
- [ ] Test package removal/uninstallation
- [ ] Verify systemd service (Linux) works correctly
- [ ] Check file permissions and ownership
- [ ] Ensure configuration files are marked as config files
- [ ] Test that data directories are preserved on upgrade
- [ ] Verify license information is correct
- [ ] Update changelog/release notes

### Distribution-Specific Guidelines

Each distribution has specific requirements:

- **Debian**: Follow [Debian Policy Manual](https://www.debian.org/doc/debian-policy/)
- **Fedora**: Follow [Fedora Packaging Guidelines](https://docs.fedoraproject.org/en-US/packaging-guidelines/)
- **Arch**: Follow [Arch Packaging Standards](https://wiki.archlinux.org/title/Arch_package_guidelines)
- **Chocolatey**: Follow [Chocolatey Package Guidelines](https://docs.chocolatey.org/en-us/create/create-packages)
- **WinGet**: Follow [WinGet Manifest Guidelines](https://github.com/microsoft/winget-pkgs/blob/master/AUTHORING_MANIFESTS.md)
- **Homebrew**: Follow [Homebrew Formula Cookbook](https://docs.brew.sh/Formula-Cookbook)

### Community Repositories

For faster adoption, consider submitting to community-maintained repositories first:

- **Ubuntu PPA**: Personal Package Archive on Launchpad
- **Fedora Copr**: Community projects repository
- **AUR**: Arch User Repository
- **Homebrew Tap**: Custom Homebrew repository

Example of creating a Homebrew Tap:

```bash
# Create tap repository
git clone https://github.com/makr-code/homebrew-themisdb
cd homebrew-themisdb
mkdir Formula
cp ../ThemisDB/packaging/homebrew/themisdb.rb Formula/

# Users can then install with:
# brew tap makr-code/themisdb
# brew install themisdb
```

---

## Continuous Integration

Consider automating package builds using GitHub Actions:

```yaml
name: Build Packages

on:
  release:
    types: [published]

jobs:
  build-deb:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build Debian package
        run: |
          sudo apt-get install -y debhelper devscripts
          dpkg-buildpackage -us -uc -b
      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: debian-package
          path: ../*.deb

  build-rpm:
    runs-on: ubuntu-latest
    container: fedora:latest
    steps:
      - uses: actions/checkout@v3
      - name: Build RPM package
        run: |
          dnf install -y rpm-build rpmdevtools
          rpmdev-setuptree
          rpmbuild -ba themisdb.spec
      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: rpm-package
          path: ~/rpmbuild/RPMS/x86_64/*.rpm
```

---

## Support

For packaging issues or questions:

- GitHub Issues: https://github.com/makr-code/ThemisDB/issues
- Documentation: https://makr-code.github.io/ThemisDB/
- Email: info@themisdb.org
