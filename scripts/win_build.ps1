# Windows build helper for Themis
$ErrorActionPreference = 'Stop'
Write-Host "Running Windows build helper..."

if (-not $env:VCPKG_ROOT) {
    Write-Host "VCPKG_ROOT not set. Please run setup.ps1 first or set VCPKG_ROOT." -ForegroundColor Red
    exit 1
}

$toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
Write-Host "Using vcpkg toolchain: $toolchain"

# Use an isolated build directory to avoid locked files (use build-msvc)
$buildDir = 'build-msvc'
if (Test-Path $buildDir) {
    Write-Host "Removing existing $buildDir directory for a clean configure..."
    Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
}

cmake -S . -B $buildDir -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DVCPKG_TARGET_TRIPLET=x64-windows -A x64 --debug-output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Listing generated .vcxproj (first 20):"
Get-ChildItem -Path $buildDir -Recurse -File -Filter "*.vcxproj" | Select-Object -First 20

Write-Host "Starting build of themis_tests (Debug)..."
cmake --build $buildDir --config Debug --target themis_tests -v
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build finished successfully."
