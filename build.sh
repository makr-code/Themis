#!/usr/bin/env bash
set -euo pipefail

# Cross-platform Linux/macOS/WSL build script
# Prereq: setup.sh executed or VCPKG_ROOT set

BUILD_TYPE=${BUILD_TYPE:-Release}
GENERATOR=${GENERATOR:-}

# Default build dir: prefer build-wsl under WSL, else build
DEFAULT_BUILD_DIR=build
if grep -qi microsoft /proc/version 2>/dev/null; then
  DEFAULT_BUILD_DIR=build-wsl
fi
BUILD_DIR=${BUILD_DIR:-${DEFAULT_BUILD_DIR}}
ENABLE_TESTS=${ENABLE_TESTS:-ON}
ENABLE_BENCHMARKS=${ENABLE_BENCHMARKS:-OFF}
ENABLE_GPU=${ENABLE_GPU:-OFF}
ENABLE_TRACING=${ENABLE_TRACING:-ON}
ENABLE_ASAN=${ENABLE_ASAN:-OFF}
STRICT=${STRICT:-OFF}
RUN_TESTS=${RUN_TESTS:-0}

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
  -DTHEMIS_BUILD_TESTS="${ENABLE_TESTS}"
  -DTHEMIS_BUILD_BENCHMARKS="${ENABLE_BENCHMARKS}"
  -DTHEMIS_ENABLE_GPU="${ENABLE_GPU}"
  -DTHEMIS_ENABLE_TRACING="${ENABLE_TRACING}"
  -DTHEMIS_ENABLE_ASAN="${ENABLE_ASAN}"
  -DTHEMIS_STRICT_BUILD="${STRICT}"
)

if [[ -n "${GENERATOR}" ]]; then
  CMAKE_ARGS+=( -G "${GENERATOR}" )
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j

# Run tests if present
if [[ "${RUN_TESTS}" == "1" ]]; then
  if command -v ctest >/dev/null 2>&1; then
    ctest -C "${BUILD_TYPE}" --test-dir "${BUILD_DIR}" --output-on-failure || true
  fi
fi

echo "Build completed: ${BUILD_DIR}"
echo "Server binary (if built): ${BUILD_DIR}/themis_server${EXE_SUFFIX}"  # EXE_SUFFIX leer auf Linux/macOS
echo "Options: TESTS=${ENABLE_TESTS} BENCHMARKS=${ENABLE_BENCHMARKS} GPU=${ENABLE_GPU} TRACING=${ENABLE_TRACING} ASAN=${ENABLE_ASAN} STRICT=${STRICT}"
if [[ "${RUN_TESTS}" == "1" ]]; then echo "Tests: executed"; else echo "Tests: skipped (set RUN_TESTS=1)"; fi