#!/usr/bin/env bash
set -euo pipefail

echo "[WSL] Starting Release build with tests/benchmarks"

# Resolve repo path on Windows mount
REPO_MNT_PATH="/mnt/c/VCC/themis"
if [ ! -d "$REPO_MNT_PATH" ]; then
  echo "[WSL] Repo path not found at $REPO_MNT_PATH" >&2
  exit 1
fi

# Ensure Ninja is available
if ! command -v ninja >/dev/null 2>&1; then
  echo "[WSL] Installing ninja-build via apt..."
  sudo apt-get update
  sudo apt-get install -y ninja-build
fi

# vcpkg toolchain
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
  echo "[WSL] vcpkg toolchain not found at $TOOLCHAIN_FILE" >&2
  echo "      Please install vcpkg in WSL (git clone https://github.com/microsoft/vcpkg.git && ./bootstrap-vcpkg.sh)" >&2
  exit 1
fi

BUILD_DIR="$HOME/themis-build-release"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[WSL] Configuring CMake (Release, tests+benchmarks ON)"
cmake "$REPO_MNT_PATH" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DTHEMIS_BUILD_TESTS=ON \
  -DTHEMIS_BUILD_BENCHMARKS=ON

echo "[WSL] Building..."
cmake --build . -j "$(command -v nproc >/dev/null 2>&1 && nproc || echo 2)"

echo "[WSL] Running tests..."
ctest -j 2 --output-on-failure || true

echo "[WSL] Done"
