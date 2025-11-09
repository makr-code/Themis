# Quick validation script for compression metrics endpoint
# Starts server, uploads compressed blob, queries metrics

Write-Host "=== Themis Compression Metrics Test ===" -ForegroundColor Cyan

# Start server in background
Write-Host "`nStarting Themis server..." -ForegroundColor Yellow
$serverProcess = Start-Process -FilePath ".\build\Release\themis_server.exe" -ArgumentList "--port", "8080" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 3

try {
    # Upload a text blob (should compress)
    Write-Host "`nUploading compressible text blob..." -ForegroundColor Yellow
    $textPayload = @{
        content = @{
            mime_type = "text/plain"
            title = "Test Document"
        }
    } | ConvertTo-Json -Compress
    
    $textBlob = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. " * 100
    
    $response = Invoke-RestMethod -Uri "http://localhost:8080/content" `
        -Method POST `
        -Headers @{"Content-Type"="application/json"} `
        -Body $textPayload `
        -ContentType "application/json" `
        -ErrorAction Stop
    
    Write-Host "Content uploaded: $($response.id)" -ForegroundColor Green
    
    # Query /metrics endpoint
    Write-Host "`nFetching /metrics..." -ForegroundColor Yellow
    $metrics = Invoke-RestMethod -Uri "http://localhost:8080/metrics" -Method GET -ErrorAction Stop
    
    # Filter compression-related metrics
    Write-Host "`n=== Compression Metrics ===" -ForegroundColor Cyan
    $metrics -split "`n" | Where-Object { $_ -match "themis_compress" } | ForEach-Object {
        Write-Host $_ -ForegroundColor White
    }
    
    Write-Host "`n=== Test Passed ===" -ForegroundColor Green
    
} catch {
    Write-Host "`nError: $_" -ForegroundColor Red
    Write-Host "Test failed!" -ForegroundColor Red
} finally {
    # Stop server
    Write-Host "`nStopping server..." -ForegroundColor Yellow
    Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
    Write-Host "Done." -ForegroundColor Cyan
}
