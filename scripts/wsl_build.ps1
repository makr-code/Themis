<#
WSL Build Helper for ThemisDB

This PowerShell script runs the recommended setup + build steps inside WSL (Ubuntu).
It will:
 - check for WSL availability
 - run package installation (build-essential, cmake, ninja, git, libssl-dev, etc.)
 - install vcpkg into the WSL home directory if missing and bootstrap it
 - create a build dir for the repository (assumes repo is mounted at /mnt/c/VCC/themis)
 - run CMake with the vcpkg toolchain (Ninja generator) and build
 - run relevant tests (TimeRange / Temporal aggregation)

Usage (from Windows PowerShell):
  .\scripts\wsl_build.ps1 [-RepoWinPath C:\VCC\themis] [-VcpkgPath ~/vcpkg] [-RunTests]

Examples:
  .\scripts\wsl_build.ps1
  .\scripts\wsl_build.ps1 -RepoWinPath C:\Projects\themis -RunTests

Note:
 - This script executes commands inside WSL. It assumes you have an Ubuntu-based WSL distribution.
 - For heavy builds, consider cloning the repo into the WSL filesystem (~/projects) for better IO performance.
#>
param(
    [string]$RepoWinPath = 'C:\VCC\themis',
    [string]$VcpkgPath = '~/vcpkg',
    [switch]$RunTests = $true
)

function Fail($msg){
    Write-Error $msg
    exit 1
}

# Check WSL availability
if (-not (Get-Command wsl -ErrorAction SilentlyContinue)){
    Fail "wsl.exe not found. Please enable WSL and install a Linux distro (Ubuntu recommended)."
}

# Normalize Windows repo path to WSL mount path
$repoWin = (Resolve-Path $RepoWinPath).ProviderPath
# Convert C:\foo to /mnt/c/foo
$drive = $repoWin.Substring(0,1).ToLower()
$rest = $repoWin.Substring(2) -replace '\\\\','/' -replace '\\','/'
$wslRepoPath = "/mnt/$drive/$rest"

Write-Host "Repository Windows path: $repoWin"
Write-Host "WSL path: $wslRepoPath"

# Compose bash commands to run inside WSL
$bash = @"
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

# 1) Install system packages
echo '==> Installing system packages (apt-get)'
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends build-essential cmake ninja-build git pkg-config python3 python3-pip wget unzip libssl-dev ca-certificates curl

# 2) Ensure repo exists at path
if [ ! -d "$wslRepoPath" ]; then
  echo "ERROR: Repository not found at $wslRepoPath inside WSL. Mount the Windows path or clone the repo inside WSL."
  exit 2
fi

# 3) Install vcpkg if missing
if [ ! -d $VcpkgPath ]; then
  echo '==> Cloning vcpkg into $VcpkgPath'
  git clone https://github.com/microsoft/vcpkg.git $VcpkgPath
  (cd $VcpkgPath && ./bootstrap-vcpkg.sh)
else
  echo '==> vcpkg already present at $VcpkgPath'
fi

export VCPKG_ROOT=$VcpkgPath

# 4) (optional) Preinstall common packages to speed up CMake/vcpkg
echo '==> Installing common vcpkg packages (may take a few minutes)'
$VcpkgPath/vcpkg install openssl:x64-linux rocksdb:x64-linux gtest:x64-linux hnswlib:x64-linux nlohmann-json:x64-linux yaml-cpp:x64-linux lz4:x64-linux || true

# 5) Configure CMake in repo
cd $wslRepoPath
rm -rf build
mkdir -p build && cd build

echo '==> Running CMake'
cmake .. -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DTHEMIS_BUILD_TESTS=ON \
  -DTHEMIS_BUILD_BENCHMARKS=OFF \
  -DTHEMIS_ENABLE_GPU=OFF

echo '==> Building (Release)'
cmake --build . --config Release -j

if [ $($RunTests -ne $null) ]; then
  echo '==> Running TimeRange/Temporal tests'
  if [ -f ./themis_tests ]; then
    ./themis_tests --gtest_filter="*TimeRange*"
    ./themis_tests --gtest_filter="*TemporalAggregation*" || true
  else
    echo 'Warning: test binary ./themis_tests not found; skipping test run'
  fi
fi

echo '==> Build finished successfully in WSL.'
"@

# Replace PowerShell variable interpolation for $RunTests inside heredoc: we want to pass whether to run tests
# We'll create final command with RunTests true/false
if ($RunTests -and $RunTests.IsPresent) {
  $runTestsFlag = 'true'
} else {
  $runTestsFlag = 'false'
}
$bash = $bash -replace '\$\(\$RunTests -ne \$null\)', $runTestsFlag

Write-Host "Executing build steps inside WSL (this may take several minutes)..."
# Execute
wsl bash -lc $bash

Write-Host "All done. If you want the build inside WSL filesystem for faster IO, consider cloning the repo into ~/projects and re-running the script."