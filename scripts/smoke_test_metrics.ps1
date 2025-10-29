param(
  [switch]$StartServer
)

$ErrorActionPreference = 'Stop'

function Invoke-JsonPost($uri, $obj) {
  $body = ($obj | ConvertTo-Json -Compress -Depth 10)
  return Invoke-RestMethod -UseBasicParsing -Method Post -Uri $uri -ContentType 'application/json' -Body $body
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$serverExe = Join-Path $repoRoot 'build/Release/themis_server.exe'
$config   = Join-Path $repoRoot 'config/config.json'
$serverStartedByScript = $false

# Optional: Server starten
if ($StartServer) {
  if (-not (Get-Process themis_server -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path $serverExe)) { throw "Server executable not found at $serverExe" }
    Write-Host "Starting server..."
    Start-Process -FilePath $serverExe -ArgumentList '--config', $config | Out-Null
    Start-Sleep -Seconds 1
    $serverStartedByScript = $true
  } else {
    Write-Host "Server already running."
  }
}

# Healthcheck
$health = Invoke-RestMethod -UseBasicParsing http://localhost:8765/health
if ($health.status -ne 'healthy') { throw "Server not healthy: $($health | ConvertTo-Json -Compress)" }

# Testdaten anlegen
Invoke-RestMethod -UseBasicParsing -Method Put -Uri http://localhost:8765/entities/users:alice -ContentType application/json -Body '{"blob":"{\"name\":\"Alice\",\"age\":30,\"city\":\"Berlin\"}"}' | Out-Null
Invoke-RestMethod -UseBasicParsing -Method Put -Uri http://localhost:8765/entities/users:bob   -ContentType application/json -Body '{"blob":"{\"name\":\"Bob\",\"age\":35,\"city\":\"Berlin\"}"}' | Out-Null

# Range-Index für ORDER BY
try { Invoke-JsonPost 'http://localhost:8765/index/create' @{ table='users'; column='age'; type='range' } | Out-Null } catch { }

# Cursor-Seiten abfragen
$aql = 'FOR u IN users SORT u.age ASC LIMIT 0,1 RETURN u'
$resp1 = Invoke-JsonPost 'http://localhost:8765/query/aql' @{ query=$aql; use_cursor=$true; explain=$true }
$resp2 = Invoke-JsonPost 'http://localhost:8765/query/aql' @{ query=$aql; use_cursor=$true; cursor=$resp1.next_cursor; explain=$true }

# Metrics einsammeln
$metrics = (Invoke-WebRequest -UseBasicParsing http://localhost:8765/metrics).Content

function Get-MetricValue($text, [string]$name) {
  $lines = $text -split "`r?`n"
  $line = $lines | Where-Object { $_ -like "$name *" } | Select-Object -First 1
  if (-not $line) { return $null }
  $parts = $line -split '\s+'
  if ($parts.Length -ge 2) { return [double]$parts[1] } else { return $null }
}

$anchorHits = Get-MetricValue $metrics 'vccdb_cursor_anchor_hits_total'
$rangeSteps = Get-MetricValue $metrics 'vccdb_range_scan_steps_total'
$pageCount  = Get-MetricValue $metrics 'vccdb_page_fetch_time_ms_count'
$pageSum    = Get-MetricValue $metrics 'vccdb_page_fetch_time_ms_sum'

# Ausgabe
Write-Host "AnchorHits=" $anchorHits " RangeSteps=" $rangeSteps " PageCount=" $pageCount " PageSumMs=" $pageSum

# Minimal-Checks
if ($pageCount -lt 2 -or $pageSum -le 0) { throw "Page histogram not incremented as expected." }
if ($anchorHits -lt 1) { throw "Cursor anchor hits not incremented." }
if ($rangeSteps -lt 1) { throw "Range scan steps not incremented." }

Write-Host "Smoke test metrics: PASS"

# Optional: Server stoppen
if ($serverStartedByScript) {
  Get-Process themis_server -ErrorAction SilentlyContinue | Stop-Process -Force
}
