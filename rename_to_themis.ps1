# Themis Rebranding Script
# Systematisches Umbenennen von VCCDB/Aether zu Themis

Write-Host "=== Themis Rebranding Script ===" -ForegroundColor Cyan
Write-Host "Renaming VCCDB/Aether to Themis across all project files..." -ForegroundColor Yellow
Write-Host ""

$ErrorActionPreference = "Stop"
$changedFiles = @()

# Function to replace in file with encoding preservation
function Replace-InFile {
    param(
        [string]$FilePath,
        [string]$OldPattern,
        [string]$NewPattern,
        [switch]$Regex
    )
    
    if (-not (Test-Path $FilePath)) {
        return $false
    }
    
    try {
        $content = Get-Content -Path $FilePath -Raw -Encoding UTF8
        $originalContent = $content
        
        if ($Regex) {
            $content = $content -replace $OldPattern, $NewPattern
        } else {
            $content = $content.Replace($OldPattern, $NewPattern)
        }
        
        if ($content -ne $originalContent) {
            Set-Content -Path $FilePath -Value $content -Encoding UTF8 -NoNewline
            Write-Host "  [OK] $FilePath" -ForegroundColor Green
            $script:changedFiles += $FilePath
            return $true
        }
        return $false
    } catch {
        Write-Host "  [ERROR] $FilePath : $_" -ForegroundColor Red
        return $false
    }
}

# Get all relevant files
$cppFiles = Get-ChildItem -Path . -Include *.cpp,*.h,*.hpp -Recurse | Where-Object { 
    $_.FullName -notmatch "\\build\\" -and $_.FullName -notmatch "\\vcpkg_installed\\"
}

$docFiles = Get-ChildItem -Path . -Include *.md -Recurse | Where-Object { 
    $_.FullName -notmatch "\\build\\" -and $_.FullName -notmatch "\\vcpkg_installed\\"
}

$allFiles = $cppFiles + $docFiles + @(
    (Get-Item ".\CMakeLists.txt" -ErrorAction SilentlyContinue),
    (Get-Item ".\vcpkg.json" -ErrorAction SilentlyContinue),
    (Get-Item ".\build.ps1" -ErrorAction SilentlyContinue),
    (Get-Item ".\setup.ps1" -ErrorAction SilentlyContinue)
) | Where-Object { $_ -ne $null }

Write-Host "Processing $($allFiles.Count) files..." -ForegroundColor Cyan
Write-Host ""

# Phase 1: AETHER -> THEMIS (uppercase)
Write-Host "Phase 1: AETHER -> THEMIS..." -ForegroundColor Cyan
foreach ($file in $allFiles) {
    Replace-InFile -FilePath $file.FullName -OldPattern "AETHER" -NewPattern "THEMIS" | Out-Null
}

# Phase 2: Aether -> Themis (title case)
Write-Host "Phase 2: Aether -> Themis..." -ForegroundColor Cyan
foreach ($file in $allFiles) {
    Replace-InFile -FilePath $file.FullName -OldPattern "Aether" -NewPattern "Themis" | Out-Null
}

# Phase 3: aether -> themis (lowercase)
Write-Host "Phase 3: aether -> themis..." -ForegroundColor Cyan
foreach ($file in $allFiles) {
    Replace-InFile -FilePath $file.FullName -OldPattern "aether::" -NewPattern "themis::" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "aether_core" -NewPattern "themis_core" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "aether_server" -NewPattern "themis_server" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "aether_tests" -NewPattern "themis_tests" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "aether_demo" -NewPattern "themis_demo" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "aether_benchmarks" -NewPattern "themis_benchmarks" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "namespace aether" -NewPattern "namespace themis" | Out-Null
}

# Phase 4: VCCDB -> THEMIS (uppercase remaining)
Write-Host "Phase 4: VCCDB -> THEMIS..." -ForegroundColor Cyan
foreach ($file in $allFiles) {
    Replace-InFile -FilePath $file.FullName -OldPattern "VCCDB_INFO" -NewPattern "THEMIS_INFO" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "VCCDB_WARN" -NewPattern "THEMIS_WARN" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "VCCDB_ERROR" -NewPattern "THEMIS_ERROR" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "VCCDB_DEBUG" -NewPattern "THEMIS_DEBUG" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "VCCDB" -NewPattern "THEMIS" | Out-Null
}

# Phase 5: vccdb -> themis (lowercase remaining)
Write-Host "Phase 5: vccdb -> themis..." -ForegroundColor Cyan
foreach ($file in $allFiles) {
    Replace-InFile -FilePath $file.FullName -OldPattern "vccdb::" -NewPattern "themis::" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "namespace vccdb" -NewPattern "namespace themis" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "} // namespace vccdb" -NewPattern "} // namespace themis" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "using namespace vccdb" -NewPattern "using namespace themis" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "vccdb_core" -NewPattern "themis_core" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "vccdb_server" -NewPattern "themis_server" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "vccdb_tests" -NewPattern "themis_tests" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "vccdb_demo" -NewPattern "themis_demo" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "vccdb_benchmarks" -NewPattern "themis_benchmarks" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern '"vccdb"' -NewPattern '"themis"' | Out-Null
}

# Phase 6: VCC descriptions -> Themis descriptions
Write-Host "Phase 6: VCC descriptions -> Themis..." -ForegroundColor Cyan
foreach ($file in $allFiles) {
    Replace-InFile -FilePath $file.FullName -OldPattern "VCC Multi-Model Database" -NewPattern "Themis Multi-Model Database" | Out-Null
    Replace-InFile -FilePath $file.FullName -OldPattern "VCC Database" -NewPattern "Themis Database" | Out-Null
}

# Summary
Write-Host ""
Write-Host "=== Rebranding Summary ===" -ForegroundColor Cyan
$uniqueFiles = $changedFiles | Sort-Object -Unique
Write-Host "Total files modified: $($uniqueFiles.Count)" -ForegroundColor Green
Write-Host ""
if ($uniqueFiles.Count -gt 0) {
    Write-Host "Changed files:" -ForegroundColor Yellow
    $uniqueFiles | ForEach-Object {
        $relativePath = $_.Replace((Get-Location).Path, ".").Replace("\", "/")
        Write-Host "  - $relativePath"
    }
}

Write-Host ""
Write-Host "Rebranding complete! Welcome to Themis! ⚖️" -ForegroundColor Green
Write-Host ""
Write-Host "Next: Clean rebuild with ./build.ps1" -ForegroundColor Yellow
