#!/bin/bash
# Local code quality checks for ThemisDB
# Run this before pushing to ensure CI will pass

set -e  # Exit on error

echo "========================================"
echo "ThemisDB Code Quality Checks"
echo "========================================"
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if commands exist
command_exists() {
    command -v "$1" &> /dev/null
}

# Run with optional skip
run_check() {
    local name=$1
    local cmd=$2
    local skip_var=$3
    
    if [ "${!skip_var}" = "1" ]; then
        echo -e "${YELLOW}⊘ Skipping $name${NC}"
        return 0
    fi
    
    echo ""
    echo "========================================"
    echo "Running $name..."
    echo "========================================"
    
    if eval "$cmd"; then
        echo -e "${GREEN}✓ $name passed${NC}"
        return 0
    else
        echo -e "${RED}✗ $name failed${NC}"
        return 1
    fi
}

# Parse arguments
SKIP_BUILD=0
SKIP_TIDY=0
SKIP_CPPCHECK=0
SKIP_GITLEAKS=0
SKIP_TESTS=0
FIX_MODE=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build) SKIP_BUILD=1; shift ;;
        --skip-tidy) SKIP_TIDY=1; shift ;;
        --skip-cppcheck) SKIP_CPPCHECK=1; shift ;;
        --skip-gitleaks) SKIP_GITLEAKS=1; shift ;;
        --skip-tests) SKIP_TESTS=1; shift ;;
        --fix) FIX_MODE=1; shift ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --skip-build      Skip CMake build"
            echo "  --skip-tidy       Skip clang-tidy"
            echo "  --skip-cppcheck   Skip cppcheck"
            echo "  --skip-gitleaks   Skip gitleaks"
            echo "  --skip-tests      Skip unit tests"
            echo "  --fix             Auto-fix issues where possible"
            echo "  --help            Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Check prerequisites
echo "Checking prerequisites..."
MISSING_TOOLS=0

if ! command_exists cmake; then
    echo -e "${RED}✗ cmake not found${NC}"
    MISSING_TOOLS=1
fi

if ! command_exists clang-tidy && [ "$SKIP_TIDY" != "1" ]; then
    echo -e "${YELLOW}⊘ clang-tidy not found (will skip)${NC}"
    SKIP_TIDY=1
fi

if ! command_exists cppcheck && [ "$SKIP_CPPCHECK" != "1" ]; then
    echo -e "${YELLOW}⊘ cppcheck not found (will skip)${NC}"
    SKIP_CPPCHECK=1
fi

if ! command_exists gitleaks && [ "$SKIP_GITLEAKS" != "1" ]; then
    echo -e "${YELLOW}⊘ gitleaks not found (will skip)${NC}"
    SKIP_GITLEAKS=1
fi

if [ "$MISSING_TOOLS" = "1" ]; then
    echo ""
    echo "Install missing tools:"
    echo "  Ubuntu/Debian: sudo apt-get install cmake clang-tidy cppcheck"
    echo "  macOS: brew install cmake llvm cppcheck gitleaks"
    exit 1
fi

# Create build directory if needed
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir -p build
fi

# Build project
if [ "$SKIP_BUILD" != "1" ]; then
    run_check "CMake Configuration" \
        "cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" \
        SKIP_BUILD
    
    run_check "Build" \
        "cmake --build build --parallel $(nproc 2>/dev/null || echo 2)" \
        SKIP_BUILD
fi

# Run clang-tidy
if [ "$SKIP_TIDY" != "1" ]; then
    TIDY_CMD="find src include -name '*.cpp' -o -name '*.h' | \
        xargs clang-tidy -p build --quiet 2>&1 | \
        tee clang-tidy-report.txt; \
        grep -E 'warning:|error:' clang-tidy-report.txt | wc -l | \
        awk '{if (\$1 > 0) {print \"Found \" \$1 \" issues\"; exit 1} else {print \"No issues found\"; exit 0}}'"
    
    if [ "$FIX_MODE" = "1" ]; then
        TIDY_CMD="${TIDY_CMD/--quiet/--fix --quiet}"
    fi
    
    run_check "Clang-Tidy Static Analysis" "$TIDY_CMD" SKIP_TIDY || true
fi

# Run cppcheck
if [ "$SKIP_CPPCHECK" != "1" ]; then
    CPPCHECK_CMD="cppcheck \
        --enable=all \
        --std=c++17 \
        --language=c++ \
        --platform=unix64 \
        --suppressions-list=.cppcheck-suppressions \
        --inline-suppr \
        --quiet \
        -I include/ \
        src/ \
        2>&1 | tee cppcheck-report.txt; \
        if [ -s cppcheck-report.txt ]; then \
            echo 'Found issues:'; \
            cat cppcheck-report.txt; \
            exit 1; \
        else \
            echo 'No issues found'; \
        fi"
    
    run_check "Cppcheck Linting" "$CPPCHECK_CMD" SKIP_CPPCHECK || true
fi

# Run gitleaks
if [ "$SKIP_GITLEAKS" != "1" ]; then
    GITLEAKS_CMD="gitleaks detect \
        --source . \
        --config .gitleaks.toml \
        --report-format json \
        --report-path gitleaks-report.json \
        --verbose \
        --no-git; \
        if [ -f gitleaks-report.json ] && [ -s gitleaks-report.json ]; then \
            echo 'Secrets detected:'; \
            jq -r '.[] | \"[\(.RuleID)] \(.File):\(.StartLine)\"' gitleaks-report.json; \
            exit 1; \
        else \
            echo 'No secrets detected'; \
        fi"
    
    run_check "Gitleaks Secret Scanning" "$GITLEAKS_CMD" SKIP_GITLEAKS
fi

# Run tests
if [ "$SKIP_TESTS" != "1" ]; then
    run_check "Unit Tests" \
        "cd build && ctest --output-on-failure" \
        SKIP_TESTS
fi

# Summary
echo ""
echo "========================================"
echo -e "${GREEN}All checks completed!${NC}"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Review any warnings above"
echo "  2. Fix issues if needed"
echo "  3. Commit and push changes"
echo ""
echo "Reports generated:"
echo "  - clang-tidy-report.txt"
echo "  - cppcheck-report.txt"
echo "  - gitleaks-report.json (if secrets found)"
echo ""
