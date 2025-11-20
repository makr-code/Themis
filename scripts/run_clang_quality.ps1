<#
.SYNOPSIS
  Lokales Code-Qualitätsskript (Windows PowerShell) für ThemisDB.
.DESCRIPTION
  Führt clang-format (Diff oder Apply) und clang-tidy gegen gestagte oder alle Dateien aus.
.PARAMETER ApplyFormat
  Wendet Format direkt an (statt nur Diff).
.PARAMETER All
  Alle Versionierte Quell-Dateien prüfen statt nur gestagte Änderungen.
.PARAMETER Fix
  clang-tidy mit automatischen Fixes (-fix -format).
.PARAMETER FailOnFormat
  Nicht formatiert? Skript beendet sich mit Exitcode 3.
.PARAMETER Jobs
  Parallelität für clang-tidy (Default 4).
.EXAMPLE
  ./scripts/run_clang_quality.ps1
.EXAMPLE
  ./scripts/run_clang_quality.ps1 -ApplyFormat -FailOnFormat
.EXAMPLE
  ./scripts/run_clang_quality.ps1 -All -Fix -Jobs 8
#>
[CmdletBinding()]
param(
  [switch]$ApplyFormat,
  [switch]$All,
  [switch]$Fix,
  [switch]$FailOnFormat,
  [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'

function Find-Tool($name, [string[]]$alternates) {
  $cmd = Get-Command $name -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Path }
  foreach ($a in $alternates) {
    $cmd = Get-Command $a -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Path }
  }
  Write-Warning "Tool $name nicht gefunden. Installiere via Chocolatey: choco install llvm -y"
  return $null
}

$clangFormat = Find-Tool 'clang-format' @('clang-format.exe')
$clangTidy   = Find-Tool 'clang-tidy'   @('clang-tidy.exe')
if (-not $clangFormat -or -not $clangTidy) {
  Write-Warning 'Abbruch: clang-format oder clang-tidy fehlt.'
  exit 1
}

# Staged oder alle Dateien sammeln
if ($All) {
  $files = git ls-files | Where-Object { $_ -match '\.(c|cpp|h|hpp)$' }
} else {
  $files = git diff --name-only --cached --diff-filter=ACMR | Where-Object { $_ -match '\.(c|cpp|h|hpp)$' }
}

if (-not $files -or $files.Count -eq 0) {
  Write-Host '[INFO] Keine passenden Dateien.'
  exit 0
}

Write-Host "[INFO] Prüfe Format (${files.Count} Dateien)" 
$formatChanged = $false
foreach ($f in $files) {
  if ($ApplyFormat) {
    & $clangFormat -i $f
  } else {
    $original = Get-Content -Raw $f
    $formatted = & $clangFormat $f
    if ($original -ne $formatted) {
      Write-Host "[FORMAT] Abweichung: $f"
      $formatChanged = $true
    }
  }
}

if ($FailOnFormat -and -not $ApplyFormat -and $formatChanged) {
  Write-Error 'Format-Abweichungen erkannt (--FailOnFormat).'
  exit 3
}

Write-Host '[INFO] Starte clang-tidy'
# compile_commands.json erwarten wir z.B. in build-msvc oder build-wsl; heuristik
$compileDBCandidates = @('build-msvc/compile_commands.json','build-wsl/compile_commands.json','build/compile_commands.json')
$compileDB = $compileDBCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $compileDB) {
  Write-Warning 'Keine compile_commands.json gefunden. Bitte CMake mit Export ausführen.'
}

# Parallelisierung (rudimentär): Dateien in Batches
$batchSize = [Math]::Ceiling($files.Count / $Jobs)
$index = 0
while ($index -lt $files.Count) {
  $batch = $files[$index..([Math]::Min($index + $batchSize - 1, $files.Count - 1))]
  $jobsObjs = @()
  foreach ($bf in $batch) {
    $args = @($bf)
    if ($compileDB) { $args += @('-p', (Split-Path $compileDB)) }
    if ($Fix) { $args += @('-fix','-format') }
    $jobsObjs += Start-Job -ScriptBlock { param($exe,$arguments) & $exe $arguments } -ArgumentList $clangTidy,$args
  }
  $jobsObjs | Wait-Job | Receive-Job | ForEach-Object { $_ }
  $jobsObjs | Remove-Job
  $index += $batchSize
}

Write-Host '[INFO] Fertig.'
exit 0
