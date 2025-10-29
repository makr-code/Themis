#!/usr/bin/env bash
set -euo pipefail

# Cross-platform setup for Linux/macOS (vcpkg bootstrap)
# Windows users can use setup.ps1

# Detect or install vcpkg
if [[ -z "${VCPKG_ROOT:-}" ]]; then
  echo "VCPKG_ROOT not set. Installing vcpkg locally to ./external/vcpkg ..."
  mkdir -p external
  if [[ ! -d external/vcpkg ]]; then
    git clone https://github.com/microsoft/vcpkg.git external/vcpkg
  fi
  export VCPKG_ROOT="$(pwd)/external/vcpkg"
  echo "VCPKG_ROOT set to ${VCPKG_ROOT}"
fi

# Bootstrap vcpkg
if [[ ! -f "${VCPKG_ROOT}/vcpkg" ]]; then
  pushd "$VCPKG_ROOT" >/dev/null
  ./bootstrap-vcpkg.sh
  popd >/dev/null
fi

echo "vcpkg ready at: ${VCPKG_ROOT}"