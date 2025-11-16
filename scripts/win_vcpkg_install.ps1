# Install vcpkg manifest dependencies for Themis (Windows x64)
$ErrorActionPreference = 'Stop'

if (-not $env:VCPKG_ROOT) {
    Write-Error "VCPKG_ROOT is not set. Run setup.ps1 first."
    exit 1
}

$vcpkgExe = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'
if (-not (Test-Path $vcpkgExe)) {
    Write-Error "vcpkg.exe not found at $vcpkgExe"
    exit 1
}

Write-Host "Running: $vcpkgExe install --triplet x64-windows"
# Older vcpkg versions expect packages on the command line or will detect manifest automatically.
# Call without --manifest to maximize compatibility with installed vcpkg.
& $vcpkgExe install --triplet x64-windows
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "vcpkg manifest install completed."