$ErrorActionPreference = 'Stop'

$packageName = 'themisdb'

# Stop and remove service if it exists
$service = Get-Service -Name 'ThemisDB' -ErrorAction SilentlyContinue
if ($service) {
    Write-Host "Stopping ThemisDB service..."
    Stop-Service -Name 'ThemisDB' -Force -ErrorAction SilentlyContinue
    
    Write-Host "Removing ThemisDB service..."
    sc.exe delete 'ThemisDB'
}

# Remove from PATH
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$binPath = Join-Path $toolsDir "bin"
Uninstall-ChocolateyPath -PathToUninstall $binPath -PathType 'Machine'

Write-Host "ThemisDB uninstalled successfully!" -ForegroundColor Green
Write-Host "Note: Data directory C:\ProgramData\ThemisDB was preserved."
