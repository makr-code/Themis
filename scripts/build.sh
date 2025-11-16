#!/usr/bin/env bash
set -euo pipefail

# Simple WSL build helper for ThemisDB
# Usage (in WSL/Ubuntu):
#   ./scripts/build.sh               # default: bootstrap vcpkg, cmake (Ninja), build Release, run ctest
# Environment variables (optional):
#   VCPKG_ROOT           - where vcpkg lives (default: $HOME/vcpkg)
#   BUILD_DIR            - build directory (default: build)
#   BUILD_TYPE           - Release/Debug (default: Release)
#   VCPKG_TARGET_TRIPLET - vcpkg triplet (default: x64-linux)
#   RUN_TESTS            - 1 to run ctest, 0 to skip (default: 1)
#   SKIP_VCPKG_INSTALL   - 1 to skip cloning/bootstrap of vcpkg if not present (default: 0)
#   NUM_JOBS             - parallel jobs for build/tests (default: auto from nproc)

VCPKG_ROOT=${VCPKG_ROOT:-$HOME/vcpkg}
BUILD_DIR=${BUILD_DIR:-build}
BUILD_TYPE=${BUILD_TYPE:-Release}
TRIPLET=${VCPKG_TARGET_TRIPLET:-x64-linux}
RUN_TESTS=${RUN_TESTS:-1}
SKIP_VCPKG_INSTALL=${SKIP_VCPKG_INSTALL:-0}
NUM_JOBS=${NUM_JOBS:-$(nproc)}

echo "=== Themis WSL build helper ==="
echo "VCPKG_ROOT: $VCPKG_ROOT"
echo "BUILD_DIR: $BUILD_DIR"
echo "BUILD_TYPE: $BUILD_TYPE"
echo "VCPKG_TARGET_TRIPLET: $TRIPLET"
echo "RUN_TESTS: $RUN_TESTS"
echo "NUM_JOBS: $NUM_JOBS"

# Ensure running inside WSL/Linux
if grep -qi microsoft /proc/version 2>/dev/null; then
  echo "Running under WSL"
else
  echo "Warning: This script is designed for WSL/Ubuntu. Continuing anyway..."
fi

# Ensure minimum tools exist
command -v git >/dev/null 2>&1 || { echo "git not found. Install it (sudo apt install git)."; exit 1; }

echo "Updating apt and installing system packages (may ask for sudo)..."
sudo apt-get update

# Install core packages first (avoid failing on python3-distutils variants)
sudo apt-get install -y build-essential cmake ninja-build git pkg-config python3 libssl-dev wget unzip ca-certificates || true

# Try installing python3-distutils; if not available try versioned distutils, else fall back to pip/setuptools
PYVER=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
echo "Detected python version: $PYVER"
if ! sudo apt-get install -y python3-distutils 2>/dev/null; then
  echo "python3-distutils not available via apt (trying versioned package)..."
  if sudo apt-get install -y "python3-${PYVER}-distutils" 2>/dev/null; then
    echo "Installed python3-${PYVER}-distutils"
  else
    echo "No distutils package found; installing python3-pip and python3-setuptools instead"
    sudo apt-get install -y python3-pip python3-setuptools python3-venv
    # Try to upgrade pip/setuptools in user-site to avoid modifying system-managed packages (PEP 668)
    echo "Attempting to upgrade pip/setuptools into user site (will not modify system packages)..."
    if python3 -m pip install --user --upgrade pip setuptools 2>/dev/null; then
      echo "Upgraded pip/setuptools in user site"
    else
      echo "Could not upgrade pip/setuptools in user site (system may be externally managed - PEP 668)."
      echo "That's OK; the script will continue. If you need isolated Python tooling, create a venv:"
      echo "  python3 -m venv ~/.themis-build-venv && source ~/.themis-build-venv/bin/activate && python -m pip install --upgrade pip setuptools"
    fi
  fi
else
  echo "Installed python3-distutils"
fi

# Bootstrap vcpkg if necessary
if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
  if [ "$SKIP_VCPKG_INSTALL" = "1" ]; then
    echo "vcpkg not found at $VCPKG_ROOT and SKIP_VCPKG_INSTALL=1 -> aborting"
    exit 1
  fi
  echo "Cloning vcpkg into $VCPKG_ROOT..."
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
  echo "Bootstrapping vcpkg (this may take a few minutes)..."
  (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh)
else
  echo "Found existing vcpkg at $VCPKG_ROOT"
fi

# Prepare build directory
if [ -d "$BUILD_DIR" ]; then
  # If a CMakeCache exists and was created on Windows (drive letter like C:/...),
  # remove the build dir to avoid mismatched cache/source paths when running under WSL.
  if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cache_home=$(grep -m1 '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$BUILD_DIR/CMakeCache.txt" || true)
    cache_home=${cache_home#CMAKE_HOME_DIRECTORY:INTERNAL=}
    if [ -n "$cache_home" ]; then
      if echo "$cache_home" | grep -qE '^[A-Za-z]:[\\/]'; then
        echo "Detected CMakeCache created under Windows path ($cache_home). Removing stale build directory to avoid path mismatch..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
      fi
    fi
  fi
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure CMake with vcpkg toolchain and Ninja
echo "Running CMake configure..."
cmake .. \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
  -DTHEMIS_BUILD_TESTS=ON

# Build
echo "Building (Release)..."
cmake --build . --config $BUILD_TYPE -j "$NUM_JOBS"

# Run tests if requested
if [ "$RUN_TESTS" = "1" ]; then
  echo "Running ctest (output on failure)..."
  if command -v ctest >/dev/null 2>&1; then
    ctest --output-on-failure --parallel "$NUM_JOBS"
  else
    # try to run the test binary directly if ctest isn't available
    if [ -x ./themis_tests ]; then
      echo "Running test binary ./themis_tests --gtest_output=xml:themis_tests.xml"
      ./themis_tests --gtest_output=xml:themis_tests.xml
    else
      echo "No ctest and no ./themis_tests binary found. Skipping tests."
    fi
  fi
fi

echo "Build script finished. Build artifacts are in: $(pwd)"

exit 0
