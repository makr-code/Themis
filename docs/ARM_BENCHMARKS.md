# ARM-Specific Benchmark Suite

This document describes the ARM-specific benchmark suite for ThemisDB, designed to measure and optimize performance on ARM architectures including Raspberry Pi.

## Overview

The ARM benchmark suite consists of specialized benchmarks that test:

1. **SIMD Performance** - ARM NEON vs scalar implementations
2. **Memory Access Patterns** - Cache-friendly vs cache-unfriendly access
3. **Vector Operations** - Distance calculations and dot products

These benchmarks help identify performance characteristics and optimization opportunities on ARM platforms.

## Benchmark Programs

### 1. `bench_arm_simd` - SIMD Performance Testing

**Purpose:** Measures ARM NEON SIMD performance vs scalar implementations.

**Tests:**
- **L2 Distance** - Euclidean distance calculation (with sqrt)
- **L2 Distance Squared** - Faster variant without sqrt
- **Dot Product** - For cosine similarity calculations
- **Batch Processing** - Simulates vector search workload

**Dimensions Tested:**
- 64, 192, 320, 448, 576, 704, 832, 960, 1088, 1216, 1344, 1472 (common embedding sizes)

**Key Metrics:**
- `ops_per_sec` - Operations per second
- `dimension` - Vector dimensionality
- `vectors_per_sec` - Throughput for batch operations

**Expected Results (ARM64 with NEON):**
- 2-4x speedup over scalar for L2 distance
- Additional 5-10% improvement with FMA (ARMv8.2+)
- Higher speedup for larger dimensions

**Example Output:**
```
BM_ARM_L2_Distance_SIMD/64        125 us      2.3M ops/s [ARM_NEON]
BM_ARM_L2_Distance_Scalar/64      450 us      0.6M ops/s [Scalar_Reference]
Speedup: 3.6x
```

### 2. `bench_arm_memory` - Memory Access Pattern Testing

**Purpose:** Measures cache efficiency and memory bandwidth on ARM platforms.

**Tests:**
- **Sequential Read/Write** - Optimal cache-friendly access
- **Random Read/Write** - Worst-case cache-unfriendly access
- **Strided Access** - Various stride patterns
- **Memory Copy** - Bandwidth testing (memcpy vs loop)
- **Cache Line Alignment** - Effects of 64-byte alignment

**Memory Sizes Tested:**
- 16 KB (L1 cache)
- 128 KB (L2 cache)
- 1 MB (L2 boundary)
- 4 MB (RAM)

**Key Metrics:**
- `bandwidth_mb_s` - Memory bandwidth in MB/s
- `size_kb` - Working set size
- `stride` - Access pattern stride

**Expected Results (Raspberry Pi 4):**
- Sequential: 1-3 GB/s
- Random: 100-500 MB/s (much slower due to cache misses)
- L1 hit: <10 cycles
- L2 hit: ~40 cycles
- RAM: ~100 cycles

**Cache Hierarchy (Raspberry Pi 4):**
- L1: 32 KB per core (data), 48 KB (instruction)
- L2: 1 MB shared
- Cache line: 64 bytes

### 3. `bench_simd_distance` - Existing SIMD Benchmark

**Purpose:** Cross-platform SIMD performance (works on x86_64 and ARM).

**Tests:**
- Generic SIMD vs scalar L2 distance
- Automatically uses best available SIMD (AVX2/AVX512 on x86, NEON on ARM)

## Running Benchmarks

### Quick Start

```bash
# Build with benchmarks enabled
cmake -S . -B build-arm \
    -DCMAKE_BUILD_TYPE=Release \
    -DTHEMIS_BUILD_BENCHMARKS=ON

cmake --build build-arm -j$(nproc)

# Run ARM-specific benchmarks
./scripts/run-arm-benchmarks.sh
```

### Individual Benchmarks

```bash
# Run specific benchmark
./build-arm/bench_arm_simd

# Run with filters
./build-arm/bench_arm_simd --benchmark_filter=SIMD

# Save results to file
./build-arm/bench_arm_simd --benchmark_out=results.json \
    --benchmark_out_format=json
```

### Benchmark Options

**Common Google Benchmark flags:**

```bash
# Run fewer iterations (faster, less accurate)
--benchmark_min_time=0.5s

# Filter benchmarks by name
--benchmark_filter=L2_Distance

# Output format
--benchmark_format=console|json|csv

# Repetitions for statistical analysis
--benchmark_repetitions=5

# Display aggregate statistics
--benchmark_report_aggregates_only=true
```

## Performance Tuning

### Raspberry Pi 4/5 Optimization

**CPU Governor:**
```bash
# Set performance mode
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Verify
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

**Thermal Monitoring:**
```bash
# Monitor CPU temperature during benchmarks
watch -n 1 'vcgencmd measure_temp'

# Ensure proper cooling to avoid throttling
```

**Memory Configuration:**
```bash
# Check swap usage (disable for consistent results)
sudo swapoff -a

# Re-enable after benchmarks
sudo swapon -a
```

### ARM64 Compiler Optimizations

**CMake Flags for Maximum Performance:**
```bash
cmake -S . -B build-arm64-perf \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-O3 -march=armv8-a+simd -mtune=cortex-a72" \
    -DCMAKE_CXX_FLAGS="-O3 -march=armv8-a+simd -mtune=cortex-a72" \
    -DTHEMIS_BUILD_BENCHMARKS=ON
```

**For Raspberry Pi 4 (Cortex-A72):**
```bash
-march=armv8-a+crc+simd
-mtune=cortex-a72
```

**For Raspberry Pi 5 (Cortex-A76):**
```bash
-march=armv8.2-a+fp16+rcpc+dotprod
-mtune=cortex-a76
```

## Interpreting Results

### SIMD Speedup Calculation

```
Speedup = Scalar Time / SIMD Time

Example:
  Scalar: 450 us
  SIMD:   125 us
  Speedup: 450 / 125 = 3.6x
```

**Expected Speedups:**

| Operation | ARM64 NEON | ARMv7 NEON | x86_64 AVX2 |
|-----------|------------|------------|-------------|
| L2 Distance | 2.5-4x | 2-3x | 3-6x |
| Dot Product | 3-5x | 2-4x | 4-8x |
| Batch (100 vectors) | 2-4x | 2-3x | 3-6x |

### Memory Bandwidth Analysis

**Sequential vs Random Access:**
```
Cache Efficiency = Random Bandwidth / Sequential Bandwidth

Good: > 0.5 (cache-friendly)
Fair: 0.2-0.5 (moderate cache usage)
Poor: < 0.2 (cache-unfriendly)
```

**Example:**
```
Sequential Read (16 KB): 2500 MB/s (L1 cache)
Random Read (16 KB):     1200 MB/s
Cache Efficiency: 48% (Good)

Sequential Read (4 MB):  1000 MB/s (RAM)
Random Read (4 MB):       120 MB/s
Cache Efficiency: 12% (Poor - expected for large random access)
```

## Comparing Results

### Baseline Comparison

Compare ARM performance against x86_64:

```bash
# Run on ARM
./build-arm/bench_arm_simd > results_arm64.txt

# Run same benchmark on x86_64
./build-x64/bench_arm_simd > results_x64.txt

# Compare
diff results_arm64.txt results_x64.txt
```

### Cross-Platform Metrics

**Normalized Performance (Operations per Second per GHz):**

```
Normalized Perf = (Ops/sec) / (CPU Frequency GHz)

Example:
  ARM64 @ 1.5 GHz: 2.3M ops/s → 1.53M ops/s/GHz
  x86_64 @ 3.0 GHz: 5.5M ops/s → 1.83M ops/s/GHz
  
  Efficiency ratio: ARM is 84% as efficient per clock
```

### Regression Testing

Track performance over time:

```bash
# Save results with timestamp
./scripts/run-arm-benchmarks.sh

# Compare with previous run
diff benchmark_results/arm_benchmark_20250101_120000.txt \
     benchmark_results/arm_benchmark_20250110_120000.txt
```

## Platform-Specific Notes

### Raspberry Pi 2/3 (ARMv7)

- NEON support: Check with `cat /proc/cpuinfo | grep neon`
- 32-bit: Use `armv7l` build
- Lower memory bandwidth: ~1 GB/s vs 3 GB/s on RPi 4
- Thermal throttling more aggressive

### Raspberry Pi 3 (ARM64 mode)

- Can run 64-bit OS for better performance
- NEON guaranteed in 64-bit mode
- 1 GB RAM: Reduce benchmark sizes

### Raspberry Pi 4

- Cortex-A72: ARMv8-A with NEON
- 4 cores @ 1.5 GHz (can boost to 1.8 GHz)
- Memory: 2-8 GB options
- Optimal for benchmarking

### Raspberry Pi 5

- Cortex-A76: ARMv8.2-A with additional features
- FMA instructions available
- Better memory bandwidth
- Expect 10-15% better performance than RPi 4

## Troubleshooting

### Benchmark Crashes

**Issue:** Benchmark segfaults or crashes

**Solutions:**
- Check memory limits: `ulimit -v`
- Reduce benchmark size: Modify dimension ranges
- Ensure proper alignment: Check for alignment issues

### Inconsistent Results

**Issue:** Results vary significantly between runs

**Causes:**
- CPU throttling due to temperature
- Background processes consuming CPU
- Non-deterministic cache state

**Solutions:**
```bash
# Disable turbo boost for consistency
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost

# Run with higher priority
sudo nice -n -20 ./build/bench_arm_simd

# Multiple runs for statistical significance
./build/bench_arm_simd --benchmark_repetitions=10
```

### NEON Not Detected

**Issue:** Benchmarks show "Scalar" instead of "ARM_NEON"

**Check:**
```bash
# Verify NEON support
cat /proc/cpuinfo | grep -i neon

# Check compiler flags
grep -r "ARM_NEON\|armv8-a" build/CMakeCache.txt
```

**Fix:**
- Ensure CMake detected ARM correctly
- Rebuild with `-march=armv8-a` or `-march=armv7-a+neon`

## CI/CD Integration

The ARM benchmark suite integrates with GitHub Actions:

```yaml
# Run benchmarks on ARM hardware (if available)
- name: Run ARM Benchmarks
  if: runner.arch == 'ARM64'
  run: |
    ./scripts/run-arm-benchmarks.sh
    
- name: Upload Results
  uses: actions/upload-artifact@v4
  with:
    name: arm-benchmark-results
    path: benchmark_results/
```

For self-hosted ARM runners, see CI/CD documentation.

## References

- [Google Benchmark Documentation](https://github.com/google/benchmark)
- [ARM NEON Optimization Guide](https://developer.arm.com/documentation/102159/latest/)
- [Raspberry Pi Performance Tuning](https://www.raspberrypi.com/documentation/computers/config_txt.html)
- [ThemisDB CI/CD Guide](CI_CD_MULTIARCH.md)

## Contributing

To add new ARM-specific benchmarks:

1. Create `benchmarks/bench_arm_<feature>.cpp`
2. Follow existing patterns (use Google Benchmark)
3. Test on actual ARM hardware
4. Document expected results
5. Update this documentation

## Support

For benchmark-related issues:
- Check existing results in `benchmark_results/`
- Review [ARM Build Guide](ARM_RASPBERRY_PI_BUILD.md)
- Open issue with benchmark output and system info
