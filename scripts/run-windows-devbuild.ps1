param(
    [string]$OpenSslRoot = $env:OPENSSL_ROOT_DIR
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host '==> Cleaning Windows build directory: build-msvc'
Remove-Item -Recurse -Force .\build-msvc -ErrorAction SilentlyContinue

# Locate vswhere
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\\Installer\\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Write-Host 'vswhere not found. Please install Visual Studio or ensure cl.exe is in PATH.'
    exit 2
}

$inst = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $inst) {
    Write-Host 'vswhere did not return an installation path. Ensure Visual Studio with MSBuild is installed.'
    exit 3
}

$vsdev = Join-Path $inst 'Common7\\Tools\\VsDevCmd.bat'
if (-not (Test-Path $vsdev)) {
    Write-Host "VsDevCmd not found at $vsdev"
    exit 4
}

Write-Host "Found VsDevCmd: $vsdev"

# Build base CMake command
$cmakeCmd = 'cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=external/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_CXX_FLAGS="/bigobj /EHsc"'

if ($OpenSslRoot) {
    # Use forward slashes for CMake and quote path
    $norm = ($OpenSslRoot -replace '\\', '/').TrimEnd('/')
    $cmakeCmd += ' -DOPENSSL_ROOT_DIR="' + $norm + '"'
    Write-Host "Adding OPENSSL_ROOT_DIR: $norm"
} else {
    Write-Host 'No OPENSSL_ROOT_DIR provided; relying on vcpkg/toolchain discovery.'
}

# Create a temporary batch file to run inside cmd.exe (avoids quoting issues)
$cmdFile = Join-Path $PSScriptRoot 'build-msvc-run.cmd'
$lines = @()
$lines += 'call "' + $vsdev + '" -arch=amd64 -host_arch=amd64'
$lines += $cmakeCmd
$lines += 'cmake --build build-msvc --config Debug --target themis_tests'
$lines += 'cd build-msvc'
$lines += 'ctest -C Debug --output-on-failure -j1'

Write-Host "Writing temporary batch file: $cmdFile"
Set-Content -Path $cmdFile -Value $lines -Encoding ASCII

Write-Host 'Invoking Visual Studio Developer environment and build (may take several minutes)...'
cmd.exe /c $cmdFile

# Cleanup
Remove-Item -Force $cmdFile -ErrorAction SilentlyContinue

Write-Host 'Windows build script finished.'
