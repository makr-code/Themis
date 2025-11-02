# Local code quality checks for ThemisDB (PowerShell)
# Run this before pushing to ensure CI will pass

param(
    [switch]$SkipBuild,
    [switch]$SkipTidy,
    [switch]$SkipCppcheck,
    [switch]$SkipGitleaks,
    [switch]$SkipTests,
    [switch]$Fix,
    [switch]$Help
)

# Show help
if ($Help) {
    Write-Host @"
ThemisDB Code Quality Checks

Usage: .\scripts\check-quality.ps1 [OPTIONS]

Options:
  -SkipBuild      Skip CMake build
  -SkipTidy       Skip clang-tidy
  -SkipCppcheck   Skip cppcheck
  -SkipGitleaks   Skip gitleaks
  -SkipTests      Skip unit tests
  -Fix            Auto-fix issues where possible
  -Help           Show this help

Examples:
  .\scripts\check-quality.ps1
  .\scripts\check-quality.ps1 -SkipBuild -SkipTests
  .\scripts\check-quality.ps1 -Fix

"@
    exit 0
}

# Color output functions
function Write-Success { param($msg) Write-Host "✓ $msg" -ForegroundColor Green }
function Write-Error { param($msg) Write-Host "✗ $msg" -ForegroundColor Red }
function Write-Warning { param($msg) Write-Host "⊘ $msg" -ForegroundColor Yellow }
function Write-Section { param($msg) Write-Host "`n========================================`n$msg`n========================================" -ForegroundColor Cyan }

Write-Section "ThemisDB Code Quality Checks"

# Check prerequisites
Write-Host "Checking prerequisites..."
$MissingTools = @()

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $MissingTools += "cmake"
}

if (-not (Get-Command clang-tidy -ErrorAction SilentlyContinue) -and -not $SkipTidy) {
    Write-Warning "clang-tidy not found (will skip)"
    $SkipTidy = $true
}

if (-not (Get-Command cppcheck -ErrorAction SilentlyContinue) -and -not $SkipCppcheck) {
    Write-Warning "cppcheck not found (will skip)"
    $SkipCppcheck = $true
}

if (-not (Get-Command gitleaks -ErrorAction SilentlyContinue) -and -not $SkipGitleaks) {
    Write-Warning "gitleaks not found (will skip)"
    $SkipGitleaks = $true
}

if ($MissingTools.Count -gt 0) {
    Write-Error "Missing required tools: $($MissingTools -join ', ')"
    Write-Host "`nInstall with:"
    Write-Host "  choco install cmake llvm cppcheck gitleaks"
    Write-Host "  OR download from official websites"
    exit 1
}

# Create build directory
if (-not (Test-Path "build")) {
    Write-Host "Creating build directory..."
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# Track overall success
$AllPassed = $true

# Build project
if (-not $SkipBuild) {
    Write-Section "CMake Configuration"
    try {
        cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
        Write-Success "CMake configuration passed"
    } catch {
        Write-Error "CMake configuration failed: $_"
        $AllPassed = $false
    }
    
    Write-Section "Build"
    try {
        cmake --build build --config Debug --parallel
        if ($LASTEXITCODE -ne 0) { throw "Build failed" }
        Write-Success "Build passed"
    } catch {
        Write-Error "Build failed: $_"
        $AllPassed = $false
    }
}

# Run clang-tidy
if (-not $SkipTidy) {
    Write-Section "Clang-Tidy Static Analysis"
    try {
        $SourceFiles = Get-ChildItem -Path src,include -Include *.cpp,*.h -Recurse
        $TidyArgs = @("-p", "build", "--quiet")
        if ($Fix) {
            $TidyArgs += "--fix"
        }
        
        $TidyOutput = $SourceFiles | ForEach-Object {
            clang-tidy $TidyArgs $_.FullName 2>&1
        }
        
        $TidyOutput | Out-File -FilePath "clang-tidy-report.txt"
        
        $IssueCount = ($TidyOutput | Select-String -Pattern "warning:|error:").Count
        
        if ($IssueCount -gt 0) {
            Write-Warning "Found $IssueCount clang-tidy issues"
            $TidyOutput | Select-String -Pattern "warning:|error:" | Select-Object -First 20
            # Don't fail on warnings, just report
        } else {
            Write-Success "Clang-tidy passed - no issues found"
        }
    } catch {
        Write-Warning "Clang-tidy failed: $_"
    }
}

# Run cppcheck
if (-not $SkipCppcheck) {
    Write-Section "Cppcheck Linting"
    try {
        $CppcheckArgs = @(
            "--enable=all",
            "--std=c++17",
            "--language=c++",
            "--platform=win64",
            "--suppressions-list=.cppcheck-suppressions",
            "--inline-suppr",
            "--quiet",
            "-Iinclude/",
            "src/"
        )
        
        $CppcheckOutput = cppcheck $CppcheckArgs 2>&1
        $CppcheckOutput | Out-File -FilePath "cppcheck-report.txt"
        
        if ($CppcheckOutput) {
            Write-Warning "Cppcheck found issues:"
            $CppcheckOutput | Select-Object -First 20
            # Don't fail on warnings, just report
        } else {
            Write-Success "Cppcheck passed - no issues found"
        }
    } catch {
        Write-Warning "Cppcheck failed: $_"
    }
}

# Run gitleaks
if (-not $SkipGitleaks) {
    Write-Section "Gitleaks Secret Scanning"
    try {
        $GitleaksArgs = @(
            "detect",
            "--source", ".",
            "--config", ".gitleaks.toml",
            "--report-format", "json",
            "--report-path", "gitleaks-report.json",
            "--verbose",
            "--no-git"
        )
        
        gitleaks $GitleaksArgs 2>&1 | Out-Null
        
        if (Test-Path "gitleaks-report.json") {
            $LeaksContent = Get-Content "gitleaks-report.json" -Raw
            if ($LeaksContent -and $LeaksContent.Trim() -ne "[]" -and $LeaksContent.Trim() -ne "") {
                $Leaks = $LeaksContent | ConvertFrom-Json
                if ($Leaks.Count -gt 0) {
                    Write-Error "Found $($Leaks.Count) potential secret(s)"
                    $Leaks | ForEach-Object {
                        Write-Host "  [$($_.RuleID)] $($_.File):$($_.StartLine)" -ForegroundColor Red
                    }
                    $AllPassed = $false
                } else {
                    Write-Success "Gitleaks passed - no secrets detected"
                }
            } else {
                Write-Success "Gitleaks passed - no secrets detected"
            }
        } else {
            Write-Success "Gitleaks passed - no secrets detected"
        }
    } catch {
        Write-Error "Gitleaks failed: $_"
        $AllPassed = $false
    }
}

# Run tests
if (-not $SkipTests) {
    Write-Section "Unit Tests"
    try {
        Push-Location build
        ctest -C Debug --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
        Pop-Location
        Write-Success "Tests passed"
    } catch {
        Pop-Location
        Write-Error "Tests failed: $_"
        $AllPassed = $false
    }
}

# Summary
Write-Section "Summary"

if ($AllPassed) {
    Write-Success "All checks completed successfully!"
} else {
    Write-Error "Some checks failed - review output above"
}

Write-Host "`nReports generated:"
Write-Host "  - clang-tidy-report.txt"
Write-Host "  - cppcheck-report.txt"
if (Test-Path "gitleaks-report.json") {
    Write-Host "  - gitleaks-report.json"
}

Write-Host "`nNext steps:"
Write-Host "  1. Review any warnings above"
Write-Host "  2. Fix issues if needed"
Write-Host "  3. Commit and push changes"
Write-Host ""

if (-not $AllPassed) {
    exit 1
}
