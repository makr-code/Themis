# Semantic Cache Manual Test Script
# Run with server already started

Write-Host "`n=== Semantic Cache API Test ===" -ForegroundColor Cyan

# Test 1: PUT - Store LLM response in cache
Write-Host "`n[Test 1] Cache PUT - Store response" -ForegroundColor Yellow
$putBody = @{
    prompt = "What is the capital of France?"
    parameters = @{model = "gpt-4"; temperature = 0.7}
    response = "The capital of France is Paris."
    metadata = @{tokens = 15; cost_usd = 0.001}
    ttl_seconds = 3600
} | ConvertTo-Json -Depth 5

try {
    $putResult = Invoke-RestMethod -Uri "http://localhost:8765/cache/put" -Method POST -Body $putBody -ContentType "application/json"
    Write-Host "✓ PUT Success:" -ForegroundColor Green
    $putResult | ConvertTo-Json
} catch {
    Write-Host "✗ PUT Failed: $_" -ForegroundColor Red
}

# Test 2: QUERY - Retrieve cached response (Hit)
Write-Host "`n[Test 2] Cache QUERY - Cache Hit" -ForegroundColor Yellow
$queryBody = @{
    prompt = "What is the capital of France?"
    parameters = @{model = "gpt-4"; temperature = 0.7}
} | ConvertTo-Json -Depth 5

try {
    $queryResult = Invoke-RestMethod -Uri "http://localhost:8765/cache/query" -Method POST -Body $queryBody -ContentType "application/json"
    Write-Host "✓ QUERY Success (Hit):" -ForegroundColor Green
    $queryResult | ConvertTo-Json
} catch {
    Write-Host "✗ QUERY Failed: $_" -ForegroundColor Red
}

# Test 3: QUERY - Cache Miss
Write-Host "`n[Test 3] Cache QUERY - Cache Miss" -ForegroundColor Yellow
$missBody = @{
    prompt = "What is quantum computing?"
    parameters = @{model = "gpt-3.5"}
} | ConvertTo-Json -Depth 5

try {
    $missResult = Invoke-RestMethod -Uri "http://localhost:8765/cache/query" -Method POST -Body $missBody -ContentType "application/json"
    Write-Host "✓ QUERY Success (Miss):" -ForegroundColor Green
    $missResult | ConvertTo-Json
} catch {
    Write-Host "✗ QUERY Failed: $_" -ForegroundColor Red
}

# Test 4: STATS - Get cache statistics
Write-Host "`n[Test 4] Cache STATS" -ForegroundColor Yellow
try {
    $stats = Invoke-RestMethod -Uri "http://localhost:8765/cache/stats" -Method GET
    Write-Host "✓ STATS Success:" -ForegroundColor Green
    $stats | ConvertTo-Json
    
    Write-Host "`nCache Metrics:" -ForegroundColor Cyan
    Write-Host "  Hit Count:    $($stats.hit_count)"
    Write-Host "  Miss Count:   $($stats.miss_count)"
    Write-Host "  Hit Rate:     $([math]::Round($stats.hit_rate * 100, 2))%"
    Write-Host "  Total Entries: $($stats.total_entries)"
} catch {
    Write-Host "✗ STATS Failed: $_" -ForegroundColor Red
}

Write-Host "`n=== Test Complete ===" -ForegroundColor Cyan
