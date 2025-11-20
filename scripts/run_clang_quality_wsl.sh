#!/usr/bin/env bash
# Lokal auszuführendes Qualitätsskript für ThemisDB unter WSL.
# Funktionen:
# 1. Prüft Installation von clang-format / clang-tidy.
# 2. Erzeugt compile_commands.json falls nicht vorhanden.
# 3. Zeigt Format-Diffs oder wendet Format optional an.
# 4. Führt clang-tidy gegen geänderte oder alle Dateien aus.
# Nutzung:
#   ./scripts/run_clang_quality_wsl.sh [--apply-format] [--all] [--fix] [--jobs N]
# Beispiele:
#   ./scripts/run_clang_quality_wsl.sh                  # Nur Diff & Tidy auf geänderten Dateien
#   ./scripts/run_clang_quality_wsl.sh --apply-format    # Format direkt anwenden
#   ./scripts/run_clang_quality_wsl.sh --all --fix       # Alle Dateien, automatische Fixes (vorsichtig!)
#   ./scripts/run_clang_quality_wsl.sh --jobs 8          # Parallelisierung für clang-tidy
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-wsl}" # erlaubt externes Override
COMPILE_DB="${BUILD_DIR}/compile_commands.json"
APPLY_FORMAT=false
ALL_FILES=false
FIX_MODE=false
JOBS=4
FAIL_ON_FORMAT=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply-format) APPLY_FORMAT=true; shift ;;
    --all) ALL_FILES=true; shift ;;
    --fix) FIX_MODE=true; shift ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --fail-on-format) FAIL_ON_FORMAT=true; shift ;;
    *) echo "Unbekannte Option: $1"; exit 2 ;;
  esac
done

need_tool() {
  local bin="$1"
  if ! command -v "$bin" >/dev/null 2>&1; then
    echo "[INFO] Installiere fehlendes Tool: $bin" >&2
    sudo apt-get update -y && sudo apt-get install -y "$bin"
  fi
}

need_tool clang-format
need_tool clang-tidy
need_tool cmake

if [[ ! -f "$COMPILE_DB" ]]; then
  echo "[INFO] Erzeuge compile_commands.json via CMake Export" >&2
  mkdir -p "$BUILD_DIR"
  (cd "$BUILD_DIR" && cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug "$ROOT_DIR")
fi

# Dateiliste ermitteln
if $ALL_FILES; then
  mapfile -t FILES < <(git ls-files '*.cpp' '*.c' '*.hpp' '*.h')
else
  # Geänderte Dateien gegen HEAD
  mapfile -t FILES < <(git diff --name-only --diff-filter=ACMR HEAD | grep -E '\.(cpp|c|hpp|h)$' || true)
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "[INFO] Keine passenden Dateien gefunden." >&2
else
  echo "[INFO] Prüfe Format für ${#FILES[@]} Dateien" >&2
  format_changed=0
  if $APPLY_FORMAT; then
    clang-format -i "${FILES[@]}"
  else
    for f in "${FILES[@]}"; do
      # diff nur anzeigen, nicht anwenden
      if ! diff -u <(cat "$f") <(clang-format "$f") >/dev/null; then
        echo "[FORMAT] Abweichung: $f" >&2
        format_changed=1
      fi
    done
  fi

  echo "[INFO] Starte clang-tidy" >&2
  TIDY_EXTRA=()
  $FIX_MODE && TIDY_EXTRA+=("-fix" "-format")

  # Parallelisierte Ausführung: einfache Aufteilung
  export COMPILE_DB
  run_tidy() {
    local file="$1"
    clang-tidy "$file" -p "$BUILD_DIR" "${TIDY_EXTRA[@]}" || true
  }
  export -f run_tidy
  printf '%s\n' "${FILES[@]}" | xargs -n1 -P "$JOBS" bash -c 'run_tidy "$@"' _
echo "[INFO] Fertig." >&2
fi

if $FAIL_ON_FORMAT && [[ $format_changed -eq 1 ]]; then
  echo "[ERROR] Format-Abweichungen erkannt (--fail-on-format aktiv). Commit/Hooks sollten stoppen." >&2
  exit 3
fi

echo "[INFO] Fertig." >&2
