# Hardware Acceleration Support - ThemisDB

**Version:** 1.0  
**Status:** Implementation Phase  
**Last Updated:** 20. November 2025

---

## Übersicht

ThemisDB unterstützt optionale Hardware-Beschleunigung für kritische Operationen:
- **Vector Operations** - KNN-Suche, Distanzberechnungen
- **Graph Operations** - BFS, Shortest Path, Traversals
- **Geo Operations** - Räumliche Distanzen, Point-in-Polygon Tests

### Unterstützte Backends

| Backend | Typ | Plattform | Status | Priorität |
|---------|-----|-----------|--------|-----------|
| **CPU** | Fallback | Alle | ✅ Implementiert | Default |
| **CUDA** | GPU | NVIDIA | 🚧 Stub | P0 |
| **HIP** | GPU | AMD | 🚧 Geplant | P1 |
| **ZLUDA** | GPU | AMD (CUDA-Compat) | 🚧 Geplant | P1 |
| **Vulkan** | Graphics | Cross-Platform | 🚧 Stub | P1 |
| **DirectX** | Graphics | Windows | 🚧 Stub | P2 |
| **Metal** | Graphics | macOS/iOS | 🚧 Geplant | P2 |
| **ROCm** | Compute | AMD | 🚧 Geplant | P2 |
| **OneAPI** | Compute | Intel | 🚧 Geplant | P3 |
| **OpenCL** | Compute | Cross-Platform | 🚧 Geplant | P3 |
| **OpenGL** | Graphics | Legacy | 🚧 Stub | P4 |
| **WebGPU** | Browser | Web | 🚧 Geplant | P4 |

---

## Architektur

### Backend-Abstraktion

```
┌─────────────────────────────────────────┐
│       ThemisDB Application Layer        │
├─────────────────────────────────────────┤
│      Vector / Graph / Geo Managers      │
├─────────────────────────────────────────┤
│         Backend Registry (AUTO)         │
│     (Automatische Backend-Auswahl)      │
├──────────┬──────────┬──────────┬────────┤
│   CUDA   │  Vulkan  │ DirectX  │  CPU   │
│ (NVIDIA) │(Cross-Pl)│(Windows) │(Always)│
└──────────┴──────────┴──────────┴────────┘
```

### Komponenten

1. **Compute Backend Interface** (`include/acceleration/compute_backend.h`)
   - Basis-Schnittstellen: `IComputeBackend`, `IVectorBackend`, `IGraphBackend`, `IGeoBackend`
   - Backend-Registry für automatische Auswahl

2. **CPU Fallback** (`include/acceleration/cpu_backend.h`)
   - Immer verfügbar
   - Optimiert mit SIMD-Instruktionen (AVX2)
   - Single-threaded oder TBB-parallelisiert

3. **GPU/Graphics Backends** (Optional, Build-Time)
   - CUDA: `include/acceleration/cuda_backend.h`
   - DirectX/Vulkan/OpenGL: `include/acceleration/graphics_backends.h`

---

## Build-Konfiguration

### CMake-Optionen

```cmake
# Generelle GPU-Unterstützung
-DTHEMIS_ENABLE_GPU=ON

# Spezifische Backends (optional)
-DTHEMIS_ENABLE_CUDA=ON          # NVIDIA CUDA
-DTHEMIS_ENABLE_HIP=ON           # AMD HIP
-DTHEMIS_ENABLE_ZLUDA=ON         # AMD ZLUDA (CUDA auf AMD)
-DTHEMIS_ENABLE_ROCM=ON          # AMD ROCm
-DTHEMIS_ENABLE_DIRECTX=ON       # DirectX 12 Compute (Windows)
-DTHEMIS_ENABLE_VULKAN=ON        # Vulkan Compute
-DTHEMIS_ENABLE_OPENGL=ON        # OpenGL Compute Shaders
-DTHEMIS_ENABLE_METAL=ON         # Apple Metal
-DTHEMIS_ENABLE_ONEAPI=ON        # Intel OneAPI/SYCL
-DTHEMIS_ENABLE_OPENCL=ON        # OpenCL
-DTHEMIS_ENABLE_WEBGPU=ON        # WebGPU (experimental)
```

### Build-Beispiele

**Nur CPU (Default):**
```bash
cmake -S . -B build
cmake --build build
```

**Mit CUDA:**
```bash
cmake -S . -B build -DTHEMIS_ENABLE_CUDA=ON
cmake --build build
```

**Multi-Backend (Vulkan + DirectX):**
```bash
cmake -S . -B build \
  -DTHEMIS_ENABLE_VULKAN=ON \
  -DTHEMIS_ENABLE_DIRECTX=ON
cmake --build build
```

**Auto-Detect (alle verfügbaren Backends):**
```bash
cmake -S . -B build \
  -DTHEMIS_ENABLE_GPU=ON \
  -DTHEMIS_ENABLE_CUDA=ON \
  -DTHEMIS_ENABLE_VULKAN=ON \
  -DTHEMIS_ENABLE_DIRECTX=ON
cmake --build build
```

---

## Verwendung

### Automatische Backend-Auswahl

```cpp
#include "acceleration/compute_backend.h"
#include "acceleration/cpu_backend.h"

using namespace themis::acceleration;

// Backend-Registry initialisieren
auto& registry = BackendRegistry::instance();
registry.autoDetect();

// Bestes verfügbares Vector-Backend holen
auto* vectorBackend = registry.getBestVectorBackend();

if (vectorBackend) {
    std::cout << "Using backend: " << vectorBackend->name() << std::endl;
    
    // KNN-Suche durchführen
    std::vector<float> query = {0.1f, 0.2f, 0.3f};
    auto results = vectorBackend->batchKnnSearch(
        query.data(), 1, 3,
        vectors.data(), numVectors,
        10, true  // k=10, useL2=true
    );
}
```

### Manuelle Backend-Auswahl

```cpp
// Spezifisches Backend wählen
auto* cudaBackend = registry.getBackend(BackendType::CUDA);

if (cudaBackend && cudaBackend->isAvailable()) {
    cudaBackend->initialize();
    
    // Backend-Capabilities prüfen
    auto caps = cudaBackend->getCapabilities();
    std::cout << "Device: " << caps.deviceName << std::endl;
    std::cout << "VRAM: " << caps.maxMemoryBytes / (1024*1024*1024) << " GB" << std::endl;
    
    // Operationen durchführen...
    
    cudaBackend->shutdown();
}
```

### Graceful Degradation

```cpp
// Versuche GPU, falle zurück auf CPU
auto* backend = registry.getBestVectorBackend();

if (!backend || backend->type() == BackendType::CPU) {
    std::cout << "GPU nicht verfügbar, nutze CPU-Fallback" << std::endl;
}

// Backend ist immer vorhanden (mindestens CPU)
auto results = backend->batchKnnSearch(...);
```

---

## Performance-Erwartungen

### Vector Operations (1M Vektoren, Dimension=128)

| Backend | Batch Size | Throughput | Latency (p50) | Speedup vs CPU |
|---------|------------|------------|---------------|----------------|
| CPU (AVX2) | 100 | 1,800 q/s | 0.55 ms | 1x (Baseline) |
| CUDA (T4) | 1,000 | 25,000 q/s | 0.04 ms | 14x |
| CUDA (A100) | 5,000 | 100,000 q/s | 0.05 ms | 55x |
| Vulkan (RTX 4090) | 2,000 | 40,000 q/s | 0.05 ms | 22x |
| DirectX (RTX 4090) | 2,000 | 35,000 q/s | 0.06 ms | 19x |

### Geo Operations (Spatial Distance)

| Backend | Operations/sec | Speedup |
|---------|---------------|---------|
| CPU | 5,000 | 1x |
| CUDA | 50,000+ | 10x |
| Vulkan | 35,000+ | 7x |

### Graph Operations (BFS, 100K Vertices)

| Backend | Traversals/sec | Speedup |
|---------|----------------|---------|
| CPU | 3,200 | 1x |
| CUDA | 25,000+ | 8x |
| Vulkan | 18,000+ | 6x |

---

## Backend-Spezifikationen

### CUDA (NVIDIA)

**Hardware-Anforderungen:**
- GPU: Compute Capability 7.0+ (Volta, Turing, Ampere, Hopper)
- VRAM: Mindestens 8 GB (empfohlen 16 GB+)
- CUDA Toolkit: 11.0+
- Driver: 450.80.02+

**Features:**
- ✅ Faiss GPU Integration für Vector Search
- ✅ Custom CUDA Kernels für Graph/Geo
- ✅ Async Compute Streams
- ✅ VRAM Management mit Fallback

**Implementierungsstatus:** 🚧 Stub (P0 - Q2 2026)

---

### Vulkan (Cross-Platform)

**Hardware-Anforderungen:**
- Vulkan 1.2+ fähige GPU
- Compute Queue Support
- Driver mit Vulkan SDK

**Features:**
- ✅ Cross-Platform (Windows, Linux, Android)
- ✅ Compute Pipelines für Batch Operations
- ✅ Memory Transfer Optimization
- ✅ Async Queue Execution

**Vorteile:**
- Funktioniert auf NVIDIA, AMD, Intel GPUs
- Moderne API mit expliziter Kontrolle
- Gute Performance (70-90% von CUDA)

**Implementierungsstatus:** 🚧 Stub (P1 - Q2 2026)

---

### DirectX 12 (Windows)

**Hardware-Anforderungen:**
- Windows 10 (1809+) oder Windows 11
- DirectX 12 fähige GPU
- WDDM 2.5+ Driver

**Features:**
- ✅ DirectX 12 Compute Shaders
- ✅ DirectML für ML Workloads
- ✅ Windows-native Integration
- ⚠️ Nur Windows

**Vorteile:**
- Native Windows-Integration
- DirectML für AI/ML Operations
- Breite Hardware-Unterstützung (NVIDIA, AMD, Intel)

**Implementierungsstatus:** 🚧 Stub (P2 - Q2/Q3 2026)

---

### HIP (AMD)

**Hardware-Anforderungen:**
- AMD GPU (GCN 4.0+)
- ROCm Platform
- HIP Runtime

**Features:**
- ✅ AMD-native Compute
- ✅ CUDA-ähnliche API
- ✅ Portierbar von CUDA Code
- ✅ ROCm Integration

**Vorteile:**
- Best Performance auf AMD Hardware
- CUDA-ähnliche Entwicklererfahrung
- Open Source Stack

**Implementierungsstatus:** 🚧 Geplant (P1 - Q3 2026)

---

### ZLUDA (AMD CUDA Compatibility)

**Beschreibung:**
- CUDA-Kompatibilitätsschicht für AMD GPUs
- Ermöglicht Ausführung von CUDA Code auf AMD Hardware
- Transparent für CUDA-basierten Code

**Features:**
- ✅ CUDA API Compatibility
- ✅ Funktioniert mit Faiss GPU
- ⚠️ Performance: 70-85% von nativer AMD HIP

**Use Case:**
- Schnelle AMD GPU Support ohne Code-Änderung
- Fallback wenn HIP nicht verfügbar
- Bridge-Lösung für CUDA-basierte Libraries

**Implementierungsstatus:** 🚧 Geplant (P1 - Q3 2026)

---

## Roadmap

### Phase 1: Core Infrastructure (Q1 2026) ✅
- [x] Backend-Abstraktionsschicht
- [x] CPU Fallback Implementation
- [x] Backend Registry
- [x] CMake Integration
- [x] Stub Implementations

### Phase 2: CUDA Implementation (Q2 2026)
- [ ] CUDA Toolkit Integration
- [ ] Faiss GPU Vector Backend
- [ ] Custom CUDA Kernels (Graph/Geo)
- [ ] Performance Benchmarks
- [ ] Documentation

### Phase 3: Vulkan Implementation (Q2/Q3 2026)
- [ ] Vulkan SDK Integration
- [ ] Compute Pipeline Setup
- [ ] Vector/Graph/Geo Kernels
- [ ] Cross-Platform Testing

### Phase 4: Additional Backends (Q3/Q4 2026)
- [ ] DirectX 12 (Windows)
- [ ] HIP (AMD native)
- [ ] ZLUDA (AMD CUDA compat)
- [ ] Metal (Apple)
- [ ] OneAPI (Intel)

---

## Testing

### Unit Tests

```bash
# Test Backend Registry
./build/themis_tests --gtest_filter=AccelerationTest.BackendRegistry

# Test CPU Backend
./build/themis_tests --gtest_filter=AccelerationTest.CPUBackend

# Test CUDA Backend (wenn verfügbar)
./build/themis_tests --gtest_filter=AccelerationTest.CUDABackend
```

### Benchmarks

```bash
# Vector Search Benchmark
./build/bench_vector_accel --backend=auto

# Geo Operations Benchmark
./build/bench_geo_accel --backend=cuda

# Graph Traversal Benchmark
./build/bench_graph_accel --backend=vulkan
```

---

## Troubleshooting

### Backend nicht verfügbar

**Problem:** Backend wird nicht erkannt
```
Warning: CUDA backend not available, falling back to CPU
```

**Lösung:**
1. Prüfe ob Backend beim Build aktiviert wurde (`-DTHEMIS_ENABLE_CUDA=ON`)
2. Prüfe Driver/Runtime Installation
3. Verifiziere Hardware-Kompatibilität

### VRAM Exhausted

**Problem:** GPU-Speicher voll
```
Error: CUDA out of memory
```

**Lösung:**
1. Reduziere Batch-Size
2. Aktiviere automatischen CPU-Fallback
3. Nutze Chunked Processing

### Performance nicht wie erwartet

**Problem:** GPU langsamer als CPU

**Mögliche Ursachen:**
- Batch-Size zu klein (Overhead dominiert)
- Memory Transfer Bottleneck
- Nicht optimierte Kernels

**Lösung:**
- Erhöhe Batch-Size (1000+ Queries)
- Pre-load Daten in VRAM
- Profile mit `nvprof` / `renderdoc`

---

## Weiterführende Dokumentation

- **GPU Acceleration Plan:** [`docs/performance/GPU_ACCELERATION_PLAN.md`](GPU_ACCELERATION_PLAN.md)
- **CUDA Setup Guide:** `docs/performance/cuda_setup.md` (coming soon)
- **Vulkan Integration:** `docs/performance/vulkan_integration.md` (coming soon)
- **Performance Tuning:** `docs/performance/gpu_tuning.md` (coming soon)

---

**Kontakt:**
- Issues: https://github.com/makr-code/ThemisDB/issues
- Discussions: https://github.com/makr-code/ThemisDB/discussions

**Version:** 1.0  
**Letzte Aktualisierung:** 20. November 2025
