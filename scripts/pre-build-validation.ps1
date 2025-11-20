#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Pre-build code quality validation for ThemisDB
.DESCRIPTION
    Runs multiple static analysis tools to ensure code quality before building.
    Critical for database stability and reliability.
.PARAMETER Quick
    Run only fast checks (skip deep analysis)
.PARAMETER FailFast
    Stop on first error
.PARAMETER Strict
    Treat warnings as errors
#>

param(
    [switch]$Quick,
    [switch]$FailFast,
    [switch]$Strict
)

$ErrorActionPreference = if ($FailFast) { "Stop" } else { "Continue" }
$global:ValidationErrors = 0
$global:ValidationWarnings = 0

# Colors
$ColorReset = "`e[0m"
$ColorRed = "`e[91m"
$ColorYellow = "`e[93m"
$ColorGreen = "`e[92m"
$ColorBlue = "`e[94m"
$ColorCyan = "`e[96m"

function Write-Section {
    param([string]$Title)
    Write-Host "`n${ColorCyan}═══════════════════════════════════════════════════════${ColorReset}"
    Write-Host "${ColorCyan}  $Title${ColorReset}"
    Write-Host "${ColorCyan}═══════════════════════════════════════════════════════${ColorReset}`n"
}

function Write-Success {
    param([string]$Message)
    Write-Host "${ColorGreen}✓${ColorReset} $Message"
}

function Write-Warning {
    param([string]$Message)
    Write-Host "${ColorYellow}⚠${ColorReset} $Message"
    $global:ValidationWarnings++
}

function Write-Error-Custom {
    param([string]$Message)
    Write-Host "${ColorRed}✗${ColorReset} $Message"
    $global:ValidationErrors++
}

function Test-Command {
    param([string]$Command)
    $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
}

# ============================================================================
# 1. Environment Check
# ============================================================================

Write-Section "Environment Validation"

$requiredTools = @(
    @{Name="cmake"; Required=$true},
    @{Name="ninja"; Required=$false},
    @{Name="clang-tidy"; Required=$false},
    @{Name="cppcheck"; Required=$false}
)

foreach ($tool in $requiredTools) {
    if (Test-Command $tool.Name) {
        Write-Success "$($tool.Name) found"
    } elseif ($tool.Required) {
        Write-Error-Custom "$($tool.Name) not found (required)"
    } else {
        Write-Warning "$($tool.Name) not found (optional)"
    }
}

# ============================================================================
# 2. File Structure Validation
# ============================================================================

Write-Section "Project Structure Validation"

$requiredDirs = @("src", "include", "tests", "scripts")
$requiredFiles = @("CMakeLists.txt", "vcpkg.json", "README.md")

foreach ($dir in $requiredDirs) {
    if (Test-Path $dir) {
        Write-Success "Directory: $dir"
    } else {
        Write-Error-Custom "Missing directory: $dir"
    }
}

foreach ($file in $requiredFiles) {
    if (Test-Path $file) {
        Write-Success "File: $file"
    } else {
        Write-Error-Custom "Missing file: $file"
    }
}

# ============================================================================
# 3. Source Code Statistics
# ============================================================================

Write-Section "Source Code Statistics"

$cppFiles = Get-ChildItem -Path src,include -Include *.cpp,*.h,*.hpp -Recurse -File
$totalLines = ($cppFiles | Get-Content | Measure-Object -Line).Lines
$totalFiles = $cppFiles.Count

Write-Host "  Files: $totalFiles"
Write-Host "  Lines of Code: $totalLines"

# Check for large files
$largeFiles = $cppFiles | Where-Object { (Get-Content $_.FullName | Measure-Object -Line).Lines -gt 1000 }
if ($largeFiles) {
    Write-Warning "Large files detected (>1000 lines):"
    $largeFiles | ForEach-Object {
        $lines = (Get-Content $_.FullName | Measure-Object -Line).Lines
        Write-Host "    $($_.Name): $lines lines"
    }
}

# ============================================================================
# 4. Header Guard Check
# ============================================================================

Write-Section "Header Guard Validation"

$headerFiles = Get-ChildItem -Path include -Include *.h,*.hpp -Recurse -File
$missingGuards = @()

foreach ($header in $headerFiles) {
    $content = Get-Content $header.FullName -Raw
    if ($content -notmatch '#pragma once' -and $content -notmatch '#ifndef.*\n#define') {
        $missingGuards += $header.Name
    }
}

if ($missingGuards.Count -eq 0) {
    Write-Success "All headers have guards"
} else {
    Write-Warning "Headers without guards: $($missingGuards.Count)"
    $missingGuards | ForEach-Object { Write-Host "    $_" }
}

# ============================================================================
# 5. Clang-Tidy Analysis
# ============================================================================

if ((Test-Command "clang-tidy") -and !$Quick) {
    Write-Section "Clang-Tidy Analysis"
    
    $changedFiles = git diff --name-only --diff-filter=ACM HEAD | Where-Object { $_ -match '\.(cpp|h)$' }
    
    if ($changedFiles) {
        Write-Host "  Analyzing $($changedFiles.Count) changed files..."
        
        $tidyErrors = 0
        foreach ($file in $changedFiles) {
            if (Test-Path $file) {
                Write-Host "    Checking: $file"
                $result = clang-tidy $file -p build-msvc 2>&1
                if ($LASTEXITCODE -ne 0) {
                    $tidyErrors++
                    if ($Strict) {
                        Write-Error-Custom "Clang-tidy failed for $file"
                    } else {
                        Write-Warning "Clang-tidy warnings in $file"
                    }
                }
            }
        }
        
        if ($tidyErrors -eq 0) {
            Write-Success "Clang-tidy passed"
        } else {
            Write-Warning "Clang-tidy found issues in $tidyErrors files"
        }
    } else {
        Write-Host "  No changed C++ files to analyze"
    }
}

# ============================================================================
# 6. Cppcheck Analysis
# ============================================================================

if ((Test-Command "cppcheck") -and !$Quick) {
    Write-Section "Cppcheck Static Analysis"
    
    Write-Host "  Running comprehensive analysis..."
    $cppcheckArgs = @(
        "--enable=warning,style,performance,portability",
        "--inline-suppr",
        "--suppress=missingIncludeSystem",
        "--suppress=unmatchedSuppression",
        "--quiet",
        "--template='{file}:{line}: {severity}: {message}'",
        "src/"
    )
    
    $cppcheckOutput = & cppcheck $cppcheckArgs 2>&1
    
    if ($cppcheckOutput) {
        $errorCount = ($cppcheckOutput | Select-String "error:").Count
        $warningCount = ($cppcheckOutput | Select-String "warning:|style:|performance:").Count
        
        if ($errorCount -gt 0) {
            Write-Error-Custom "Cppcheck found $errorCount errors"
            $cppcheckOutput | Select-String "error:" | ForEach-Object { Write-Host "    $_" }
        }
        
        if ($warningCount -gt 0) {
            Write-Warning "Cppcheck found $warningCount warnings"
            if ($Strict) {
                $global:ValidationErrors += $warningCount
            }
        }
    } else {
        Write-Success "Cppcheck passed"
    }
}

# ============================================================================
# 7. Include What You Use Check
# ============================================================================

Write-Section "Include Dependency Check"

# Check for common antipatterns
$badIncludes = @(
    @{Pattern='#include\s*<bits/stdc\+\+\.h>'; Message='Do not use <bits/stdc++.h>'},
    @{Pattern='using\s+namespace\s+std\s*;'; Message='Avoid "using namespace std" in headers'},
    @{Pattern='#include\s*"\.\.\/\.\.'; Message='Avoid deep relative includes'}
)

$includeIssues = 0
foreach ($pattern in $badIncludes) {
    $matches = Select-String -Path src/**/*.cpp,include/**/*.h -Pattern $pattern.Pattern
    if ($matches) {
        Write-Warning $pattern.Message
        $includeIssues += $matches.Count
        $matches | Select-Object -First 3 | ForEach-Object {
            Write-Host "    $($_.Filename):$($_.LineNumber)"
        }
    }
}

if ($includeIssues -eq 0) {
    Write-Success "Include patterns look good"
}

# ============================================================================
# 8. TODO/FIXME/HACK Detection
# ============================================================================

Write-Section "Code Annotation Check"

$annotations = @("TODO", "FIXME", "HACK", "XXX", "BUG")
$annotationCounts = @{}

foreach ($ann in $annotations) {
    $matches = Select-String -Path src/**/*.cpp,include/**/*.h -Pattern "\b$ann\b"
    $annotationCounts[$ann] = $matches.Count
    if ($matches.Count -gt 0) {
        Write-Host "  ${ColorYellow}$ann${ColorReset}: $($matches.Count)"
    }
}

$criticalAnnotations = $annotationCounts["FIXME"] + $annotationCounts["BUG"]
if ($criticalAnnotations -gt 5) {
    Write-Warning "High number of critical annotations: $criticalAnnotations"
}

# ============================================================================
# 9. Memory Safety Checks
# ============================================================================

Write-Section "Memory Safety Patterns"

$unsafePatterns = @(
    @{Pattern='\bmalloc\s*\('; Message='Raw malloc detected - prefer RAII'},
    @{Pattern='\bfree\s*\('; Message='Raw free detected - prefer RAII'},
    @{Pattern='\bnew\s+\w+\s*\['; Message='Raw array new - prefer std::vector'},
    @{Pattern='\bdelete\s*\['; Message='Raw delete[] - prefer std::vector'},
    @{Pattern='\breinterpret_cast\s*<'; Message='reinterpret_cast usage detected'},
    @{Pattern='\bstrcpy\s*\('; Message='Unsafe strcpy - use std::string'},
    @{Pattern='\bsprintf\s*\('; Message='Unsafe sprintf - use std::format or fmt'}
)

$safetyIssues = 0
foreach ($pattern in $unsafePatterns) {
    $matches = Select-String -Path src/**/*.cpp -Pattern $pattern.Pattern
    if ($matches) {
        Write-Warning "$($pattern.Message): $($matches.Count) occurrences"
        $safetyIssues += $matches.Count
        $matches | Select-Object -First 2 | ForEach-Object {
            Write-Host "    $($_.Filename):$($_.LineNumber)"
        }
    }
}

if ($safetyIssues -eq 0) {
    Write-Success "No unsafe memory patterns detected"
} elseif ($safetyIssues -gt 10 -and $Strict) {
    Write-Error-Custom "Too many unsafe memory patterns: $safetyIssues"
}

# ============================================================================
# 10. Build Configuration Check
# ============================================================================

Write-Section "Build Configuration"

if (Test-Path "CMakeLists.txt") {
    $cmake = Get-Content "CMakeLists.txt" -Raw
    
    # Check for important flags
    $checks = @(
        @{Pattern='CMAKE_CXX_STANDARD\s+20'; Message='C++20 standard enabled'},
        @{Pattern='POSITION_INDEPENDENT_CODE'; Message='PIC enabled'},
        @{Pattern='CMAKE_EXPORT_COMPILE_COMMANDS'; Message='Compile commands export enabled'}
    )
    
    foreach ($check in $checks) {
        if ($cmake -match $check.Pattern) {
            Write-Success $check.Message
        } else {
            Write-Warning "Missing: $($check.Message)"
        }
    }
}

# ============================================================================
# Summary
# ============================================================================

Write-Section "Validation Summary"

Write-Host "  Files analyzed: $totalFiles"
Write-Host "  Lines of code: $totalLines"
Write-Host "  ${ColorRed}Errors${ColorReset}: $global:ValidationErrors"
Write-Host "  ${ColorYellow}Warnings${ColorReset}: $global:ValidationWarnings"

if ($global:ValidationErrors -eq 0) {
    Write-Host "`n${ColorGreen}✓ VALIDATION PASSED${ColorReset} - Ready to build`n"
    exit 0
} else {
    Write-Host "`n${ColorRed}✗ VALIDATION FAILED${ColorReset} - Fix errors before building`n"
    exit 1
}
