# ThemisDB Docker Build for QNAP (Ubuntu 20.04)
# Builds container compatible with older GLIBC versions

param(
    [switch]$NoCache,
    [string]$Tag = "themisdb:qnap"
)

$ErrorActionPreference = "Stop"

Write-Host "=== ThemisDB QNAP Docker Build ===" -ForegroundColor Cyan
Write-Host "Target: Ubuntu 20.04 (GLIBC 2.31 compatible)" -ForegroundColor Green
Write-Host ""

$buildArgs = @(
    "build",
    "-f", "Dockerfile.qnap",
    "-t", $Tag
)

if ($NoCache) {
    $buildArgs += "--no-cache"
    Write-Host "Building with --no-cache" -ForegroundColor Yellow
}

$buildArgs += "."

Write-Host "Executing: docker $($buildArgs -join ' ')" -ForegroundColor Gray
Write-Host ""

& docker $buildArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "=== Build erfolgreich ===" -ForegroundColor Green
    Write-Host "Image: $Tag" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Testen:" -ForegroundColor Yellow
    Write-Host "  docker run --rm $Tag --version" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Verifiziere GLIBC-Kompatibilität:" -ForegroundColor Yellow
    Write-Host "  docker run --rm $Tag ldd /usr/local/bin/themis_server | grep GLIBC" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Docker Compose starten:" -ForegroundColor Yellow
    Write-Host "  docker-compose -f docker-compose.qnap.yml up -d" -ForegroundColor Gray
} else {
    Write-Host ""
    Write-Host "=== Build fehlgeschlagen ===" -ForegroundColor Red
    exit 1
}
