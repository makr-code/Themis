<#
Local Windows build helper for ThemisDB
Usage (PowerShell, run as normal user with Developer Command Prompt available in PATH):
  .\scripts\windows_build.ps1                     # default: bootstraps vcpkg (if missing), builds Release, runs tests
  .\scripts\windows_build.ps1 -VcpkgRoot C:\vcpkg -Triplet x64-windows -Config Release -RunTests:$false

Parameters:
  -VcpkgRoot   Path to vcpkg root (default: $env:VCPKG_ROOT or .\vcpkg)
  -BuildDir    Build directory (default: build)
  -Triplet     vcpkg triplet (default: x64-windows)
  -Config      Build config (Release/Debug) default Release
  -RunTests    Switch: run unit tests (default: $true)
  -UseNinja    Switch: prefer Ninja generator when available and MSVC is in PATH
  -SkipVcpkgInstall Switch: skip cloning/bootstrapping vcpkg if missing
#>

param(
    [string]$VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $PSScriptRoot '..\vcpkg' }),
    [string]$BuildDir = 'build',
    [string]$Triplet = 'x64-windows',
    [string]$Config = 'Release',
    [switch]$RunTests = $true,
    [switch]$UseNinja = $true,
    [switch]$SkipVcpkgInstall = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "=== Themis Windows build helper ==="
Write-Host "VcpkgRoot: $VcpkgRoot"
Write-Host "BuildDir: $BuildDir"
Write-Host "Triplet: $Triplet"
Write-Host "Config: $Config"
Write-Host "RunTests: $RunTests"

# Check for MSVC toolchain (cl.exe)
$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    Write-Warning "cl.exe not found in PATH. Ensure Visual Studio 'Desktop development with C++' is installed and Developer Command Prompt is used or MSVC is in PATH."
}

# Ensure git available
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git is required but not found in PATH. Install Git for Windows and retry."
}

# Bootstrap vcpkg if missing
if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
    if ($SkipVcpkgInstall) {
        throw "vcpkg not found at $VcpkgRoot and SkipVcpkgInstall specified. Aborting."
    }
    Write-Host "Cloning vcpkg into $VcpkgRoot..."
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    Write-Host "Bootstrapping vcpkg (this may take a while)..."
    Push-Location $VcpkgRoot
    & .\bootstrap-vcpkg.bat
    Pop-Location
} else {
    Write-Host "Found existing vcpkg at $VcpkgRoot"
}

# Determine generator
$generator = 'Visual Studio 17 2022'
$generatorArgs = @()
if ($UseNinja -and (Get-Command ninja -ErrorAction SilentlyContinue) -and $cl) {
    $generator = 'Ninja'
    Write-Host "Using Ninja generator (Ninja present and cl.exe available)."
} else {
    Write-Host "Using Visual Studio generator: $generator"
    $generatorArgs += '-A' ; $generatorArgs += 'x64'
}

# Prepare build dir; if CMake cache exists with linux paths we keep it, but clear stale Windows->WSL mismatch already handled by separate script
$absBuildDir = Join-Path (Get-Location) $BuildDir
if (Test-Path $absBuildDir) {
    Write-Host "Build dir exists: $absBuildDir"
} else {
    New-Item -ItemType Directory -Path $absBuildDir | Out-Null
}

# Configure CMake
$cmakeCmd = 'cmake'
$toolchainFile = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $toolchainFile)) {
    throw "vcpkg toolchain file not found at $toolchainFile"
}

$cmakeArgs = @(
    '-S', (Get-Location).Path,
    '-B', $absBuildDir,
    '-G', $generator,
    '-DCMAKE_BUILD_TYPE=' + $Config,
    '-DCMAKE_TOOLCHAIN_FILE=' + $toolchainFile,
    '-DVCPKG_TARGET_TRIPLET=' + $Triplet,
    '-DTHEMIS_BUILD_TESTS=ON'
)
$cmakeArgs += $generatorArgs

Write-Host "Running CMake configure..."
Write-Host "$cmakeCmd $($cmakeArgs -join ' ')"
& $cmakeCmd $cmakeArgs

# Build
Write-Host "Building..."
& $cmakeCmd --build $absBuildDir --config $Config -- -m

# Run tests
if ($RunTests) {
    Write-Host "Running tests with ctest..."
    $ctest = Get-Command ctest -ErrorAction SilentlyContinue
    if ($ctest) {
        & ctest -C $Config --test-dir $absBuildDir --output-on-failure
    } else {
        Write-Warning "ctest not found; trying to locate test binary(s) in build dir"
        # Try to execute themis_tests.exe
        $testExe = Join-Path $absBuildDir "$Config\themis_tests.exe"
        if (Test-Path $testExe) {
            & $testExe --gtest_output=xml:themis_tests.xml
        } else {
            Write-Warning "No test runner found. Skipping test run."
        }
    }
}

# Collect artifacts
$artifactsRoot = Join-Path (Get-Location) '..\artifacts\windows' | Resolve-Path -Relative
$artifactsDir = Join-Path $artifactsRoot "$Triplet\$Config"
New-Item -ItemType Directory -Force -Path $artifactsDir | Out-Null

Write-Host "Collecting artifacts into $artifactsDir"
# Common binary outputs
$possibleBins = @(
    Join-Path $absBuildDir "$Config\themis_server.exe" ,
    Join-Path $absBuildDir "$Config\themis_core.dll",
    Join-Path $absBuildDir "$Config\themis_core.lib",
    Join-Path $absBuildDir "$Config\themis_tests.exe"
)
foreach ($b in $possibleBins) {
    if (Test-Path $b) {
        Copy-Item -Path $b -Destination $artifactsDir -Force
    }
}

# Copy runtime DLLs (from build tree) if present
Get-ChildItem -Path (Join-Path $absBuildDir $Config) -Filter '*.dll' -File -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $artifactsDir -Force
}

Write-Host "Windows build finished. Artifacts are in: $artifactsDir"
Write-Host "You can now create a Windows container or copy artifacts to your Windows Docker build context."

exit 0
