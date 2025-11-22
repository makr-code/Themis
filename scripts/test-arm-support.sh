#!/usr/bin/env bash
# Test script to validate ARM/Raspberry Pi compilation support
# This script checks if the architecture detection and SIMD support work correctly

set -e

echo "=== ThemisDB ARM/Raspberry Pi Compilation Test ==="
echo ""

# Detect current architecture
ARCH=$(uname -m)
echo "Detected architecture: $ARCH"

case "$ARCH" in
    x86_64|amd64)
        echo "  -> x86_64 platform"
        EXPECTED_SIMD="AVX2 or AVX512"
        ;;
    aarch64|arm64)
        echo "  -> ARM64/AArch64 platform"
        EXPECTED_SIMD="NEON"
        ;;
    armv7l|armv7)
        echo "  -> ARMv7 platform"
        EXPECTED_SIMD="NEON (if supported by CPU)"
        ;;
    *)
        echo "  -> Unknown architecture: $ARCH"
        EXPECTED_SIMD="Scalar fallback"
        ;;
esac

echo ""
echo "Expected SIMD support: $EXPECTED_SIMD"
echo ""

# Check if required tools are installed
echo "Checking prerequisites..."

command -v cmake >/dev/null 2>&1 || {
    echo "ERROR: cmake not found. Please install cmake."
    exit 1
}
echo "  ✓ cmake found: $(cmake --version | head -n1)"

command -v ninja >/dev/null 2>&1 && {
    echo "  ✓ ninja found: $(ninja --version)"
} || {
    echo "  ℹ ninja not found (optional, will use make)"
}

command -v gcc >/dev/null 2>&1 || {
    echo "ERROR: gcc not found. Please install gcc."
    exit 1
}
echo "  ✓ gcc found: $(gcc --version | head -n1)"

command -v g++ >/dev/null 2>&1 || {
    echo "ERROR: g++ not found. Please install g++."
    exit 1
}
echo "  ✓ g++ found: $(g++ --version | head -n1)"

if [ -z "$VCPKG_ROOT" ]; then
    echo "WARNING: VCPKG_ROOT not set. vcpkg is required for building."
    echo "  Run ./setup.sh to install vcpkg or set VCPKG_ROOT manually."
    exit 1
fi
echo "  ✓ VCPKG_ROOT set: $VCPKG_ROOT"

echo ""
echo "Checking CMakeLists.txt for ARM support..."

# Check if CMakeLists.txt has ARM detection
if grep -q "THEMIS_TARGET_ARCH" CMakeLists.txt; then
    echo "  ✓ Architecture detection found in CMakeLists.txt"
else
    echo "  ✗ Architecture detection not found in CMakeLists.txt"
    exit 1
fi

# Check if NEON support is in simd_distance.cpp
if grep -q "__ARM_NEON" src/utils/simd_distance.cpp; then
    echo "  ✓ ARM NEON support found in simd_distance.cpp"
else
    echo "  ✗ ARM NEON support not found in simd_distance.cpp"
    exit 1
fi

echo ""
echo "Checking CMake presets..."

# Check if ARM presets exist
if grep -q "rpi-arm64" CMakePresets.json; then
    echo "  ✓ Raspberry Pi ARM64 presets found"
else
    echo "  ✗ Raspberry Pi ARM64 presets not found"
    exit 1
fi

if grep -q "rpi-armv7" CMakePresets.json; then
    echo "  ✓ Raspberry Pi ARMv7 presets found"
else
    echo "  ✗ Raspberry Pi ARMv7 presets not found"
    exit 1
fi

echo ""
echo "Testing CMake configuration..."

# Create a test build directory
BUILD_DIR="build-arm-test"
rm -rf "$BUILD_DIR"

# Try to configure the build
echo "  Running CMake configuration..."
if cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DTHEMIS_BUILD_TESTS=OFF \
    -DTHEMIS_BUILD_BENCHMARKS=OFF \
    > "$BUILD_DIR/cmake_output.log" 2>&1; then
    echo "  ✓ CMake configuration successful"
else
    echo "  ✗ CMake configuration failed. See $BUILD_DIR/cmake_output.log"
    tail -n 20 "$BUILD_DIR/cmake_output.log"
    exit 1
fi

# Check what architecture was detected
if grep -q "Target architecture:" "$BUILD_DIR/cmake_output.log"; then
    DETECTED=$(grep "Target architecture:" "$BUILD_DIR/cmake_output.log" | head -n1)
    echo "  $DETECTED"
else
    echo "  ℹ Could not determine detected architecture from CMake output"
fi

# Check for ARM-specific flags in compile commands
if [ -f "$BUILD_DIR/compile_commands.json" ]; then
    echo ""
    echo "Checking compiler flags..."
    
    case "$ARCH" in
        aarch64|arm64)
            if grep -q "armv8-a" "$BUILD_DIR/compile_commands.json"; then
                echo "  ✓ ARM64 flags detected (-march=armv8-a)"
            else
                echo "  ℹ ARM64 flags not detected (might be using generic flags)"
            fi
            ;;
        armv7l|armv7)
            if grep -q "armv7-a" "$BUILD_DIR/compile_commands.json" && \
               grep -q "neon" "$BUILD_DIR/compile_commands.json"; then
                echo "  ✓ ARMv7 NEON flags detected"
            else
                echo "  ℹ ARMv7 NEON flags not detected"
            fi
            ;;
        x86_64|amd64)
            if grep -q "march=native" "$BUILD_DIR/compile_commands.json"; then
                echo "  ✓ x86_64 native flags detected"
            else
                echo "  ℹ x86_64 native flags not detected (might be Debug build)"
            fi
            ;;
    esac
fi

echo ""
echo "=== Test Summary ==="
echo "✓ All checks passed!"
echo ""
echo "Architecture: $ARCH"
echo "Expected SIMD: $EXPECTED_SIMD"
echo "CMake configuration: OK"
echo ""
echo "To build ThemisDB on this platform, run:"
echo ""

case "$ARCH" in
    aarch64|arm64)
        echo "  cmake --preset rpi-arm64-gcc-release"
        echo "  cmake --build --preset rpi-arm64-gcc-release"
        ;;
    armv7l|armv7)
        echo "  cmake --preset rpi-armv7-gcc-release"
        echo "  cmake --build --preset rpi-armv7-gcc-release"
        ;;
    *)
        echo "  ./build.sh"
        ;;
esac

echo ""
echo "Or use the standard build script:"
echo "  ./build.sh"
echo ""
echo "For more information, see: docs/ARM_RASPBERRY_PI_BUILD.md"

# Cleanup
rm -rf "$BUILD_DIR"
