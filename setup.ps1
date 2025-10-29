# Setup script for THEMIS development environment
# Run this ONCE to setup vcpkg and install dependencies

Write-Host "=== THEMIS Development Environment Setup ===" -ForegroundColor Cyan
Write-Host ""

# Check if vcpkg exists
if (-not $env:VCPKG_ROOT) {
    Write-Host "vcpkg not found. Installing vcpkg..." -ForegroundColor Yellow
    
    $vcpkgPath = "$HOME\vcpkg"
    
    if (Test-Path $vcpkgPath) {
        Write-Host "vcpkg directory already exists at $vcpkgPath" -ForegroundColor Green
    } else {
        Write-Host "Cloning vcpkg from GitHub..." -ForegroundColor Yellow
        git clone https://github.com/microsoft/vcpkg.git $vcpkgPath
        
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Failed to clone vcpkg!" -ForegroundColor Red
            exit 1
        }
    }
    
    # Bootstrap vcpkg
    Write-Host "Bootstrapping vcpkg..." -ForegroundColor Yellow
    Set-Location $vcpkgPath
    .\bootstrap-vcpkg.bat
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Failed to bootstrap vcpkg!" -ForegroundColor Red
        exit 1
    }
    
    # Set environment variable
    $env:VCPKG_ROOT = $vcpkgPath
    [Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgPath, "User")
    
    Write-Host "vcpkg installed successfully at: $vcpkgPath" -ForegroundColor Green
    Write-Host "VCPKG_ROOT environment variable set" -ForegroundColor Green
    
    # Return to project directory
    Set-Location $PSScriptRoot
}

Write-Host ""
Write-Host "=== Installing Dependencies ===" -ForegroundColor Cyan
Write-Host "This may take 30-60 minutes on first run..." -ForegroundColor Yellow
Write-Host ""

# Install dependencies using vcpkg.json manifest mode
# vcpkg will automatically install all dependencies listed in vcpkg.json
# when CMake is run with the vcpkg toolchain

Write-Host "Dependencies will be installed automatically during CMake configuration" -ForegroundColor Green
Write-Host ""

Write-Host "=== Setup Complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Run: .\build.ps1" -ForegroundColor Gray
Write-Host "  2. Run: .\build\Release\themis_server.exe" -ForegroundColor Gray
Write-Host ""
Write-Host "Optional: Enable GPU support" -ForegroundColor Cyan
Write-Host "  Edit build.ps1 and set -DTHEMIS_ENABLE_GPU=ON" -ForegroundColor Gray
Write-Host ""
