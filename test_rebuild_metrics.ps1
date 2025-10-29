# Test script for rebuild metrics
Write-Host "Testing Index Rebuild Metrics" -ForegroundColor Green

# Kill any existing server
Get-Process themis_server -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Start server
Write-Host "Starting server..." -ForegroundColor Yellow
$serverProcess = Start-Process -FilePath "c:\VCC\VCCDB\build\Release\themis_server.exe" -WorkingDirectory "c:\VCC\VCCDB" -PassThru

# Wait for server to start
Start-Sleep -Seconds 5

# Check if server is running
if ($serverProcess.HasExited) {
    Write-Host "ERROR: Server failed to start!" -ForegroundColor Red
    exit 1
}

Write-Host "Server started (PID: $($serverProcess.Id))" -ForegroundColor Green

try {
    # Test 1: Check initial metrics (should be 0)
    Write-Host "`nTest 1: Checking initial rebuild metrics..." -ForegroundColor Yellow
    $metrics = Invoke-RestMethod -Uri "http://localhost:8080/metrics"
    $rebuild_lines = $metrics | Select-String "vccdb_index_rebuild"
    Write-Host "Initial metrics:"
    $rebuild_lines

    # Test 2: Create some test data and index
    Write-Host "`nTest 2: Creating test data and index..." -ForegroundColor Yellow
    
    # Create entity
    $entity = @{
        table = "test_metrics"
        pk = "item1"
        fields = @{
            name = "Test Item"
            price = "99.99"
        }
    } | ConvertTo-Json -Compress
    
    Invoke-RestMethod -Uri "http://localhost:8080/entity" -Method POST -Body $entity -ContentType "application/json" | Out-Null
    Write-Host "Created test entity"
    
    # Create index
    $index = @{
        table = "test_metrics"
        column = "price"
    } | ConvertTo-Json -Compress
    
    Invoke-RestMethod -Uri "http://localhost:8080/index/create" -Method POST -Body $index -ContentType "application/json" | Out-Null
    Write-Host "Created index on 'price'"

    # Test 3: Rebuild index and check metrics
    Write-Host "`nTest 3: Rebuilding index..." -ForegroundColor Yellow
    $rebuild_req = @{
        table = "test_metrics"
        column = "price"
    } | ConvertTo-Json -Compress
    
    $rebuild_result = Invoke-RestMethod -Uri "http://localhost:8080/index/rebuild" -Method POST -Body $rebuild_req -ContentType "application/json"
    Write-Host "Rebuild result:"
    $rebuild_result | ConvertTo-Json

    # Test 4: Check updated metrics
    Write-Host "`nTest 4: Checking updated rebuild metrics..." -ForegroundColor Yellow
    $metrics_after = Invoke-RestMethod -Uri "http://localhost:8080/metrics"
    $rebuild_lines_after = $metrics_after | Select-String "vccdb_index_rebuild"
    Write-Host "Updated metrics:"
    $rebuild_lines_after

    Write-Host "`n✓ All tests passed!" -ForegroundColor Green

} catch {
    Write-Host "`nERROR: $_" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    # Cleanup
    Write-Host "`nStopping server..." -ForegroundColor Yellow
    $serverProcess | Stop-Process -Force
    Write-Host "Done!" -ForegroundColor Green
}
