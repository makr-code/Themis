param(
  [string]$Version = "1.74.0",
  [string]$UiPort = "16686",
  [string]$OtlpPort = "4318"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$tools = Join-Path $root "..\tools\jaeger"
New-Item -ItemType Directory -Path $tools -Force | Out-Null
$zip = Join-Path $tools "jaeger-$Version-windows-amd64.zip"
$folder = Join-Path $tools "jaeger-$Version-windows-amd64"
$exe = Join-Path $folder "jaeger-all-in-one.exe"
if (-not (Test-Path $exe)) {
  Write-Host "Downloading Jaeger $Version..."
  $url = "https://github.com/jaegertracing/jaeger/releases/download/v$Version/jaeger-$Version-windows-amd64.zip"
  Invoke-WebRequest -Uri $url -OutFile $zip
  Expand-Archive -Path $zip -DestinationPath $tools -Force
}
if (-not (Get-Process -Name 'jaeger-all-in-one' -ErrorAction SilentlyContinue)) {
  Start-Process -FilePath $exe -ArgumentList "--collector.otlp.enabled=true --collector.otlp.http.host-port=:$OtlpPort --query.http-server.host-port=:$UiPort" -WindowStyle Minimized | Out-Null
  Start-Sleep -Seconds 2
  Write-Host "Jaeger started: UI=http://localhost:$UiPort, OTLP HTTP=:$OtlpPort"
} else {
  Write-Host "Jaeger already running."
}
