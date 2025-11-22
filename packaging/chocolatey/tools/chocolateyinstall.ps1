$ErrorActionPreference = 'Stop'

$packageName = 'themisdb'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$version = '1.0.0'
$url64 = "https://github.com/makr-code/ThemisDB/releases/download/v$version/themisdb-$version-win64.zip"

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  softwareName  = 'ThemisDB*'
  checksum64    = ''  # Will be filled during package build
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs

# Add to PATH
$binPath = Join-Path $toolsDir "bin"
Install-ChocolateyPath -PathToInstall $binPath -PathType 'Machine'

# Create service installation script
$serviceScript = @"
# Install ThemisDB as Windows Service
# Run this script as Administrator

`$serviceName = 'ThemisDB'
`$serviceDisplayName = 'ThemisDB Database Server'
`$serviceDescription = 'Multi-model database system with ACID transactions'
`$exePath = Join-Path '$binPath' 'themis_server.exe'
`$dataDir = 'C:\ProgramData\ThemisDB\data'
`$configPath = 'C:\ProgramData\ThemisDB\config.yaml'

# Create data directory
New-Item -ItemType Directory -Force -Path `$dataDir | Out-Null

# Create default config if it doesn't exist
if (-not (Test-Path `$configPath)) {
    @'
storage:
  rocksdb_path: C:\ProgramData\ThemisDB\data
  memtable_size_mb: 256
  block_cache_size_mb: 1024

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 8
'@ | Set-Content -Path `$configPath
}

# Install service
New-Service -Name `$serviceName ``
    -DisplayName `$serviceDisplayName ``
    -Description `$serviceDescription ``
    -BinaryPathName "`$exePath --config `$configPath" ``
    -StartupType Automatic

Write-Host 'ThemisDB service installed successfully!'
Write-Host 'Start the service with: Start-Service ThemisDB'
"@

$serviceScript | Set-Content -Path (Join-Path $toolsDir "install-service.ps1")

Write-Host ""
Write-Host "ThemisDB installed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "To install as Windows Service, run as Administrator:" -ForegroundColor Yellow
Write-Host "  & '$toolsDir\install-service.ps1'" -ForegroundColor Cyan
Write-Host ""
Write-Host "Or run directly:" -ForegroundColor Yellow
Write-Host "  themis_server --config C:\ProgramData\ThemisDB\config.yaml" -ForegroundColor Cyan
