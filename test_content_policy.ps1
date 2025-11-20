# Test Content Policy Validation API
# Tests the /api/content/validate endpoint with various scenarios

$baseUrl = "http://localhost:8080"

Write-Host "=== Content Policy Validation Tests ===" -ForegroundColor Cyan
Write-Host ""

# Test 1: Allowed file (text/plain, 5MB limit)
Write-Host "Test 1: Allowed text file (1MB, within 10MB limit)" -ForegroundColor Yellow
$body1 = @{
    filename = "document.txt"
    file_size = 1048576  # 1 MB
} | ConvertTo-Json

$response1 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body1 -ContentType "application/json" -UseBasicParsing
Write-Host "Status: $($response1.StatusCode)" -ForegroundColor Green
Write-Host "Response: $($response1.Content)"
Write-Host ""

# Test 2: Allowed geo file (GeoJSON, 500MB limit)
Write-Host "Test 2: Allowed GeoJSON (100MB, within 500MB limit)" -ForegroundColor Yellow
$body2 = @{
    filename = "map.geojson"
    file_size = 104857600  # 100 MB
} | ConvertTo-Json

$response2 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body2 -ContentType "application/json" -UseBasicParsing
Write-Host "Status: $($response2.StatusCode)" -ForegroundColor Green
Write-Host "Response: $($response2.Content)"
Write-Host ""

# Test 3: Size exceeded (text file too large)
Write-Host "Test 3: Text file exceeds 10MB limit (20MB)" -ForegroundColor Yellow
$body3 = @{
    filename = "large_document.txt"
    file_size = 20971520  # 20 MB (exceeds 10MB limit)
} | ConvertTo-Json

try {
    $response3 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body3 -ContentType "application/json" -UseBasicParsing
    Write-Host "Status: $($response3.StatusCode)" -ForegroundColor Red
    Write-Host "Response: $($response3.Content)"
} catch {
    Write-Host "Status: 403 Forbidden (expected)" -ForegroundColor Green
    $errorResponse = $_.ErrorDetails.Message
    Write-Host "Response: $errorResponse"
}
Write-Host ""

# Test 4: Blacklisted file (executable)
Write-Host "Test 4: Blacklisted executable file (.exe)" -ForegroundColor Yellow
$body4 = @{
    filename = "malware.exe"
    file_size = 1024  # 1 KB
} | ConvertTo-Json

try {
    $response4 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body4 -ContentType "application/json" -UseBasicParsing
    Write-Host "Status: $($response4.StatusCode)" -ForegroundColor Red
    Write-Host "Response: $($response4.Content)"
} catch {
    Write-Host "Status: 403 Forbidden (expected)" -ForegroundColor Green
    $errorResponse = $_.ErrorDetails.Message
    Write-Host "Response: $errorResponse"
}
Write-Host ""

# Test 5: Blacklisted script (.js)
Write-Host "Test 5: Blacklisted JavaScript file (.js)" -ForegroundColor Yellow
$body5 = @{
    filename = "script.js"
    file_size = 2048  # 2 KB
} | ConvertTo-Json

try {
    $response5 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body5 -ContentType "application/json" -UseBasicParsing
    Write-Host "Status: $($response5.StatusCode)" -ForegroundColor Red
    Write-Host "Response: $($response5.Content)"
} catch {
    Write-Host "Status: 403 Forbidden (expected)" -ForegroundColor Green
    $errorResponse = $_.ErrorDetails.Message
    Write-Host "Response: $errorResponse"
}
Write-Host ""

# Test 6: Themis VPB file (1GB limit)
Write-Host "Test 6: Themis VPB file (500MB, within 1GB limit)" -ForegroundColor Yellow
$body6 = @{
    filename = "building.vpb.json"
    file_size = 524288000  # 500 MB
} | ConvertTo-Json

$response6 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body6 -ContentType "application/json" -UseBasicParsing
Write-Host "Status: $($response6.StatusCode)" -ForegroundColor Green
Write-Host "Response: $($response6.Content)"
Write-Host ""

# Test 7: Parquet file (2GB limit)
Write-Host "Test 7: Parquet file (1.5GB, within 2GB limit)" -ForegroundColor Yellow
$body7 = @{
    filename = "data.parquet"
    file_size = 1610612736  # 1.5 GB
} | ConvertTo-Json

$response7 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body7 -ContentType "application/json" -UseBasicParsing
Write-Host "Status: $($response7.StatusCode)" -ForegroundColor Green
Write-Host "Response: $($response7.Content)"
Write-Host ""

# Test 8: ZIP archive (1GB limit)
Write-Host "Test 8: ZIP archive (800MB, within 1GB limit)" -ForegroundColor Yellow
$body8 = @{
    filename = "backup.zip"
    file_size = 838860800  # 800 MB
} | ConvertTo-Json

$response8 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body8 -ContentType "application/json" -UseBasicParsing
Write-Host "Status: $($response8.StatusCode)" -ForegroundColor Green
Write-Host "Response: $($response8.Content)"
Write-Host ""

# Test 9: Default policy (unknown MIME type)
Write-Host "Test 9: Unknown file type (fallback to default 100MB limit)" -ForegroundColor Yellow
$body9 = @{
    filename = "unknown.xyz"
    file_size = 52428800  # 50 MB (within default 100MB)
} | ConvertTo-Json

$response9 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body9 -ContentType "application/json" -UseBasicParsing
Write-Host "Status: $($response9.StatusCode)" -ForegroundColor Green
Write-Host "Response: $($response9.Content)"
Write-Host ""

# Test 10: Default limit exceeded
Write-Host "Test 10: Unknown file type exceeds default 100MB limit (150MB)" -ForegroundColor Yellow
$body10 = @{
    filename = "large_unknown.xyz"
    file_size = 157286400  # 150 MB (exceeds default 100MB)
} | ConvertTo-Json

try {
    $response10 = Invoke-WebRequest -Uri "$baseUrl/api/content/validate" -Method POST -Body $body10 -ContentType "application/json" -UseBasicParsing
    Write-Host "Status: $($response10.StatusCode)" -ForegroundColor Red
    Write-Host "Response: $($response10.Content)"
} catch {
    Write-Host "Status: 403 Forbidden (expected)" -ForegroundColor Green
    $errorResponse = $_.ErrorDetails.Message
    Write-Host "Response: $errorResponse"
}
Write-Host ""

Write-Host "=== All Tests Complete ===" -ForegroundColor Cyan
