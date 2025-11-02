# PowerShell script to apply API handler changes to http_server.cpp

$file = "src\server\http_server.cpp"
$content = Get-Content $file -Raw

# 1. Add PIIDetector include after other utils includes
if ($content -notmatch 'utils/pii_detector\.h') {
    $content = $content -replace '(#include "utils/logger_impl\.h")', "`$1`n#include `"utils/pii_detector.h`""
    Write-Host "✓ Added PIIDetector include"
} else {
    Write-Host "✓ PIIDetector include already present"
}

# 2. Add KeyProvider and security includes
if ($content -notmatch 'security/key_provider\.h') {
    $content = $content -replace '(#include "utils/logger_impl\.h")', "`$1`n#include `"security/key_provider.h`"`n#include `"security/encryption.h`"`n#include `"security/audit_logger.h`""
    Write-Host "✓ Added security includes"
} else {
    Write-Host "✓ Security includes already present"
}

# 3. Add API handler includes
if ($content -notmatch 'server/keys_api_handler\.h') {
    $content = $content -replace '(#include "server/http_server\.h")', "`$1`n#include `"server/keys_api_handler.h`"`n#include `"server/classification_api_handler.h`"`n#include `"server/reports_api_handler.h`""
    Write-Host "✓ Added API handler includes"
} else {
    Write-Host "✓ API handler includes already present"
}

# 4. Find the constructor and add member variable initializations
# This is complex - we'll add initialization code after other initializations

# Search for the pattern where we initialize other components
$initPattern = '(?s)(tx_manager_\(std::move\(tx_manager\)\)[\r\n\s]*\{)'

if ($content -match $initPattern) {
    Write-Host "Found constructor initialization block"
    
    # Add the API handler initialization in the constructor body
    # Find the end of the constructor (after other initializations)
    $constructorBodyPattern = '(?s)(tx_manager_\(std::move\(tx_manager\)\)[\r\n\s]*\{)(.*?)(\/\/ Initialize.*?|THEMIS_INFO)'
    
    if ($content -match '(?s)(tx_manager_\(std::move\(tx_manager\)\)[\r\n\s]*\{[\s\S]*?)(\n\s*\/\/ |THEMIS_INFO)') {
        # Find where to insert the API initialization code
        # Look for existing THEMIS_INFO or initialization comments
        
        # Insert initialization code before any existing logs
        $insertPoint = $content.IndexOf('THEMIS_INFO("HttpServer initialized')
        
        if ($insertPoint -gt 0) {
            $beforeInit = $content.Substring(0, $insertPoint)
            $afterInit = $content.Substring($insertPoint)
            
            # Build initialization block
            $apiInit = @"

    // Initialize security components
    auto pki_client = std::make_shared<themis::security::VCCPKIClient>(
        "config/keys/ca-cert.pem",
        "config/keys/server-cert.pem",
        "config/keys/server-key.pem"
    );
    
    auto key_provider = std::make_shared<themis::security::KeyProvider>(pki_client);
    key_provider->initialize();
    THEMIS_INFO("KeyProvider initialized");
    
    // Initialize Keys API Handler with KeyProvider
    keys_api_ = std::make_unique<themis::server::KeysApiHandler>(key_provider);
    THEMIS_INFO("Keys API Handler initialized");
    
    // Initialize PII Detector for Classification API
    auto pii_detector = std::make_shared<themis::utils::PIIDetector>("config/pii_patterns.yaml", pki_client);
    THEMIS_INFO("PII Detector initialized with config/pii_patterns.yaml");
    
    // Initialize Classification API Handler with PIIDetector
    classification_api_ = std::make_unique<themis::server::ClassificationApiHandler>(pii_detector);
    THEMIS_INFO("Classification API Handler initialized");
    
    // Initialize Reports API Handler
    reports_api_ = std::make_unique<themis::server::ReportsApiHandler>();
    THEMIS_INFO("Reports API Handler initialized");

"@
            
            $content = $beforeInit + $apiInit + $afterInit
            Write-Host "✓ Added API initialization code"
        } else {
            Write-Host "⚠ Could not find insertion point (THEMIS_INFO log)"
        }
    }
} else {
    Write-Host "⚠ Could not find constructor pattern"
}

# Save the modified content
$content | Set-Content $file -NoNewline
Write-Host "`n✓ Changes applied to $file"
Write-Host "Please review the file before building."
