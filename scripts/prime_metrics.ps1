Write-Output 'Generating 50 /health requests to populate latency buckets'
for ($i = 0; $i -lt 50; $i++) {
    curl.exe -s http://localhost:8765/health | Out-Null
    Start-Sleep -Milliseconds 10
}

Write-Output 'Done'

$m = curl.exe -s http://localhost:8765/metrics
if ($m -and $m.Length -gt 0) {
    $m -split "`n" | Select-String -Pattern 'vccdb_latency_bucket_microseconds\{le=\"\+Inf\"\}|vccdb_latency_bucket_microseconds\{le=\"100\"\}|vccdb_latency_count' -AllMatches | ForEach-Object { $_.Line }
} else {
    Write-Output 'no metrics'
}
