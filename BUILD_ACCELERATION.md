# Building ThemisDB with Hardware Acceleration

## Quick Start

### CPU Only (Default)
```bash
cmake -S . -B build
cmake --build build
```

### CUDA Acceleration (NVIDIA GPUs)
```bash
# Prerequisites: CUDA Toolkit 11.0+
cmake -S . -B build -DTHEMIS_ENABLE_CUDA=ON
cmake --build build
```

### Vulkan Acceleration (Cross-Platform)
```bash
# Prerequisites: Vulkan SDK
cmake -S . -B build -DTHEMIS_ENABLE_VULKAN=ON
cmake --build build
```

### Multi-Backend
```bash
cmake -S . -B build \
  -DTHEMIS_ENABLE_CUDA=ON \
  -DTHEMIS_ENABLE_VULKAN=ON
cmake --build build
```

---

## Prerequisites

### CUDA Backend

**Install CUDA Toolkit:**
- **Linux:** https://developer.nvidia.com/cuda-downloads
  ```bash
  wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
  sudo dpkg -i cuda-keyring_1.1-1_all.deb
  sudo apt-get update
  sudo apt-get install cuda-toolkit-12-3
  ```

- **Windows:** Download installer from NVIDIA
  ```
  https://developer.nvidia.com/cuda-downloads
  ```

**Verify Installation:**
```bash
nvcc --version
nvidia-smi
```

**Hardware Requirements:**
- NVIDIA GPU with Compute Capability 7.0+
- 8GB+ VRAM recommended
- CUDA Driver 450.80.02+

### Vulkan Backend

**Install Vulkan SDK:**
- **Linux:**
  ```bash
  wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
  sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-jammy.list https://packages.lunarg.com/vulkan/lunarg-vulkan-jammy.list
  sudo apt update
  sudo apt install vulkan-sdk
  ```

- **Windows:** Download from LunarG
  ```
  https://vulkan.lunarg.com/sdk/home#windows
  ```

**Verify Installation:**
```bash
vulkaninfo
glslangValidator --version
```

**Hardware Requirements:**
- Vulkan 1.2+ capable GPU (NVIDIA/AMD/Intel)
- 4GB+ VRAM recommended

---

## Build Options

All hardware acceleration backends are **optional** and disabled by default.

### Available Options

```cmake
# CUDA (NVIDIA GPUs)
-DTHEMIS_ENABLE_CUDA=ON

# Vulkan (Cross-platform)
-DTHEMIS_ENABLE_VULKAN=ON

# DirectX 12 (Windows only)
-DTHEMIS_ENABLE_DIRECTX=ON

# AMD HIP (AMD GPUs, future)
-DTHEMIS_ENABLE_HIP=ON

# AMD ZLUDA (CUDA compatibility on AMD, future)
-DTHEMIS_ENABLE_ZLUDA=ON

# Other backends (planned)
-DTHEMIS_ENABLE_METAL=ON      # Apple Silicon
-DTHEMIS_ENABLE_ONEAPI=ON     # Intel GPUs
-DTHEMIS_ENABLE_OPENCL=ON     # Generic
-DTHEMIS_ENABLE_OPENGL=ON     # Legacy
```

### Advanced Options

```cmake
# Specify CUDA Toolkit location
-DCUDAToolkit_ROOT=/usr/local/cuda-12.3

# Specify Vulkan SDK location
-DVULKAN_SDK=/path/to/vulkan/1.3.xxx

# Enable multiple backends
-DTHEMIS_ENABLE_CUDA=ON -DTHEMIS_ENABLE_VULKAN=ON
```

---

## Build Examples

### Development Build (Debug + CUDA)
```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTHEMIS_ENABLE_CUDA=ON \
  -DTHEMIS_BUILD_TESTS=ON

cmake --build build
./build/themis_tests
```

### Production Build (Release + Multi-Backend)
```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DTHEMIS_ENABLE_CUDA=ON \
  -DTHEMIS_ENABLE_VULKAN=ON \
  -DTHEMIS_STRICT_BUILD=ON

cmake --build build --config Release
```

### Plugin-Only Build
```bash
# Build only the CUDA plugin as a separate DLL
cd plugins/cuda
cmake -S . -B build
cmake --build build
```

---

## Verification

### Check CUDA Backend
```bash
./build/themis_server

# Look for output:
# CUDA Backend initialized successfully:
#   Device: NVIDIA GeForce RTX 4090
#   Compute Capability: 8.9
#   Global Memory: 24 GB
#   Multiprocessors: 128
```

### Check Vulkan Backend
```bash
./build/themis_server

# Look for output:
# Vulkan Backend: Compute shaders available
# Vulkan Device: NVIDIA GeForce RTX 4090
# VRAM: 24 GB
```

### Run Tests
```bash
cd build
ctest --output-on-failure

# Or specific tests
./themis_tests --gtest_filter=AccelerationTest.*
```

---

## Troubleshooting

### CUDA Not Found
```
Error: CUDA toolkit not found
```

**Solution:**
```bash
# Set CUDA_ROOT
export CUDA_ROOT=/usr/local/cuda
cmake -S . -B build -DTHEMIS_ENABLE_CUDA=ON
```

### Vulkan Not Found
```
Error: Could NOT find Vulkan
```

**Solution:**
```bash
# Set VULKAN_SDK
export VULKAN_SDK=/path/to/vulkan/sdk
cmake -S . -B build -DTHEMIS_ENABLE_VULKAN=ON
```

### GPU Not Detected at Runtime
```
Warning: No CUDA-capable device found
```

**Check:**
1. Driver installed? `nvidia-smi`
2. GPU visible? `nvidia-smi -L`
3. CUDA initialized? `export CUDA_VISIBLE_DEVICES=0`

### Compilation Errors

**CUDA Compute Capability Mismatch:**
```
nvcc fatal: Unsupported gpu architecture 'compute_89'
```

**Solution:**
```cmake
# In CMakeLists.txt, adjust CUDA architectures
set(CMAKE_CUDA_ARCHITECTURES 75 80 86 89)  # Adjust for your GPU
```

**Vulkan Shader Compilation Failed:**
```bash
# Manually compile shaders
cd src/acceleration/vulkan/shaders
glslangValidator -V l2_distance.comp -o l2_distance.spv
glslangValidator -V cosine_distance.comp -o cosine_distance.spv
```

---

## Performance Tuning

### CUDA
```cmake
# Release build with CUDA optimizations
-DCMAKE_BUILD_TYPE=Release
-DTHEMIS_ENABLE_AVX2=ON  # CPU fallback optimization
```

**Runtime:**
```bash
# Use specific GPU
export CUDA_VISIBLE_DEVICES=0

# Limit VRAM usage
# (via config/acceleration.yaml)
cuda:
  memory_limit_gb: 16
```

### Vulkan
```bash
# Optimize shaders
spirv-opt l2_distance.spv -O -o l2_distance_opt.spv
```

---

## Docker Build

### CUDA Container
```dockerfile
FROM nvidia/cuda:12.3.0-devel-ubuntu22.04

RUN apt-get update && apt-get install -y \
    cmake build-essential git

COPY . /themis
WORKDIR /themis

RUN cmake -S . -B build -DTHEMIS_ENABLE_CUDA=ON && \
    cmake --build build

CMD ["./build/themis_server"]
```

**Run:**
```bash
docker build -t themis-cuda .
docker run --gpus all themis-cuda
```

### Vulkan Container
```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    cmake build-essential git \
    vulkan-sdk

# ... rest similar to CUDA
```

---

## Cross-Platform Notes

### Windows
- Use Visual Studio 2019+ with CUDA support
- Vulkan SDK installer handles environment variables
- DirectX 12 is native (no extra setup)

### Linux
- CUDA: Requires proprietary NVIDIA drivers
- Vulkan: Works with Mesa drivers (AMD/Intel) or proprietary (NVIDIA)
- Best for multi-backend builds

### macOS
- CUDA: Not supported (Apple deprecated CUDA)
- Vulkan: Via MoltenVK (Vulkan → Metal translation)
- Metal: Native, best performance on Apple Silicon

---

**See Also:**
- [CUDA Backend Documentation](docs/performance/CUDA_BACKEND.md)
- [Vulkan Backend Documentation](docs/performance/VULKAN_BACKEND.md)
- [Hardware Acceleration Guide](docs/performance/HARDWARE_ACCELERATION.md)

**Last Updated:** 20. November 2025  
**Version:** 1.0
