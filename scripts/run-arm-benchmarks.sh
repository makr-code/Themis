#!/usr/bin/env bash
# ARM-Specific Benchmark Runner
# Runs ARM/NEON benchmarks and generates performance report

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="${BUILD_DIR:-build}"
BENCHMARK_DIR="${BENCHMARK_DIR:-benchmark_results}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${BENCHMARK_DIR}/arm_benchmark_${TIMESTAMP}.txt"

echo -e "${BLUE}=== ThemisDB ARM Benchmark Runner ===${NC}"
echo ""

# Detect architecture
ARCH=$(uname -m)
echo -e "${GREEN}Detected architecture: ${ARCH}${NC}"

case "$ARCH" in
    aarch64|arm64)
        echo "  -> ARM64/AArch64 platform (NEON supported)"
        EXPECTED_SIMD="NEON"
        ;;
    armv7l|armv7)
        echo "  -> ARMv7 platform (NEON may be supported)"
        EXPECTED_SIMD="NEON (if available)"
        ;;
    x86_64|amd64)
        echo "  -> x86_64 platform (AVX2/AVX512)"
        EXPECTED_SIMD="AVX2 or AVX512"
        ;;
    *)
        echo -e "${YELLOW}  -> Unknown architecture, running generic benchmarks${NC}"
        EXPECTED_SIMD="Unknown"
        ;;
esac

echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
    echo "Please build the project first or set BUILD_DIR environment variable"
    exit 1
fi

# Create benchmark results directory
mkdir -p "$BENCHMARK_DIR"

# Find ARM-specific benchmarks
ARM_BENCHMARKS=(
    "bench_arm_simd"
    "bench_arm_memory"
    "bench_simd_distance"
)

FOUND_BENCHMARKS=()
for bench in "${ARM_BENCHMARKS[@]}"; do
    if [ -f "$BUILD_DIR/$bench" ]; then
        FOUND_BENCHMARKS+=("$bench")
    fi
done

if [ ${#FOUND_BENCHMARKS[@]} -eq 0 ]; then
    echo -e "${RED}Error: No ARM benchmarks found in $BUILD_DIR${NC}"
    echo "Please build benchmarks with -DTHEMIS_BUILD_BENCHMARKS=ON"
    exit 1
fi

echo -e "${GREEN}Found ${#FOUND_BENCHMARKS[@]} benchmark(s):${NC}"
for bench in "${FOUND_BENCHMARKS[@]}"; do
    echo "  - $bench"
done
echo ""

# System information
echo -e "${BLUE}=== System Information ===${NC}"
echo "Architecture: $ARCH"
echo "CPU Model: $(cat /proc/cpuinfo | grep -m1 'model name' | cut -d: -f2 | xargs || echo 'Unknown')"
echo "CPU Cores: $(nproc)"
echo "Total Memory: $(free -h | grep Mem | awk '{print $2}')"
echo "Kernel: $(uname -r)"
echo ""

# Create report header
{
    echo "=========================================="
    echo "ThemisDB ARM Benchmark Report"
    echo "=========================================="
    echo ""
    echo "Date: $(date)"
    echo "Architecture: $ARCH"
    echo "Expected SIMD: $EXPECTED_SIMD"
    echo "Build Directory: $BUILD_DIR"
    echo ""
    echo "System Information:"
    echo "  CPU: $(cat /proc/cpuinfo | grep -m1 'model name' | cut -d: -f2 | xargs || cat /proc/cpuinfo | grep -m1 'Hardware' | cut -d: -f2 | xargs || echo 'Unknown')"
    echo "  Cores: $(nproc)"
    echo "  Memory: $(free -h | grep Mem | awk '{print $2}')"
    echo ""
    echo "=========================================="
    echo ""
} > "$REPORT_FILE"

# Run benchmarks
echo -e "${BLUE}=== Running Benchmarks ===${NC}"
echo ""

for bench in "${FOUND_BENCHMARKS[@]}"; do
    echo -e "${GREEN}Running: $bench${NC}"
    
    # Add benchmark section to report
    {
        echo "----------------------------------------"
        echo "Benchmark: $bench"
        echo "----------------------------------------"
        echo ""
    } >> "$REPORT_FILE"
    
    # Run benchmark and append to report
    if "$BUILD_DIR/$bench" --benchmark_format=console 2>&1 | tee -a "$REPORT_FILE"; then
        echo -e "${GREEN}✓ $bench completed${NC}"
    else
        echo -e "${YELLOW}⚠ $bench had warnings or errors${NC}"
    fi
    
    echo "" >> "$REPORT_FILE"
    echo ""
done

# Generate summary
echo -e "${BLUE}=== Generating Summary ===${NC}"

{
    echo "=========================================="
    echo "Summary"
    echo "=========================================="
    echo ""
    echo "Total benchmarks run: ${#FOUND_BENCHMARKS[@]}"
    echo "Results saved to: $REPORT_FILE"
    echo ""
    
    # Extract key performance metrics if available
    if grep -q "SIMD" "$REPORT_FILE"; then
        echo "SIMD Performance Detected:"
        grep -A 2 "BM_ARM.*SIMD" "$REPORT_FILE" | grep -v "^--$" || echo "  (metrics not available)"
        echo ""
    fi
    
    echo "Platform-specific notes:"
    case "$ARCH" in
        aarch64|arm64)
            echo "  - ARM64 NEON optimizations active"
            echo "  - FMA instructions may be available"
            echo "  - Expected 2-4x speedup over scalar code"
            ;;
        armv7l|armv7)
            echo "  - ARMv7 NEON optimizations may be active"
            echo "  - Check for NEON support in CPU features"
            echo "  - Expected 2-3x speedup over scalar code"
            ;;
        x86_64|amd64)
            echo "  - x86_64 SIMD (AVX2/AVX512) optimizations"
            echo "  - Results can be used as baseline comparison"
            ;;
    esac
    echo ""
    
    echo "To view detailed results:"
    echo "  cat $REPORT_FILE"
    echo ""
    echo "To compare with previous runs:"
    echo "  diff $REPORT_FILE benchmark_results/arm_benchmark_<previous>.txt"
    echo ""
} | tee -a "$REPORT_FILE"

echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}Benchmark run complete!${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "Report saved to: $REPORT_FILE"
echo ""

# Optionally generate JSON output
if command -v jq >/dev/null 2>&1; then
    echo -e "${BLUE}Generating JSON summary...${NC}"
    JSON_FILE="${BENCHMARK_DIR}/arm_benchmark_${TIMESTAMP}.json"
    
    # Simple JSON structure
    cat > "$JSON_FILE" << EOF
{
  "timestamp": "$(date -Iseconds)",
  "architecture": "$ARCH",
  "expected_simd": "$EXPECTED_SIMD",
  "cpu_cores": $(nproc),
  "benchmarks_run": ${#FOUND_BENCHMARKS[@]},
  "report_file": "$REPORT_FILE",
  "build_dir": "$BUILD_DIR"
}
EOF
    
    echo "JSON summary: $JSON_FILE"
fi

exit 0
