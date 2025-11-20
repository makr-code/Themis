#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Run SonarQube analysis for ThemisDB
.DESCRIPTION
    Executes complete SonarQube analysis including build wrapper and scanner
.PARAMETER Clean
    Clean build before analysis
.PARAMETER Upload
    Upload results to SonarCloud/SonarQube server
#>

param(
    [switch]$Clean,
    [switch]$Upload
)

$ErrorActionPreference = "Stop"

Write-Host "═══════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  SonarQube Analysis for ThemisDB" -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════════════════" -ForegroundColor Cyan

# Check for required tools
$requiredTools = @("build-wrapper-win-x64", "sonar-scanner")
foreach ($tool in $requiredTools) {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "❌ $tool not found. Install SonarQube Scanner and Build Wrapper."
        exit 1
    }
}

# Clean previous analysis
if ($Clean) {
    Write-Host "`n🧹 Cleaning previous analysis data..." -ForegroundColor Yellow
    Remove-Item -Path "bw-output" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path ".sonarqube" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path ".sonar-cache" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "build-msvc" -Recurse -Force -ErrorAction SilentlyContinue
}

# Configure CMake with compile commands
Write-Host "`n⚙️  Configuring CMake..." -ForegroundColor Yellow
cmake -B build-msvc -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
    -DTHEMIS_ENABLE_HSM_REAL=OFF

if ($LASTEXITCODE -ne 0) {
    Write-Error "❌ CMake configuration failed"
    exit 1
}

# Build with wrapper
Write-Host "`n🔨 Building with SonarQube wrapper..." -ForegroundColor Yellow
build-wrapper-win-x64 --out-dir bw-output cmake --build build-msvc -j 8

if ($LASTEXITCODE -ne 0) {
    Write-Error "❌ Build failed"
    exit 1
}

# Run tests for coverage (optional)
Write-Host "`n🧪 Running tests..." -ForegroundColor Yellow
Push-Location build-msvc
ctest --output-on-failure -j 8
$testResult = $LASTEXITCODE
Pop-Location

if ($testResult -ne 0) {
    Write-Warning "⚠️  Some tests failed, continuing with analysis..."
}

# Run SonarQube Scanner
Write-Host "`n📊 Running SonarQube Scanner..." -ForegroundColor Yellow

$scannerArgs = @()

if ($Upload) {
    # Check for SonarQube token
    if (!$env:SONAR_TOKEN) {
        Write-Error "❌ SONAR_TOKEN environment variable not set"
        Write-Host "Set it with: `$env:SONAR_TOKEN='your-token-here'" -ForegroundColor Yellow
        exit 1
    }
    
    $scannerArgs += "-Dsonar.login=$env:SONAR_TOKEN"
    
    if ($env:SONAR_HOST_URL) {
        $scannerArgs += "-Dsonar.host.url=$env:SONAR_HOST_URL"
    } else {
        # Default to SonarCloud
        $scannerArgs += "-Dsonar.host.url=https://sonarcloud.io"
    }
} else {
    Write-Host "ℹ️  Running local analysis (not uploading)" -ForegroundColor Cyan
    $scannerArgs += "-Dsonar.scanner.dumpToFile=sonar-report.json"
}

& sonar-scanner $scannerArgs

if ($LASTEXITCODE -ne 0) {
    Write-Error "❌ SonarQube Scanner failed"
    exit 1
}

Write-Host "`n✅ SonarQube analysis complete!" -ForegroundColor Green

if ($Upload) {
    Write-Host "`n📈 View results at:" -ForegroundColor Cyan
    Write-Host "   https://sonarcloud.io/dashboard?id=themisdb" -ForegroundColor Blue
} else {
    Write-Host "`n📄 Local report: sonar-report.json" -ForegroundColor Cyan
}
