$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
# Start Jaeger
& "$root\start_jaeger.ps1" @args
# Start Themis server minimized if not running
if (-not (Get-Process -Name 'themis_server' -ErrorAction SilentlyContinue)) {
  $exe = Join-Path $root "..\build\Release\themis_server.exe"
  Start-Process -FilePath $exe -WindowStyle Minimized | Out-Null
  Start-Sleep -Seconds 2
  Write-Host "themis_server started."
} else {
  Write-Host "themis_server already running."
}
# Quick health check
try {
  $resp = Invoke-RestMethod -Method Get -Uri http://localhost:8765/health
  Write-Host (ConvertTo-Json $resp -Depth 4)
} catch {
  Write-Warning $_.Exception.Message
}
