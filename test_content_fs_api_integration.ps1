# Themis ContentFS API Integration Test
# Verifies binary content endpoints: PUT/GET/HEAD/DELETE with Range support

Write-Host "=== Themis ContentFS API Integration Test ===" -ForegroundColor Cyan
Write-Host ""

$baseUrl = "http://localhost:8765"
$testsPassed = 0
$testsFailed = 0

function Pass($name){ Write-Host ("{0,-40} ✅ PASS" -f $name) -ForegroundColor Green; $script:testsPassed++ }
function Fail($name,$msg){ Write-Host ("{0,-40} ❌ FAIL - {1}" -f $name, $msg) -ForegroundColor Red; $script:testsFailed++ }

function Get-Sha256Hex([byte[]]$data){
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = $sha.ComputeHash($data)
        -join ($hash | ForEach-Object { $_.ToString('x2') })
    } finally {
        $sha.Dispose()
    }
}

# Test data
$pk = "contentfs_api_test_" + [Guid]::NewGuid().ToString('N')
$contentString = "Hello ContentFS! This is a test payload."
$contentBytes = [System.Text.Encoding]::UTF8.GetBytes($contentString)
$checksum = Get-Sha256Hex $contentBytes

# 1) PUT upload (with checksum)
$name = "PUT /contentfs/:pk (upload)"
try {
    $headers = @{ 'X-Checksum-SHA256' = $checksum }
    $resp = Invoke-WebRequest -Uri "$baseUrl/contentfs/$pk" -Method Put -ContentType 'application/octet-stream' -Body $contentBytes -Headers $headers -UseBasicParsing -ErrorAction Stop
    if ($resp.StatusCode -eq 201 -and $resp.Headers.ETag -eq $checksum -and $resp.Headers.Location -eq "/contentfs/$pk") {
        Pass $name
    } else {
        Fail $name "unexpected status/headers (code=$($resp.StatusCode), etag=$($resp.Headers.ETag), location=$($resp.Headers.Location))"
    }
} catch { Fail $name $_.Exception.Message }

# 2) HEAD metadata
$name = "HEAD /contentfs/:pk (metadata)"
try {
    $resp = Invoke-WebRequest -Uri "$baseUrl/contentfs/$pk" -Method Head -UseBasicParsing -ErrorAction Stop
    $len = [int]$resp.Headers.'Content-Length'
    if ($resp.StatusCode -eq 200 -and $len -eq $contentBytes.Length -and $resp.Headers.ETag -eq $checksum -and $resp.Headers.'Accept-Ranges' -eq 'bytes') {
        Pass $name
    } else {
        Fail $name "unexpected headers (len=$len, etag=$($resp.Headers.ETag), accept=$($resp.Headers.'Accept-Ranges'))"
    }
} catch { Fail $name $_.Exception.Message }

# 3) GET full blob
$name = "GET /contentfs/:pk (full)"
try {
    $resp = Invoke-WebRequest -Uri "$baseUrl/contentfs/$pk" -Method Get -UseBasicParsing -ErrorAction Stop
    $body = $resp.Content
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    if ($resp.StatusCode -eq 200 -and $resp.Headers.ETag -eq $checksum -and $bytes.Length -eq $contentBytes.Length -and ($bytes -ceq $contentBytes)) {
        Pass $name
    } else {
        Fail $name "content mismatch or headers (len=$($bytes.Length), etag=$($resp.Headers.ETag))"
    }
} catch { Fail $name $_.Exception.Message }

# 4) GET range (first 5 bytes)
$name = "GET /contentfs/:pk (Range 0-4)"
try {
    $headers = @{ 'Range' = 'bytes=0-4' }
    $resp = Invoke-WebRequest -Uri "$baseUrl/contentfs/$pk" -Method Get -Headers $headers -UseBasicParsing -ErrorAction Stop
    $expected = [System.Text.Encoding]::UTF8.GetBytes($contentString.Substring(0,5))
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($resp.Content)
    $cr = $resp.Headers.'Content-Range'
    if ($resp.StatusCode -eq 206 -and ($bytes -ceq $expected) -and $cr -match "^bytes 0-4/" -and $resp.Headers.'Accept-Ranges' -eq 'bytes') {
        Pass $name
    } else {
        Fail $name "unexpected range response (code=$($resp.StatusCode), cr=$cr, len=$($bytes.Length))"
    }
} catch { Fail $name $_.Exception.Message }

# 5) DELETE
$name = "DELETE /contentfs/:pk"
try {
    $resp = Invoke-WebRequest -Uri "$baseUrl/contentfs/$pk" -Method Delete -UseBasicParsing -ErrorAction Stop
    if ($resp.StatusCode -eq 204) { Pass $name } else { Fail $name "status $($resp.StatusCode)" }
} catch { Fail $name $_.Exception.Message }

# 6) GET after delete should 404
$name = "GET /contentfs/:pk after delete (404)"
try {
    $resp = Invoke-WebRequest -Uri "$baseUrl/contentfs/$pk" -Method Get -UseBasicParsing -ErrorAction Stop
    Fail $name "expected 404, got $($resp.StatusCode)"
} catch {
    if ($_.Exception.Response -and $_.Exception.Response.StatusCode.value__ -eq 404) { Pass $name }
    else { Fail $name $_.Exception.Message }
}

Write-Host ""; Write-Host "=== Test Summary ===" -ForegroundColor Cyan
Write-Host ("Passed: {0}" -f $testsPassed) -ForegroundColor Green
Write-Host ("Failed: {0}" -f $testsFailed) -ForegroundColor Red
Write-Host ("Total:  {0}" -f ($testsPassed + $testsFailed))

if ($testsFailed -eq 0) { Write-Host "`n✅ All ContentFS API tests passed!" -ForegroundColor Green; exit 0 } else { Write-Host "`n❌ Some ContentFS API tests failed." -ForegroundColor Red; exit 1 }
