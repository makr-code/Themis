# Raspberry Pi Performance Tuning Guide

This guide provides detailed optimization strategies for running ThemisDB on Raspberry Pi devices.

## Table of Contents

- [Hardware-Specific Configurations](#hardware-specific-configurations)
- [System-Level Optimizations](#system-level-optimizations)
- [Storage Optimizations](#storage-optimizations)
- [Memory Management](#memory-management)
- [CPU and Thermal Management](#cpu-and-thermal-management)
- [Network Optimizations](#network-optimizations)
- [Monitoring and Diagnostics](#monitoring-and-diagnostics)

## Hardware-Specific Configurations

### Raspberry Pi 5 (8GB RAM)

**Best for:** Production workloads, vector search, full features

**Pre-configured:** Use `config/config.rpi5.json`

```bash
cp config/config.rpi5.json config/config.json
```

**Key Settings:**
- Memory: 256MB memtable, 1GB block cache
- Vector Index: HNSW M=20, up to 200k vectors
- All features enabled including CDC and tracing

### Raspberry Pi 4 (4GB RAM)

**Best for:** Development, medium workloads

**Pre-configured:** Use `config/config.rpi4.json`

```bash
cp config/config.rpi4.json config/config.json
```

**Key Settings:**
- Memory: 128MB memtable, 512MB block cache
- Vector Index: HNSW M=16, up to 100k vectors
- Most features enabled, CDC optional

### Raspberry Pi 3 (2GB RAM)

**Best for:** Testing, light workloads, edge deployment

**Pre-configured:** Use `config/config.rpi3.json`

```bash
cp config/config.rpi3.json config/config.json
```

**Key Settings:**
- Memory: 64MB memtable, 256MB block cache
- Vector Index: HNSW M=12, up to 50k vectors
- Minimal features, optimized for low memory

## System-Level Optimizations

### CPU Governor

Set CPU to performance mode for consistent performance:

```bash
# Check current governor
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Set to performance mode (all cores)
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Make permanent (add to /etc/rc.local)
sudo nano /etc/rc.local
# Add before 'exit 0':
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

**For Raspberry Pi 5:**
```bash
# Enable higher clock speeds
sudo sh -c 'echo "arm_freq=2400" >> /boot/firmware/config.txt'
sudo sh -c 'echo "over_voltage=6" >> /boot/firmware/config.txt'
sudo reboot
```

### Memory Configuration

**Disable GPU memory allocation** (headless servers):

```bash
# Edit boot config
sudo nano /boot/firmware/config.txt

# Add or modify:
gpu_mem=16

# Reboot
sudo reboot
```

**Swap Configuration:**

For Raspberry Pi with 2-4GB RAM, configure swap:

```bash
# Disable current swap
sudo dphys-swapfile swapoff

# Edit swap configuration
sudo nano /etc/dphys-swapfile

# Set swap size (2GB recommended)
CONF_SWAPSIZE=2048

# Rebuild and enable
sudo dphys-swapfile setup
sudo dphys-swapfile swapon

# Adjust swappiness (lower = less swap usage)
sudo sysctl vm.swappiness=10
echo "vm.swappiness=10" | sudo tee -a /etc/sysctl.conf
```

**For production systems** with adequate RAM, disable swap:

```bash
sudo dphys-swapfile swapoff
sudo systemctl disable dphys-swapfile
```

### File Descriptor Limits

Increase for high-connection scenarios:

```bash
# Check current limits
ulimit -n

# Increase system-wide
sudo nano /etc/security/limits.conf

# Add:
themisdb soft nofile 65536
themisdb hard nofile 65536

# For systemd service
sudo mkdir -p /etc/systemd/system/themisdb.service.d/
sudo nano /etc/systemd/system/themisdb.service.d/limits.conf

# Add:
[Service]
LimitNOFILE=65536

# Reload and restart
sudo systemctl daemon-reload
sudo systemctl restart themisdb
```

## Storage Optimizations

### SSD vs SD Card

**Highly recommended:** Use SSD via USB 3.0 for data storage.

**Performance comparison:**
- SD Card: ~30-50 MB/s random I/O
- SSD (USB 3.0): ~200-400 MB/s random I/O
- NVMe (via HAT): ~500-1000 MB/s random I/O

**Mount SSD for data:**

```bash
# Identify SSD
lsblk

# Format (one-time)
sudo mkfs.ext4 /dev/sda1

# Create mount point
sudo mkdir -p /mnt/themisdb-data

# Mount
sudo mount /dev/sda1 /mnt/themisdb-data

# Set ownership
sudo chown themisdb:themisdb /mnt/themisdb-data

# Auto-mount on boot
sudo nano /etc/fstab
# Add:
/dev/sda1 /mnt/themisdb-data ext4 defaults,noatime 0 2

# Update config
sudo nano /etc/themisdb/config.json
# Set:
"rocksdb_path": "/mnt/themisdb-data/rocksdb"
"save_path": "/mnt/themisdb-data/vector_indexes"
```

### Filesystem Tuning

**For ext4 on SSD:**

```bash
# Remount with optimal flags
sudo mount -o remount,noatime,nodiratime,commit=60 /mnt/themisdb-data

# Make permanent in /etc/fstab:
/dev/sda1 /mnt/themisdb-data ext4 defaults,noatime,nodiratime,commit=60 0 2
```

**I/O Scheduler:**

```bash
# For SSD, use 'none' or 'deadline'
echo none | sudo tee /sys/block/sda/queue/scheduler

# Make permanent
sudo nano /etc/udev/rules.d/60-scheduler.rules
# Add:
ACTION=="add|change", KERNEL=="sd[a-z]", ATTR{queue/scheduler}="none"
```

## Memory Management

### Transparent Huge Pages

Disable for better RocksDB performance:

```bash
# Disable THP
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag

# Make permanent
sudo nano /etc/rc.local
# Add before 'exit 0':
echo never > /sys/kernel/mm/transparent_hugepage/enabled
echo never > /sys/kernel/mm/transparent_hugepage/defrag
```

### OOM Killer Protection

Protect ThemisDB from OOM killer:

```bash
# Find ThemisDB PID
THEMIS_PID=$(pgrep themis_server)

# Set OOM score adjustment (-1000 = never kill)
echo -500 | sudo tee /proc/$THEMIS_PID/oom_score_adj

# Make permanent in systemd service
sudo nano /etc/systemd/system/themisdb.service.d/oom.conf

# Add:
[Service]
OOMScoreAdjust=-500
```

### Memory Allocator

Use jemalloc for better memory management:

```bash
# Install jemalloc
sudo apt install libjemalloc2

# Preload for ThemisDB
sudo nano /etc/systemd/system/themisdb.service.d/jemalloc.conf

# Add:
[Service]
Environment="LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libjemalloc.so.2"

# Reload and restart
sudo systemctl daemon-reload
sudo systemctl restart themisdb
```

## CPU and Thermal Management

### Cooling

**Essential for sustained performance:**

- **Raspberry Pi 4/5:** Use official active cooling fan or passive heatsink
- **Target temperature:** <60°C under load
- **Throttling starts:** ~80°C

**Monitor temperature:**

```bash
# Current temperature
vcgencmd measure_temp

# Continuous monitoring
watch -n 1 vcgencmd measure_temp

# Check throttling
vcgencmd get_throttled
# 0x0 = no throttling
```

### CPU Affinity

Pin ThemisDB to specific cores:

```bash
# Pin to cores 0-3
sudo systemctl edit themisdb

# Add:
[Service]
CPUAffinity=0 1 2 3
```

### Disable Unnecessary Services

Free up resources:

```bash
# Disable Bluetooth (if not needed)
sudo systemctl disable bluetooth
sudo systemctl disable hciuart

# Disable WiFi (if using Ethernet)
sudo systemctl disable wpa_supplicant

# Disable GUI (headless server)
sudo systemctl set-default multi-user.target
```

## Network Optimizations

### TCP Tuning

Optimize for database workloads:

```bash
sudo nano /etc/sysctl.conf

# Add:
net.core.rmem_max=16777216
net.core.wmem_max=16777216
net.ipv4.tcp_rmem=4096 87380 16777216
net.ipv4.tcp_wmem=4096 65536 16777216
net.core.netdev_max_backlog=5000
net.ipv4.tcp_max_syn_backlog=3240
net.ipv4.tcp_fin_timeout=30
net.ipv4.tcp_keepalive_time=300
net.ipv4.tcp_keepalive_intvl=30
net.ipv4.tcp_keepalive_probes=5

# Apply
sudo sysctl -p
```

### Network Interface Tuning

```bash
# Increase ring buffer size
sudo ethtool -G eth0 rx 4096 tx 4096

# Disable offloading (if experiencing issues)
sudo ethtool -K eth0 tso off gso off
```

## Monitoring and Diagnostics

### Resource Monitoring

**Install monitoring tools:**

```bash
sudo apt install htop iotop sysstat
```

**Monitor ThemisDB:**

```bash
# CPU and memory usage
htop -p $(pgrep themis_server)

# Disk I/O
sudo iotop -p $(pgrep themis_server)

# Network connections
sudo netstat -plant | grep themis

# File descriptors
lsof -p $(pgrep themis_server) | wc -l
```

### Performance Metrics

**Built-in statistics:**

```bash
# ThemisDB metrics endpoint
curl http://localhost:8765/metrics

# RocksDB statistics
curl http://localhost:8765/stats
```

### Logging Configuration

Optimize logging for production:

```bash
sudo nano /etc/themisdb/config.json

# Adjust log level:
{
  "logging": {
    "level": "info",  # Use "warn" for production
    "file": "/var/log/themisdb/themis.log",
    "max_size_mb": 100,
    "max_files": 5
  }
}

# Rotate logs
sudo nano /etc/logrotate.d/themisdb

# Add:
/var/log/themisdb/*.log {
    daily
    rotate 7
    compress
    delaycompress
    notifempty
    create 0640 themisdb themisdb
    sharedscripts
    postrotate
        systemctl reload themisdb > /dev/null 2>&1 || true
    endscript
}
```

## Benchmark Your Setup

After optimizations, run benchmarks:

```bash
# Build with benchmarks
cmake --preset rpi-arm64-gcc-release -DTHEMIS_BUILD_BENCHMARKS=ON
cmake --build --preset rpi-arm64-gcc-release

# Run ARM benchmarks
./scripts/run-arm-benchmarks.sh

# Run application benchmarks
./build-*/bench_crud
./build-*/bench_vector_search
```

## Quick Optimization Checklist

**Essential (Do First):**
- [ ] Use SSD for data storage
- [ ] Set CPU governor to performance
- [ ] Configure swap appropriately
- [ ] Add cooling (fan/heatsink)
- [ ] Use hardware-specific config file

**Recommended:**
- [ ] Disable transparent huge pages
- [ ] Increase file descriptor limits
- [ ] Optimize filesystem mount options
- [ ] Set I/O scheduler to 'none' for SSD
- [ ] Disable unnecessary system services

**Advanced:**
- [ ] Pin CPU affinity
- [ ] Use jemalloc allocator
- [ ] Configure OOM protection
- [ ] Tune TCP/IP stack
- [ ] Setup monitoring

## Configuration Comparison

| Setting | RPi 3 (2GB) | RPi 4 (4GB) | RPi 5 (8GB) |
|---------|-------------|-------------|-------------|
| Memtable | 64 MB | 128 MB | 256 MB |
| Block Cache | 256 MB | 512 MB | 1024 MB |
| Worker Threads | 4 | 4 | 4 |
| Max Connections | 50 | 100 | 200 |
| HNSW M | 12 | 16 | 20 |
| Max Vectors | 50k | 100k | 200k |
| CDC | Disabled | Optional | Enabled |
| Tracing | Disabled | Disabled | Enabled |

## Troubleshooting

### High Memory Usage

1. Reduce memtable and block cache sizes
2. Limit max connections
3. Disable semantic cache
4. Reduce HNSW parameters

### High CPU Usage

1. Reduce worker threads
2. Lower HNSW ef_construction
3. Disable tracing
4. Use LZ4 compression only

### Slow Performance

1. Check temperature (throttling?)
2. Use SSD instead of SD card
3. Increase block cache
4. Optimize queries

### Database Crashes

1. Check memory limits
2. Review logs: `journalctl -u themisdb -n 100`
3. Reduce resource usage
4. Check disk space

## Additional Resources

- [ARM Build Guide](ARM_RASPBERRY_PI_BUILD.md)
- [ARM Benchmarks](ARM_BENCHMARKS.md)
- [ARM Packages](ARM_PACKAGES.md)
- [RocksDB Tuning Guide](https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide)

## Community Configurations

Share your optimized configurations:
- Open an issue with your setup and results
- Contribute platform-specific configs
- Report performance improvements

## Support

For performance issues specific to Raspberry Pi:
1. Run benchmarks: `./scripts/run-arm-benchmarks.sh`
2. Collect system info: `uname -a && free -h && vcgencmd measure_temp`
3. Check logs: `journalctl -u themisdb -n 100`
4. Open an issue with benchmark results and config
