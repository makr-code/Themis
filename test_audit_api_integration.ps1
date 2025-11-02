# Themis Audit API Integration Test
# Tests the full stack: themis_server C++ API → .NET WPF Client

Write-Host "=== Themis Audit API Integration Test ===" -ForegroundColor Cyan
Write-Host ""

$baseUrl = "http://localhost:8765"
$testsPassed = 0
$testsFailed = 0

function Test-Endpoint {
    param(
        [string]$Name,
        [string]$Url,
        [scriptblock]$Validation
    )
    
    Write-Host "Test: $Name" -NoNewline
    try {
        $response = Invoke-WebRequest -Uri $Url -UseBasicParsing -ErrorAction Stop
        $result = & $Validation $response
        if ($result) {
            Write-Host " ✅ PASS" -ForegroundColor Green
            $script:testsPassed++
            return $true
        } else {
            Write-Host " ❌ FAIL (validation failed)" -ForegroundColor Red
            $script:testsFailed++
            return $false
        }
    } catch {
        Write-Host " ❌ FAIL ($($_.Exception.Message))" -ForegroundColor Red
        $script:testsFailed++
        return $false
    }
}

# Test 1: Server Health
Test-Endpoint -Name "Server Health Check" -Url "$baseUrl/health" -Validation {
    param($resp)
    $resp.StatusCode -eq 200 -and $resp.Content -match "healthy"
}

# Test 2: Audit API - Basic Query
Test-Endpoint -Name "GET /api/audit (basic)" -Url "$baseUrl/api/audit?page=1&page_size=10" -Validation {
    param($resp)
    $json = $resp.Content | ConvertFrom-Json
    $resp.StatusCode -eq 200 -and 
    $null -ne $json.entries -and
    $null -ne $json.totalCount -and
    $null -ne $json.page -and
    $null -ne $json.pageSize -and
    $null -ne $json.hasMore
}

# Test 3: Audit API - Pagination
Test-Endpoint -Name "GET /api/audit (page 2)" -Url "$baseUrl/api/audit?page=2&page_size=2" -Validation {
    param($resp)
    $json = $resp.Content | ConvertFrom-Json
    $resp.StatusCode -eq 200 -and $json.page -eq 2 -and $json.pageSize -eq 2
}

# Test 4: Audit API - User Filter
Test-Endpoint -Name "GET /api/audit (filter: user=system)" -Url "$baseUrl/api/audit?user=system&page_size=5" -Validation {
    param($resp)
    $json = $resp.Content | ConvertFrom-Json
    $resp.StatusCode -eq 200 -and ($json.entries | Where-Object { $_.user -ne 'system' }).Count -eq 0
}

# Test 5: Audit API - Date Range
$start = (Get-Date).AddDays(-7).ToString("yyyy-MM-ddTHH:mm:ss")
$end = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
Test-Endpoint -Name "GET /api/audit (date range)" -Url "$baseUrl/api/audit?start=$start&end=$end" -Validation {
    param($resp)
    $resp.StatusCode -eq 200
}

# Test 6: CSV Export
Test-Endpoint -Name "GET /api/audit/export/csv" -Url "$baseUrl/api/audit/export/csv?page_size=50" -Validation {
    param($resp)
    $resp.StatusCode -eq 200 -and
    $resp.Headers.'Content-Type' -eq 'text/csv' -and
    $resp.Content.StartsWith("Id,Timestamp,User,Action,EntityType")
}

# Test 7: Empty Result Set
Test-Endpoint -Name "GET /api/audit (no results)" -Url "$baseUrl/api/audit?user=nonexistent_user_xyz" -Validation {
    param($resp)
    $json = $resp.Content | ConvertFrom-Json
    $resp.StatusCode -eq 200 -and $json.entries.Count -eq 0 -and $json.totalCount -eq 0
}

# Test 8: Large Page Size Limit
Test-Endpoint -Name "GET /api/audit (page_size > 1000 limit)" -Url "$baseUrl/api/audit?page_size=5000" -Validation {
    param($resp)
    $json = $resp.Content | ConvertFrom-Json
    # Should be limited to 1000
    $resp.StatusCode -eq 200 -and $json.pageSize -le 1000
}

Write-Host ""
Write-Host "=== Test Summary ===" -ForegroundColor Cyan
Write-Host "Passed: $testsPassed" -ForegroundColor Green
Write-Host "Failed: $testsFailed" -ForegroundColor Red
Write-Host "Total:  $($testsPassed + $testsFailed)"

if ($testsFailed -eq 0) {
    Write-Host ""
    Write-Host "✅ All tests passed! Integration successful." -ForegroundColor Green
    exit 0
} else {
    Write-Host ""
    Write-Host "❌ Some tests failed. Review errors above." -ForegroundColor Red
    exit 1
}
