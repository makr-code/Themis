# Building ThemisDB on ARM and Raspberry Pi

This guide explains how to build and run ThemisDB on ARM-based systems, including Raspberry Pi.

## Quick Start: Pre-built Packages

**Prefer pre-built packages?** See **[ARM Packages Guide](ARM_PACKAGES.md)** for ready-to-install DEB, RPM, and Arch packages for ARM64 and ARMv7.

**Debian/Ubuntu/Raspberry Pi OS:**
```bash
wget https://github.com/makr-code/ThemisDB/releases/latest/download/themisdb_1.0.0-1_arm64.deb
sudo apt install ./themisdb_1.0.0-1_arm64.deb
sudo systemctl start themisdb
```

For building from source, continue reading below.

## Architecture Support

ThemisDB supports the following ARM architectures:

- **ARM64/AArch64** (64-bit):
  - Raspberry Pi 3 (64-bit OS)
  - Raspberry Pi 4
  - Raspberry Pi 5
  - Other ARM64 boards (NVIDIA Jetson, AWS Graviton, etc.)
  
- **ARMv7** (32-bit):
  - Raspberry Pi 2
  - Raspberry Pi 3 (32-bit OS)
  - Other ARMv7 boards

## Performance Optimizations

ThemisDB includes ARM-specific optimizations:

### NEON SIMD Support

The vector distance calculations use ARM NEON intrinsics for high performance:
- **ARM64/AArch64**: NEON is standard and always available
- **ARMv7**: NEON is available on most modern ARMv7 processors (including all Raspberry Pi models)

The build system automatically detects your architecture and enables appropriate optimizations:
- **x86_64**: AVX2/AVX512 or native optimizations
- **ARM64**: `-march=armv8-a` with NEON
- **ARMv7**: `-march=armv7-a -mfpu=neon -mfloat-abi=hard`

## Prerequisites

### Raspberry Pi OS (Recommended)

1. **Update your system:**
   ```bash
   sudo apt update
   sudo apt upgrade
   ```

2. **Install build tools:**
   ```bash
   sudo apt install -y \
       build-essential cmake ninja-build git \
       curl zip unzip tar pkg-config \
       python3 perl nasm autoconf automake libtool
   ```

3. **Install vcpkg:**
   ```bash
   cd ~
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg
   ./bootstrap-vcpkg.sh -disableMetrics
   export VCPKG_ROOT=~/vcpkg
   echo "export VCPKG_ROOT=~/vcpkg" >> ~/.bashrc
   ```

### Memory Requirements

- **Minimum**: 2GB RAM (Raspberry Pi 3/4 with 2GB)
- **Recommended**: 4GB+ RAM (Raspberry Pi 4/5 with 4GB or 8GB)
- **Swap**: Consider adding swap space for compilation:
  ```bash
  sudo dphys-swapfile swapoff
  sudo nano /etc/dphys-swapfile
  # Set CONF_SWAPSIZE=2048
  sudo dphys-swapfile setup
  sudo dphys-swapfile swapon
  ```

## Building on Raspberry Pi

### Quick Start

1. **Clone the repository:**
   ```bash
   git clone https://github.com/makr-code/ThemisDB.git
   cd ThemisDB
   ```

2. **Run setup script:**
   ```bash
   ./setup.sh
   ```

3. **Build using the appropriate preset:**

   For Raspberry Pi 3/4/5 (64-bit OS):
   ```bash
   cmake --preset rpi-arm64-gcc-release
   cmake --build --preset rpi-arm64-gcc-release -j$(nproc)
   ```

   For Raspberry Pi 2/3 (32-bit OS):
   ```bash
   cmake --preset rpi-armv7-gcc-release
   cmake --build --preset rpi-armv7-gcc-release -j$(nproc)
   ```

### Manual Build (Without Presets)

If you prefer to build manually:

```bash
# Export vcpkg root
export VCPKG_ROOT=~/vcpkg

# Configure
cmake -S . -B build-arm \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DTHEMIS_BUILD_TESTS=ON \
    -DTHEMIS_BUILD_BENCHMARKS=OFF \
    -DTHEMIS_ENABLE_TRACING=ON

# Build (use fewer jobs on low-memory systems)
cmake --build build-arm --config Release -j2
```

### Build Options

Control the build with these CMake options:

- `-DTHEMIS_BUILD_TESTS=ON/OFF` - Build unit tests (default: ON)
- `-DTHEMIS_BUILD_BENCHMARKS=ON/OFF` - Build benchmarks (default: OFF)
- `-DTHEMIS_ENABLE_TRACING=ON/OFF` - Enable OpenTelemetry tracing (default: ON)
- `-DTHEMIS_ENABLE_GPU=OFF` - GPU support (not recommended for Raspberry Pi)
- `-DTHEMIS_ENABLE_ASAN=OFF` - AddressSanitizer for debugging

## Running ThemisDB

After building, start the server:

```bash
# ARM64 build
./build-rpi-arm64-release/themis_server --config config.yaml

# ARMv7 build
./build-rpi-armv7-release/themis_server --config config.yaml
```

Or use the build script output directory:

```bash
./build-arm/themis_server --config config.yaml
```

## Cross-Compilation

You can cross-compile for ARM on an x86_64 host for faster builds:

### Using Docker (Recommended)

Build multi-architecture Docker images:

```bash
# Enable Docker BuildKit
export DOCKER_BUILDKIT=1

# Build for ARM64
docker buildx build \
    --platform linux/arm64 \
    --build-arg VCPKG_TRIPLET=arm64-linux \
    -t themisdb:arm64 \
    -f Dockerfile .

# Build for ARMv7
docker buildx build \
    --platform linux/arm/v7 \
    --build-arg VCPKG_TRIPLET=arm-linux \
    -t themisdb:armv7 \
    -f Dockerfile .
```

### Using Cross-Compiler Toolchain

Install cross-compilation tools on Ubuntu/Debian:

```bash
# For ARM64
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# For ARMv7
sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

Create a CMake toolchain file `arm64-toolchain.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

Build with the toolchain:

```bash
cmake -S . -B build-cross-arm64 \
    -DCMAKE_TOOLCHAIN_FILE=arm64-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DTHEMIS_BUILD_TESTS=OFF

cmake --build build-cross-arm64 -j$(nproc)
```

## Performance Tuning

### Raspberry Pi 4/5 Configuration

Edit `config.yaml` for Raspberry Pi:

```yaml
storage:
  rocksdb_path: ./data/rocksdb
  memtable_size_mb: 128      # Reduced from 256 for 2GB RAM
  block_cache_size_mb: 512   # Reduced from 1024 for 2GB RAM
  enable_blobdb: true
  compression:
    default: lz4             # Fast compression
    bottommost: zstd         # Better compression for older data

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 4          # Match CPU cores

vector_index:
  engine: hnsw
  hnsw_m: 16
  hnsw_ef_construction: 200
  use_gpu: false             # No GPU on Raspberry Pi
```

### Raspberry Pi 3 Configuration (2GB RAM)

For lower memory systems:

```yaml
storage:
  rocksdb_path: ./data/rocksdb
  memtable_size_mb: 64       # Further reduced
  block_cache_size_mb: 256   # Further reduced
  enable_blobdb: true
  compression:
    default: lz4
    bottommost: zstd

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 4

vector_index:
  engine: hnsw
  hnsw_m: 12                 # Reduced graph connectivity
  hnsw_ef_construction: 100  # Faster indexing
  use_gpu: false
```

## Troubleshooting

### Build Issues

**Out of memory during compilation:**
```bash
# Use fewer parallel jobs
cmake --build build-arm -j2

# Or build single-threaded
cmake --build build-arm -j1
```

**vcpkg package installation fails:**
```bash
# Some packages may not have ARM binaries and need to be built from source
# This is automatic but can take time. Be patient.
```

**NEON intrinsics not found (ARMv7):**
```bash
# Ensure NEON is enabled in compiler flags
# The CMakeLists.txt should handle this automatically
# Verify with:
grep -i neon build-arm/CMakeCache.txt
```

### Runtime Issues

**Server won't start:**
```bash
# Check available memory
free -h

# Check disk space
df -h

# Check for port conflicts
sudo netstat -tulpn | grep 8765
```

**Poor performance:**
- Reduce worker threads in config.yaml
- Reduce memory cache sizes
- Enable swap space
- Use faster storage (SSD via USB 3.0 on RPi 4/5)

## Benchmarks

Expected performance on Raspberry Pi 4 (4GB, 64-bit OS):

| Operation | Throughput | Notes |
|-----------|------------|-------|
| Entity PUT | ~5,000 ops/s | With NEON optimizations |
| Entity GET | ~15,000 ops/s | From cache |
| Indexed Query | ~1,200 queries/s | Single predicate |
| Graph Traverse (depth=3) | ~450 ops/s | BFS algorithm |
| Vector ANN (k=10) | ~250 queries/s | HNSW with NEON |

Performance scales with:
- Number of CPU cores
- Available RAM
- Storage speed (SSD vs SD card)
- Workload characteristics

## Performance Benchmarking

ThemisDB includes ARM-specific benchmarks to measure and optimize performance:

**Run benchmarks:**
```bash
# Build with benchmarks enabled
cmake --preset rpi-arm64-gcc-release -DTHEMIS_BUILD_BENCHMARKS=ON
cmake --build --preset rpi-arm64-gcc-release

# Run ARM benchmarks
./scripts/run-arm-benchmarks.sh
```

**Benchmark Suite:**
- `bench_arm_simd` - SIMD performance (NEON vs scalar)
- `bench_arm_memory` - Memory access patterns and cache efficiency
- `bench_simd_distance` - Cross-platform SIMD testing

**Expected Results (Raspberry Pi 4):**
- SIMD speedup: 2-4x over scalar code
- L1 cache: ~2.5 GB/s bandwidth
- L2 cache: ~1.5 GB/s bandwidth
- Sequential RAM: ~1 GB/s

See [ARM Benchmarks Guide](ARM_BENCHMARKS.md) for detailed documentation.

## Pre-built Packages

Pre-built packages are available for easier installation:

**Available Formats:**
- DEB packages (Debian, Ubuntu, Raspberry Pi OS) - ARM64 & ARMv7
- RPM packages (Fedora, RHEL, Rocky Linux) - ARM64
- Arch Linux packages - ARM64

**Installation:**
```bash
# Debian/Ubuntu/Raspberry Pi OS (ARM64)
wget https://github.com/makr-code/ThemisDB/releases/latest/download/themisdb_1.0.0-1_arm64.deb
sudo apt install ./themisdb_1.0.0-1_arm64.deb

# Raspberry Pi OS (ARMv7 32-bit)
wget https://github.com/makr-code/ThemisDB/releases/latest/download/themisdb_1.0.0-1_armhf.deb
sudo apt install ./themisdb_1.0.0-1_armhf.deb

# Fedora/RHEL (ARM64)
wget https://github.com/makr-code/ThemisDB/releases/latest/download/themisdb-1.0.0-1.fc39.aarch64.rpm
sudo dnf install themisdb-1.0.0-1.fc39.aarch64.rpm
```

See [ARM Packages Guide](ARM_PACKAGES.md) for complete installation and configuration instructions.

**Building packages locally:**
```bash
# Build DEB package for ARM64
./scripts/build-arm-packages.sh

# Build with custom version
VERSION=1.1.0 ./scripts/build-arm-packages.sh

# Build RPM for ARM64
FORMAT=rpm ARCH=arm64 ./scripts/build-arm-packages.sh
```

## Additional Resources

- [Main README](../README.md) - General build instructions
- [ARM Packages Guide](ARM_PACKAGES.md) - Pre-built package installation
- [ARM Benchmarks Guide](ARM_BENCHMARKS.md) - Performance testing and optimization
- [Deployment Guide](deployment.md) - Production setup
- [Memory Tuning](memory_tuning.md) - Performance optimization
- [Architecture Overview](architecture.md) - System design
- [CI/CD for Multi-Architecture](CI_CD_MULTIARCH.md) - Automated builds and testing

## Continuous Integration

ThemisDB includes automated CI/CD for ARM builds:

- **GitHub Actions workflows** test ARM64 and ARMv7 builds on every commit
- **Docker multi-arch images** automatically built and published
- **Cross-compilation tests** ensure ARM compatibility
- **Benchmark tracking** (planned) will monitor performance regression
- See [CI/CD documentation](CI_CD_MULTIARCH.md) for details

Build status: [![ARM Build](https://github.com/makr-code/ThemisDB/actions/workflows/arm-build.yml/badge.svg)](https://github.com/makr-code/ThemisDB/actions/workflows/arm-build.yml)

## vcpkg ARM Support

All ThemisDB dependencies are available for ARM through vcpkg:

- **Core**: RocksDB, simdjson, TBB, Arrow, Boost
- **Networking**: OpenSSL, CURL
- **Vector Search**: HNSWlib (CPU-only)
- **Testing**: Google Test, Google Benchmark

Note: Faiss GPU support is not available on Raspberry Pi. Use CPU-based HNSW for vector search.

## Contributing

If you encounter ARM-specific issues or have performance improvements, please:

1. Check existing issues on GitHub
2. Test on actual ARM hardware when possible
3. Include architecture details (CPU model, RAM, OS) in bug reports
4. Submit PRs with ARM-specific optimizations

## License

Same as main ThemisDB project (MIT License).
