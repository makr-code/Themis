Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host '==> Cleaning build directory'
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Write-Host 'vswhere not found. Please install Visual Studio.'; exit 2 }
$inst = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $inst) { Write-Host 'vswhere did not find an installation'; exit 3 }
$vsdev = Join-Path $inst 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsdev)) { Write-Host "VsDevCmd not found at $vsdev"; exit 4 }

Write-Host "Using VsDevCmd at: $vsdev"
$externalVcpkg = Join-Path (Get-Location).Path 'external\vcpkg'
if (-not (Test-Path $externalVcpkg)) { Write-Host "external vcpkg not found at $externalVcpkg"; exit 5 }

$cmd = '"' + $vsdev + '" -arch=amd64 -host_arch=amd64 && powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\windows_build.ps1 -VcpkgRoot "' + $externalVcpkg + '" -SkipVcpkgInstall'
Write-Host "Running developer env and windows_build.ps1 with external vcpkg: $externalVcpkg"
cmd.exe /c $cmd
Write-Host 'Done.'
