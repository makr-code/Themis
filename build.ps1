# Build script for THEMIS
# Run this script from PowerShell

param(
    [switch]$WithSecurityScan = $false,
    [switch]$FailOnScanWarnings = $false
)

Write-Host "=== THEMIS Build Script ===" -ForegroundColor Cyan
Write-Host ""

# Check if vcpkg is installed
if (-not $env:VCPKG_ROOT) {
    Write-Host "Error: VCPKG_ROOT environment variable not set!" -ForegroundColor Red
    Write-Host "Please install vcpkg and set VCPKG_ROOT environment variable." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Quick setup:" -ForegroundColor Yellow
    Write-Host "  git clone https://github.com/microsoft/vcpkg.git" -ForegroundColor Gray
    Write-Host "  cd vcpkg" -ForegroundColor Gray
    Write-Host "  .\bootstrap-vcpkg.bat" -ForegroundColor Gray
    Write-Host "  `$env:VCPKG_ROOT = (Get-Location).Path" -ForegroundColor Gray
    exit 1
}

Write-Host "vcpkg found at: $env:VCPKG_ROOT" -ForegroundColor Green

# Create build directory
$buildDir = "build"
if (-not (Test-Path $buildDir)) {
    Write-Host "Creating build directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

# Navigate to build directory
Set-Location $buildDir

Write-Host ""
Write-Host "=== Configuring CMake ===" -ForegroundColor Cyan

# Configure with CMake
$toolchainFile = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake .. -DCMAKE_TOOLCHAIN_FILE="$toolchainFile" `
         -DTHEMIS_BUILD_TESTS=ON `
         -DTHEMIS_BUILD_BENCHMARKS=OFF `
         -DTHEMIS_ENABLE_GPU=OFF

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Write-Host ""
Write-Host "=== Building ===" -ForegroundColor Cyan

# Build
cmake --build . --config Release -j

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Write-Host ""
Write-Host "=== Build successful! ===" -ForegroundColor Green
Write-Host ""
Write-Host "Executable location:" -ForegroundColor Cyan
Write-Host "  .\build\Release\themis_server.exe" -ForegroundColor Gray
Write-Host ""
Write-Host "To run the demo:" -ForegroundColor Cyan
Write-Host "  .\build\Release\themis_server.exe" -ForegroundColor Gray
Write-Host ""

# Optional: Run security scan after successful build
if ($WithSecurityScan) {
    Write-Host "=== Security Scan (optional) ===" -ForegroundColor Cyan
    $scanScript = Join-Path (Get-Location).Path "..\security-scan.ps1"
    if (Test-Path $scanScript) {
        $scanOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $scanScript 2>&1 | Out-String
        Write-Host $scanOutput
        if ($FailOnScanWarnings) {
            if ($scanOutput -match "WARN|\[C/C\+\+\]|\[Secrets\]") {
                Write-Host "Security scan reported warnings and FailOnScanWarnings is set. Failing the build." -ForegroundColor Red
                # Return to project root before exit
                Set-Location ..
                exit 2
            }
        }
    } else {
        Write-Warning "security-scan.ps1 not found. Skipping security scan."
    }
}

# Return to project root
Set-Location ..
