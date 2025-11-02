param(
    [string]$Configuration = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$tools = Join-Path $root "tools"
$dist = Join-Path $root "dist"

$projects = @(
    "Themis.AuditLogViewer\Themis.AuditLogViewer.csproj",
    "Themis.PIIManager\Themis.PIIManager.csproj",
    "Themis.KeyRotationDashboard\Themis.KeyRotationDashboard.csproj",
    "Themis.RetentionManager\Themis.RetentionManager.csproj",
    "Themis.ClassificationDashboard\Themis.ClassificationDashboard.csproj",
    "Themis.ComplianceReports\Themis.ComplianceReports.csproj",
    "Themis.SAGAVerifier\Themis.SAGAVerifier.csproj"
)

if ($Clean) {
    if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null

foreach ($proj in $projects) {
    $projPath = Join-Path $tools $proj
    if (-not (Test-Path $projPath)) {
        Write-Warning "Projekt nicht gefunden: $projPath"
        continue
    }
    Write-Host "Publishing $projPath ..." -ForegroundColor Cyan
    dotnet publish $projPath /p:PublishProfile=SelfContainedWin64 /p:Configuration=$Configuration
}

Write-Host "Fertig. Artefakte liegen unter: $dist" -ForegroundColor Green
