param(
    [string]$Config = "Release",
    [string]$Filter = "",
    [string]$OutRoot = "build/benchmarks"
)

$ErrorActionPreference = 'Stop'

function Ensure-Dir($path) {
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

# Resolve repo root as the directory that contains this script one level up
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutDir = Join-Path $RepoRoot (Join-Path $OutRoot $Timestamp)
Ensure-Dir $OutDir

$Exes = @(
    @{ Name = 'bench_encryption'; Path = Join-Path $RepoRoot ("build/" + $Config + "/bench_encryption.exe") },
    @{ Name = 'themis_benchmarks'; Path = Join-Path $RepoRoot ("build/" + $Config + "/themis_benchmarks.exe") },
    @{ Name = 'bench_mvcc'; Path = Join-Path $RepoRoot ("build/" + $Config + "/bench_mvcc.exe") },
    @{ Name = 'bench_compression'; Path = Join-Path $RepoRoot ("build/" + $Config + "/bench_compression.exe") },
    @{ Name = 'bench_index_rebuild'; Path = Join-Path $RepoRoot ("build/" + $Config + "/bench_index_rebuild.exe") }
)

$Ran = @()

foreach ($exe in $Exes) {
    if (Test-Path -LiteralPath $exe.Path) {
        Write-Host "Running" $exe.Name "..."
        $jsonOut = Join-Path $OutDir ($exe.Name + ".json")
        $csvOut = Join-Path $OutDir ($exe.Name + ".csv")
        $args = @("--benchmark_counters_tabular=true", "--benchmark_out=$jsonOut", "--benchmark_out_format=json")
        if ($Filter -and $Filter.Trim().Length -gt 0) { $args += "--benchmark_filter=$Filter" }
        
        & $exe.Path @args
        if ($LASTEXITCODE -ne 0) { throw "Benchmark $($exe.Name) exited with code $LASTEXITCODE" }
        
        # Convert JSON to CSV lite summary
        try {
            $data = Get-Content -LiteralPath $jsonOut -Raw | ConvertFrom-Json
            $rows = @()
            foreach ($b in $data.benchmarks) {
                $row = [PSCustomObject]@{
                    benchmark      = $b.name
                    iterations     = $b.iterations
                    real_time      = $b.real_time
                    time_unit      = $b.time_unit
                    cpu_time       = $b.cpu_time
                    bytes_per_sec  = if ($b.bytes_per_second) { [double]$b.bytes_per_second } else { $null }
                    items_per_sec  = if ($b.items_per_second) { [double]$b.items_per_second } else { $null }
                    label          = $b.label
                }
                $rows += $row
            }
            $rows | Export-Csv -LiteralPath $csvOut -NoTypeInformation -Encoding UTF8
            $Ran += [PSCustomObject]@{ Name = $exe.Name; Json = $jsonOut; Csv = $csvOut }
        } catch {
            Write-Warning "Failed to parse JSON for $($exe.Name): $_"
        }
    }
}

# Combined summary
$summaryCsv = Join-Path $OutDir 'summary.csv'
$combined = @()
foreach ($r in $Ran) {
    $csvRows = Import-Csv -LiteralPath $r.Csv
    foreach ($row in $csvRows) {
        $row | Add-Member -NotePropertyName benchmark_suite -NotePropertyValue $r.Name
        $combined += $row
    }
}
if ($combined.Count -gt 0) {
    $combined | Export-Csv -LiteralPath $summaryCsv -NoTypeInformation -Encoding UTF8
}

# Write a tiny README note
$readme = @(
    "Themis Benchmarks",
    "Timestamp: $Timestamp",
    "Output Directory: $OutDir",
    "",
    "Files:",
    " - One JSON per benchmark suite (Google Benchmark JSON)",
    " - One CSV per benchmark suite (compact summary)",
    " - summary.csv (combined)",
    "",
    "Usage:",
    "  PowerShell> .\\benchmarks\\run_benchmarks.ps1 -Config Release -Filter 'Encrypt|Index'",
    ""
) -join "`r`n"
Set-Content -LiteralPath (Join-Path $OutDir 'README.txt') -Value $readme -Encoding UTF8

Write-Host "✅ Completed. Results in $OutDir" -ForegroundColor Green
