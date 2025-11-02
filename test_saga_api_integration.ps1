# SAGA API Integration Test Script
# Tests themis_server SAGA batch verification endpoints

$baseUrl = "http://localhost:8765"
$testResults = @()

function Test-Endpoint {
    param(
        [string]$name,
        [string]$url,
        [string]$method = "GET",
        [object]$body = $null,
        [int]$expectedStatus = 200
    )
    
    Write-Host "`n=== Test: $name ===" -ForegroundColor Cyan
    Write-Host "URL: $url" -ForegroundColor Gray
    
    try {
        $params = @{
            Uri = $url
            Method = $method
            UseBasicParsing = $true
        }
        
        if ($body) {
            $params.Body = ($body | ConvertTo-Json)
            $params.ContentType = "application/json"
        }
        
        $response = Invoke-WebRequest @params
        
        if ($response.StatusCode -eq $expectedStatus) {
            Write-Host "[OK] Status: $($response.StatusCode) (expected $expectedStatus)" -ForegroundColor Green
            
            if ($response.Content) {
                $json = $response.Content | ConvertFrom-Json
                Write-Host "Response preview:" -ForegroundColor Gray
                Write-Host ($json | ConvertTo-Json -Depth 2 | Select-Object -First 20) -ForegroundColor Gray
            }
            
            $script:testResults += @{
                Name = $name
                Status = "PASS"
                StatusCode = $response.StatusCode
            }
            return $json
        } else {
            Write-Host "[FAIL] Status: $($response.StatusCode) (expected $expectedStatus)" -ForegroundColor Red
            $script:testResults += @{
                Name = $name
                Status = "FAIL"
                StatusCode = $response.StatusCode
            }
            return $null
        }
        
    } catch {
        Write-Host "[ERROR] Request failed: $_" -ForegroundColor Red
        $script:testResults += @{
            Name = $name
            Status = "ERROR"
            Error = $_.Exception.Message
        }
        return $null
    }
}

Write-Host "========================================" -ForegroundColor Yellow
Write-Host "SAGA API Integration Tests" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow

# Wait for server to be ready
Write-Host "`nWaiting for server to start..." -ForegroundColor Gray
Start-Sleep -Seconds 3

# Test 1: Server Health Check
Test-Endpoint -name "Server Health Check" `
    -url "$baseUrl/health"

# Test 2: List SAGA Batches
$batches = Test-Endpoint -name "List SAGA Batches" `
    -url "$baseUrl/api/saga/batches"

# Test 3: Flush Current Batch (creates a new batch if needed)
Test-Endpoint -name "Flush Current SAGA Batch" `
    -url "$baseUrl/api/saga/flush" `
    -method "POST"

# Test 4: List Batches Again (should show flushed batch)
$batchesAfterFlush = Test-Endpoint -name "List SAGA Batches (after flush)" `
    -url "$baseUrl/api/saga/batches"

# Test 5: Get Batch Detail (if we have batches)
if ($batchesAfterFlush -and $batchesAfterFlush.batches -and $batchesAfterFlush.batches.Count -gt 0) {
    $batchId = $batchesAfterFlush.batches[0].batch_id
    Write-Host "`nUsing batch_id: $batchId" -ForegroundColor Gray
    
    Test-Endpoint -name "Get SAGA Batch Detail" `
        -url "$baseUrl/api/saga/batch/$batchId"
    
    # Test 6: Verify Batch Signature
    Test-Endpoint -name "Verify SAGA Batch Signature" `
        -url "$baseUrl/api/saga/batch/$batchId/verify" `
        -method "POST"
} else {
    Write-Host "`n[WARNING] No batches available for detail/verify tests" -ForegroundColor Yellow
    $script:testResults += @{
        Name = "Get SAGA Batch Detail"
        Status = "SKIP"
    }
    $script:testResults += @{
        Name = "Verify SAGA Batch Signature"
        Status = "SKIP"
    }
}

# Test 7: Invalid Batch ID (should return error)
Test-Endpoint -name "Get Batch Detail with Invalid ID" `
    -url "$baseUrl/api/saga/batch/invalid_batch_999" `
    -expectedStatus 500  # Should fail

# Summary
Write-Host "`n========================================" -ForegroundColor Yellow
Write-Host "Test Summary" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow

$passed = ($testResults | Where-Object { $_.Status -eq "PASS" }).Count
$failed = ($testResults | Where-Object { $_.Status -eq "FAIL" }).Count
$errors = ($testResults | Where-Object { $_.Status -eq "ERROR" }).Count
$skipped = ($testResults | Where-Object { $_.Status -eq "SKIP" }).Count
$total = $testResults.Count

Write-Host "`nTotal: $total" -ForegroundColor White
Write-Host "Passed: $passed" -ForegroundColor Green
Write-Host "Failed: $failed" -ForegroundColor Red
Write-Host "Errors: $errors" -ForegroundColor Red
Write-Host "Skipped: $skipped" -ForegroundColor Yellow

Write-Host "`nDetailed Results:" -ForegroundColor White
foreach ($result in $testResults) {
    $color = switch ($result.Status) {
        "PASS" { "Green" }
        "FAIL" { "Red" }
        "ERROR" { "Red" }
        "SKIP" { "Yellow" }
    }
    $statusIcon = switch ($result.Status) {
        "PASS" { "[PASS]" }
        "FAIL" { "[FAIL]" }
        "ERROR" { "[ERROR]" }
        "SKIP" { "[SKIP]" }
    }
    Write-Host "  $statusIcon $($result.Name): $($result.Status)" -ForegroundColor $color
}

if ($failed -eq 0 -and $errors -eq 0) {
    Write-Host "`nAll tests passed!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`nSome tests failed!" -ForegroundColor Red
    exit 1
}
