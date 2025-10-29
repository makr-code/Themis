#!/usr/bin/env bash
set -euo pipefail

# Cross-platform Linux/macOS build script
# Prereq: setup.sh executed or VCPKG_ROOT set

BUILD_TYPE=${BUILD_TYPE:-Release}
GENERATOR=${GENERATOR:-}
BUILD_DIR=${BUILD_DIR:-build}

if [[ -z "${VCPKG_ROOT:-}" ]]; then
  echo "VCPKG_ROOT not set. Run ./setup.sh first or export VCPKG_ROOT." >&2
  exit 1
fi

# Prefer Ninja if installed
if command -v ninja >/dev/null 2>&1; then
  GENERATOR=${GENERATOR:-"Ninja"}
fi

CMAKE_ARGS=(
  -S .
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
)

if [[ -n "${GENERATOR}" ]]; then
  CMAKE_ARGS+=( -G "${GENERATOR}" )
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j

# Run tests if present
if command -v ctest >/dev/null 2>&1; then
  ctest -C "${BUILD_TYPE}" --test-dir "${BUILD_DIR}" --output-on-failure || true
fi

echo "Build completed: ${BUILD_DIR}"