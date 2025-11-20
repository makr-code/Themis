# Build script for THEMIS
# Run this script from PowerShell

param(
    [string]$BuildType = "Release",
    [string]$Generator = "",            # Optional: "Ninja" oder "Visual Studio 17 2022"
    [string]$BuildDir = "build",         # Ziel-Build-Verzeichnis
    [switch]$RunTests = $false,            # Tests nach Build ausführen
    [switch]$EnableBenchmarks = $false,    # Benchmarks bauen
    [switch]$EnableGPU = $false,           # GPU aktivieren
    [switch]$EnableTracing = $true,        # OpenTelemetry aktivieren
    [switch]$EnableASAN = $false,          # AddressSanitizer (nur clang/gcc)
    [switch]$Strict = $false,              # Warnings as errors
    [switch]$Clean = $false,               # Vorheriges Build-Verzeichnis löschen
    [switch]$WithSecurityScan = $false,
    [switch]$FailOnScanWarnings = $false,
    [switch]$Quality = $false,             # Vor Build: clang-format Diff + clang-tidy auf gestagten Dateien
    [switch]$QualityApply = $false         # Format direkt anwenden (setzt implizit Quality)
)

Write-Host "=== THEMIS Build Script ===" -ForegroundColor Cyan
Write-Host ""

# Check if vcpkg is installed
if (-not $env:VCPKG_ROOT) {
    Write-Host "Error: VCPKG_ROOT environment variable not set!" -ForegroundColor Red
    Write-Host "Please install vcpkg and set VCPKG_ROOT environment variable." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Quick setup:" -ForegroundColor Yellow
    Write-Host "  git clone https://github.com/microsoft/vcpkg.git" -ForegroundColor Gray
    Write-Host "  cd vcpkg" -ForegroundColor Gray
    Write-Host "  .\bootstrap-vcpkg.bat" -ForegroundColor Gray
    Write-Host "  `$env:VCPKG_ROOT = (Get-Location).Path" -ForegroundColor Gray
    exit 1
}

Write-Host "vcpkg found at: $env:VCPKG_ROOT" -ForegroundColor Green

# Auto-detect generator and adapt default build dir
if (-not $Generator) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $Generator = "Ninja"
    } else {
        $Generator = "Visual Studio 17 2022"
    }
}
if ($BuildDir -eq "build") {
    $BuildDir = if ($Generator -eq "Ninja") { "build-ninja" } else { "build-msvc" }
}
Write-Host "Generator: $Generator" -ForegroundColor Green

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning existing build directory '$BuildDir'..." -ForegroundColor Yellow
    try {
        Remove-Item -Recurse -Force $BuildDir -ErrorAction Stop
    } catch {
        Write-Warning "Konnte Verzeichnis nicht vollständig löschen (evtl. gelockte Dateien). Weiche auf neuen Pfad aus."
        $orig = $BuildDir
        $BuildDir = "$($BuildDir)-fresh"
        Write-Host "Neues Build-Verzeichnis: $BuildDir (ursprünglich: $orig)" -ForegroundColor Yellow
    }
}
if (-not (Test-Path $BuildDir)) {
    Write-Host "Creating build directory '$BuildDir'..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Write-Host "Using build directory: $BuildDir" -ForegroundColor Green

Write-Host "BuildType: $BuildType" -ForegroundColor Green
Write-Host "Benchmarks: " + $(if ($EnableBenchmarks) {"ON"} else {"OFF"}) -ForegroundColor Green
Write-Host "GPU: " + $(if ($EnableGPU) {"ON"} else {"OFF"}) -ForegroundColor Green
Write-Host "Tracing: " + $(if ($EnableTracing) {"ON"} else {"OFF"}) -ForegroundColor Green
Write-Host "Quality (tidy): " + $(if ($Quality -or $QualityApply) {"ON"} else {"OFF"}) -ForegroundColor Green

if ($Generator -like "Visual Studio*") {
    $archFlag = "-A x64"
} else {
    $archFlag = ""
}

Write-Host ""
Write-Host "=== Configuring CMake ===" -ForegroundColor Cyan

$toolchainFile = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"

$testsFlag = "ON" # Standard: Tests bauen (für lokale Entwicklung hilfreich)
$benchFlag = if ($EnableBenchmarks) {"ON"} else {"OFF"}
$gpuFlag = if ($EnableGPU) {"ON"} else {"OFF"}
$tracingFlag = if ($EnableTracing) {"ON"} else {"OFF"}
$asanFlag = if ($EnableASAN) {"ON"} else {"OFF"}
$strictFlag = if ($Strict) {"ON"} else {"OFF"}

Write-Host "Invoking CMake configure..." -ForegroundColor Cyan

Push-Location $BuildDir

# Optional Code-Qualität vor dem eigentlichen Build: nutzt run_clang_quality.ps1
if ($Quality -or $QualityApply) {
    Write-Host "=== Running local code quality checks ===" -ForegroundColor Cyan
    $qualityScript = Join-Path (Resolve-Path ..).Path "scripts/run_clang_quality.ps1"
    if (Test-Path $qualityScript) {
        $invoke = "& '$qualityScript'" + $(if ($QualityApply) {" -ApplyFormat -FailOnFormat"} else {" -FailOnFormat"})
        Write-Host "Invoke: $invoke" -ForegroundColor Gray
        try {
            if ($QualityApply) { ./../scripts/run_clang_quality.ps1 -ApplyFormat -FailOnFormat }
            else { ./../scripts/run_clang_quality.ps1 -FailOnFormat }
        } catch {
            Write-Host "Quality checks failed." -ForegroundColor Red
            Pop-Location
            exit 5
        }
    } else {
        Write-Warning "Quality script not found (scripts/run_clang_quality.ps1). Skipping quality step."
    }
}

if ($Generator -eq "Ninja") {
    cmake .. -G Ninja `
        -DCMAKE_BUILD_TYPE=$BuildType `
        -DCMAKE_TOOLCHAIN_FILE="$toolchainFile" `
        -DTHEMIS_BUILD_TESTS=$testsFlag `
        -DTHEMIS_BUILD_BENCHMARKS=$benchFlag `
        -DTHEMIS_ENABLE_GPU=$gpuFlag `
        -DTHEMIS_ENABLE_TRACING=$tracingFlag `
        -DTHEMIS_ENABLE_ASAN=$asanFlag `
        -DTHEMIS_STRICT_BUILD=$strictFlag
} else {
    # Multi-Config Generator (Visual Studio)
    cmake .. -G "$Generator" $archFlag `
        -DCMAKE_TOOLCHAIN_FILE="$toolchainFile" `
        -DTHEMIS_BUILD_TESTS=$testsFlag `
        -DTHEMIS_BUILD_BENCHMARKS=$benchFlag `
        -DTHEMIS_ENABLE_GPU=$gpuFlag `
        -DTHEMIS_ENABLE_TRACING=$tracingFlag `
        -DTHEMIS_ENABLE_ASAN=$asanFlag `
        -DTHEMIS_STRICT_BUILD=$strictFlag
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Write-Host ""
Write-Host "=== Building ===" -ForegroundColor Cyan

Write-Host "Starting build..." -ForegroundColor Cyan
if ($Generator -eq "Ninja") {
    cmake --build . -- -j
} else {
    cmake --build . --config $BuildType -- /m
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}

Write-Host ""
Write-Host "=== Build successful! ===" -ForegroundColor Green
Write-Host ""
if ($Generator -eq "Ninja") {
    $exePath = Join-Path (Get-Location) "themis_server.exe"
} else {
    $exePath = Join-Path (Get-Location) "$BuildType/themis_server.exe"
}
Write-Host "Executable location:" -ForegroundColor Cyan
Write-Host "  $exePath" -ForegroundColor Gray
Write-Host "To run the demo:" -ForegroundColor Cyan
Write-Host "  $exePath" -ForegroundColor Gray
Write-Host ""

if ($RunTests) {
    Write-Host "=== Running tests ===" -ForegroundColor Cyan
    if ($Generator -eq "Ninja") {
        ctest --output-on-failure -C $BuildType
    } else {
        ctest --output-on-failure -C $BuildType
    }
}

# Optional: Run security scan after successful build
if ($WithSecurityScan) {
    Write-Host "=== Security Scan (optional) ===" -ForegroundColor Cyan
    $scanScript = Join-Path (Get-Location).Path "..\security-scan.ps1"
    if (Test-Path $scanScript) {
        $scanOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $scanScript 2>&1 | Out-String
        Write-Host $scanOutput
        if ($FailOnScanWarnings) {
            if ($scanOutput -match "WARN|\[C/C\+\+\]|\[Secrets\]") {
                Write-Host "Security scan reported warnings and FailOnScanWarnings is set. Failing the build." -ForegroundColor Red
                # Return to project root before exit
                Set-Location ..
                exit 2
            }
        }
    } else {
        Write-Warning "security-scan.ps1 not found. Skipping security scan."
    }
}

Pop-Location
Write-Host "Done." -ForegroundColor Green
