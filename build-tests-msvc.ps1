# Build script for MSVC with /FS flag to avoid PDB conflicts
# Usage: .\build-tests-msvc.ps1

$ErrorActionPreference = "Stop"

Write-Host "=== Building Themis Tests (MSVC with /FS) ===" -ForegroundColor Cyan

# Clean old PDB files
Write-Host "Cleaning old PDB files..." -ForegroundColor Yellow
Remove-Item -Recurse -Force build-msvc/Debug/*.pdb -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force build-msvc/Release/*.pdb -ErrorAction SilentlyContinue

# Configure CMake (if needed)
if (!(Test-Path "build-msvc/CMakeCache.txt")) {
    Write-Host "Configuring CMake..." -ForegroundColor Yellow
    
    # Set vcpkg toolchain
    $vcpkgRoot = if (Test-Path "C:\Users\mkrueger\vcpkg") { "C:\Users\mkrueger\vcpkg" } else { $env:VCPKG_ROOT }
    $toolchainFile = "$vcpkgRoot\scripts\buildsystems\vcpkg.cmake"
    
    cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="$toolchainFile"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
}

# Build with single thread to avoid PDB conflicts
Write-Host "Building themis_tests (single-threaded to avoid PDB locks)..." -ForegroundColor Yellow
cmake --build build-msvc --config Debug --target themis_tests -- /m:1 /p:UseMultiToolTask=false /p:CL_MPCount=1

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== Build successful! ===" -ForegroundColor Green
    Write-Host "Run tests with: .\build-msvc\Debug\themis_tests.exe" -ForegroundColor Cyan
} else {
    Write-Host "`n=== Build failed! ===" -ForegroundColor Red
    exit 1
}
