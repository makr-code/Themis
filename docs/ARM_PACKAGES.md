# Pre-built ARM Packages for ThemisDB

This guide explains how to install and use pre-built ThemisDB packages for ARM architectures.

## Supported Platforms

ThemisDB provides pre-built packages for the following ARM platforms:

| Distribution | Architecture | Format | Status |
|--------------|--------------|--------|--------|
| Debian 11+ | ARM64 | `.deb` | ✅ Available |
| Ubuntu 20.04+ | ARM64 | `.deb` | ✅ Available |
| Raspberry Pi OS | ARM64 | `.deb` | ✅ Available |
| Raspberry Pi OS | ARMv7 (32-bit) | `.deb` | ✅ Available |
| Fedora 38+ | ARM64 | `.rpm` | ✅ Available |
| RHEL 9+ | ARM64 | `.rpm` | ✅ Available |
| Rocky Linux 9+ | ARM64 | `.rpm` | ✅ Available |
| Arch Linux ARM | ARM64 | `.pkg.tar.zst` | ✅ Available |

## Installation

### Debian/Ubuntu/Raspberry Pi OS (ARM64)

**Download and install:**

```bash
# Download the latest ARM64 package
wget https://github.com/makr-code/ThemisDB/releases/download/v1.0.0/themisdb_1.0.0-1_arm64.deb

# Install package and dependencies
sudo apt update
sudo apt install ./themisdb_1.0.0-1_arm64.deb
```

**Start the service:**

```bash
# Enable and start ThemisDB
sudo systemctl enable themisdb
sudo systemctl start themisdb

# Check status
sudo systemctl status themisdb
```

**Configuration:**

Default configuration is at `/etc/themisdb/config.yaml`. Edit as needed:

```bash
sudo nano /etc/themisdb/config.yaml
sudo systemctl restart themisdb
```

### Raspberry Pi OS (ARMv7 32-bit)

For older Raspberry Pi models running 32-bit OS:

```bash
# Download ARMv7 package
wget https://github.com/makr-code/ThemisDB/releases/download/v1.0.0/themisdb_1.0.0-1_armhf.deb

# Install
sudo apt update
sudo apt install ./themisdb_1.0.0-1_armhf.deb

# Start service
sudo systemctl enable themisdb
sudo systemctl start themisdb
```

### RHEL/Fedora/Rocky Linux (ARM64)

**Download and install:**

```bash
# Fedora/RHEL 9+
wget https://github.com/makr-code/ThemisDB/releases/download/v1.0.0/themisdb-1.0.0-1.fc39.aarch64.rpm

# Install package
sudo dnf install themisdb-1.0.0-1.fc39.aarch64.rpm
```

**Start the service:**

```bash
sudo systemctl enable themisdb
sudo systemctl start themisdb
sudo systemctl status themisdb
```

### Arch Linux ARM

**Download and install:**

```bash
# Download package
wget https://github.com/makr-code/ThemisDB/releases/download/v1.0.0/themisdb-1.0.0-1-aarch64.pkg.tar.zst

# Install with pacman
sudo pacman -U themisdb-1.0.0-1-aarch64.pkg.tar.zst
```

**Start the service:**

```bash
sudo systemctl enable themisdb
sudo systemctl start themisdb
```

## Package Contents

All packages include:

### Binaries
- `/usr/bin/themis_server` - Main database server

### Configuration
- `/etc/themisdb/config.yaml` - Main configuration file
- Default settings optimized for ARM platforms

### Data Directory
- `/var/lib/themisdb/` - Database data storage
- Owned by `themisdb:themisdb` user/group

### Systemd Service
- `/usr/lib/systemd/system/themisdb.service`
- Automatic startup and supervision

### Documentation
- `/usr/share/doc/themisdb/` - Documentation files
- `/usr/share/licenses/themisdb/` - License information

## Post-Installation

### Verify Installation

```bash
# Check version
themis_server --version

# Check service status
sudo systemctl status themisdb

# View logs
sudo journalctl -u themisdb -f
```

### Basic Configuration

Edit `/etc/themisdb/config.yaml`:

```yaml
# Raspberry Pi 4 optimized settings
storage:
  rocksdb_path: /var/lib/themisdb/rocksdb
  memtable_size_mb: 128
  block_cache_size_mb: 512

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 4

vector_index:
  engine: hnsw
  hnsw_m: 16
  use_gpu: false
```

After editing, restart the service:

```bash
sudo systemctl restart themisdb
```

### Security Configuration

The package creates a dedicated `themisdb` user and group:

```bash
# Check user
id themisdb

# Verify permissions
ls -la /var/lib/themisdb
ls -la /etc/themisdb
```

**Recommended permissions:**
- Data directory: `750` (themisdb:themisdb)
- Config directory: `750` (root:themisdb)
- Config file: `640` (root:themisdb)

### Firewall Configuration

Open required ports:

**UFW (Ubuntu/Debian):**
```bash
sudo ufw allow 8765/tcp  # ThemisDB API
sudo ufw reload
```

**firewalld (Fedora/RHEL):**
```bash
sudo firewall-cmd --permanent --add-port=8765/tcp
sudo firewall-cmd --reload
```

## Updating

### Debian/Ubuntu

```bash
# Download new version
wget https://github.com/makr-code/ThemisDB/releases/download/v1.1.0/themisdb_1.1.0-1_arm64.deb

# Upgrade
sudo apt install ./themisdb_1.1.0-1_arm64.deb

# Service restarts automatically
```

### RHEL/Fedora

```bash
# Download new version
wget https://github.com/makr-code/ThemisDB/releases/download/v1.1.0/themisdb-1.1.0-1.fc39.aarch64.rpm

# Upgrade
sudo dnf upgrade themisdb-1.1.0-1.fc39.aarch64.rpm
```

### Arch Linux

```bash
# Download new version
wget https://github.com/makr-code/ThemisDB/releases/download/v1.1.0/themisdb-1.1.0-1-aarch64.pkg.tar.zst

# Upgrade
sudo pacman -U themisdb-1.1.0-1-aarch64.pkg.tar.zst
```

## Uninstalling

### Debian/Ubuntu

```bash
# Stop service
sudo systemctl stop themisdb
sudo systemctl disable themisdb

# Remove package (keep data)
sudo apt remove themisdb

# Remove package and data
sudo apt purge themisdb

# Clean up data manually if needed
sudo rm -rf /var/lib/themisdb
```

### RHEL/Fedora

```bash
# Stop service
sudo systemctl stop themisdb
sudo systemctl disable themisdb

# Remove package
sudo dnf remove themisdb

# Clean up data if not auto-removed
sudo rm -rf /var/lib/themisdb
```

### Arch Linux

```bash
# Stop service
sudo systemctl stop themisdb

# Remove package
sudo pacman -R themisdb

# Clean up data
sudo rm -rf /var/lib/themisdb
```

## Troubleshooting

### Service Won't Start

**Check logs:**
```bash
sudo journalctl -u themisdb -n 100 --no-pager
```

**Check configuration:**
```bash
themis_server --check-config /etc/themisdb/config.yaml
```

**Verify permissions:**
```bash
sudo ls -la /var/lib/themisdb
sudo ls -la /etc/themisdb
```

### Port Already in Use

If port 8765 is in use:

1. Edit `/etc/themisdb/config.yaml`
2. Change `server.port` to different port
3. Restart service

### Low Memory Issues (Raspberry Pi)

For Raspberry Pi with 2GB RAM or less:

```yaml
storage:
  memtable_size_mb: 64
  block_cache_size_mb: 256
```

Then restart:
```bash
sudo systemctl restart themisdb
```

### Permission Denied Errors

Fix ownership:
```bash
sudo chown -R themisdb:themisdb /var/lib/themisdb
sudo chmod 750 /var/lib/themisdb
```

## Development Packages

Development headers and libraries are available in separate packages:

### Debian/Ubuntu
```bash
sudo apt install themisdb-dev
```

### RHEL/Fedora
```bash
sudo dnf install themisdb-devel
```

### Arch Linux
Development files are included in main package.

**Development package includes:**
- Header files in `/usr/include/themis/`
- Static library: `/usr/lib/libthemis_core.a`
- CMake configuration files

## Building Custom Packages

If you need to build packages yourself:

### Debian/Ubuntu

```bash
# Install build dependencies
sudo apt install build-essential debhelper cmake ninja-build

# Clone repository
git clone https://github.com/makr-code/ThemisDB.git
cd ThemisDB

# Build package
dpkg-buildpackage -b -uc -us

# Install built package
sudo dpkg -i ../themisdb_*.deb
```

### RHEL/Fedora

```bash
# Install build tools
sudo dnf install rpm-build rpmdevtools

# Setup build tree
rpmdev-setuptree

# Build from spec file
rpmbuild -bb themisdb.spec
```

### Arch Linux

```bash
# Install build dependencies
sudo pacman -S base-devel

# Build from PKGBUILD
makepkg -s

# Install
sudo pacman -U themisdb-*.pkg.tar.zst
```

## Package Repository (Future)

**Planned:** APT and YUM repositories for easier installation:

```bash
# Debian/Ubuntu (planned)
echo "deb https://packages.themisdb.org/debian stable main" | \
    sudo tee /etc/apt/sources.list.d/themisdb.list
wget -O - https://packages.themisdb.org/key.gpg | sudo apt-key add -
sudo apt update
sudo apt install themisdb

# Fedora/RHEL (planned)
sudo dnf config-manager --add-repo https://packages.themisdb.org/rpm/themisdb.repo
sudo dnf install themisdb
```

## Performance Tips

### Raspberry Pi 4/5

**Enable performance governor:**
```bash
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

**Use SSD for data:**
Mount fast storage at `/var/lib/themisdb` for better performance.

**Optimize config:**
```yaml
storage:
  memtable_size_mb: 128  # 4GB RAM
  block_cache_size_mb: 512
  max_background_jobs: 4
  
server:
  worker_threads: 4  # Match CPU cores
```

### Monitoring

**Check resource usage:**
```bash
# CPU and memory
top -p $(pgrep themis_server)

# Disk I/O
sudo iotop -p $(pgrep themis_server)

# Service stats
sudo systemctl status themisdb
```

## Support

- **Issues:** https://github.com/makr-code/ThemisDB/issues
- **Documentation:** https://github.com/makr-code/ThemisDB/tree/main/docs
- **ARM Guide:** [docs/ARM_RASPBERRY_PI_BUILD.md](ARM_RASPBERRY_PI_BUILD.md)

## License

ThemisDB is released under the MIT License. See `/usr/share/licenses/themisdb/LICENSE` for details.
