#include <iostream>
#include "content/content_policy.h"
#include "content/mime_detector.h"

using namespace themis;
using namespace themis::content;

void testContentPolicy() {
    std::cout << "=== Content Policy Tests ===" << std::endl;
    
    ContentPolicy policy;
    
    // Setup whitelist
    policy.allowed.push_back({"text/plain", 10 * 1024 * 1024, "Text files", ""});
    policy.allowed.push_back({"application/json", 5 * 1024 * 1024, "JSON files", ""});
    
    // Setup blacklist
    policy.denied.push_back({"application/x-executable", 0, "", "Executables blocked"});
    
    // Setup category rules
    policy.category_rules["geo"] = {"geo", true, 1024ULL * 1024 * 1024, "Geo data"};
    policy.category_rules["executable"] = {"executable", false, 0, "Executables blocked"};
    
    // Default policy
    policy.default_max_size = 100 * 1024 * 1024;
    policy.default_action = true;
    
    // Test 1: Whitelist check
    std::cout << "Test 1 - isAllowed('text/plain'): " 
              << (policy.isAllowed("text/plain") ? "PASS" : "FAIL") << std::endl;
    
    // Test 2: Not whitelisted
    std::cout << "Test 2 - !isAllowed('video/mp4'): " 
              << (!policy.isAllowed("video/mp4") ? "PASS" : "FAIL") << std::endl;
    
    // Test 3: Blacklist check
    std::cout << "Test 3 - isDenied('application/x-executable'): " 
              << (policy.isDenied("application/x-executable") ? "PASS" : "FAIL") << std::endl;
    
    // Test 4: Max size for whitelisted type
    std::cout << "Test 4 - getMaxSize('text/plain') == 10MB: " 
              << (policy.getMaxSize("text/plain") == 10 * 1024 * 1024 ? "PASS" : "FAIL") << std::endl;
    
    // Test 5: Category max size
    std::cout << "Test 5 - getCategoryMaxSize('geo') == 1GB: " 
              << (policy.getCategoryMaxSize("geo") == 1024ULL * 1024 * 1024 ? "PASS" : "FAIL") << std::endl;
    
    // Test 6: Denial reason
    std::cout << "Test 6 - getDenialReason('application/x-executable') not empty: " 
              << (!policy.getDenialReason("application/x-executable").empty() ? "PASS" : "FAIL") << std::endl;
}

void testMimeDetector() {
    std::cout << "\n=== MIME Detector Tests ===" << std::endl;
    
    // Create detector without RocksDB (uses internal policy)
    auto detector = std::make_shared<MimeDetector>("", nullptr);
    
    // Test 1: Detect .txt extension
    auto result1 = detector->fromExtension("test.txt");
    std::cout << "Test 1 - fromExtension('test.txt') == 'text/plain': " 
              << (result1 == "text/plain" ? "PASS" : "FAIL") << std::endl;
    
    // Test 2: Detect .json extension
    auto result2 = detector->fromExtension("data.json");
    std::cout << "Test 2 - fromExtension('data.json') == 'application/json': " 
              << (result2 == "application/json" ? "PASS" : "FAIL") << std::endl;
    
    // Test 3: Validate upload - allowed type, valid size
    auto result3 = detector->validateUpload("test.txt", 1024 * 1024);  // 1 MB
    std::cout << "Test 3 - validateUpload('test.txt', 1MB) allowed: " 
              << (result3.allowed ? "PASS" : "FAIL") << std::endl;
    
    // Test 4: Validate upload - size exceeded
    auto result4 = detector->validateUpload("test.txt", 15 * 1024 * 1024);  // 15 MB > 10 MB limit
    std::cout << "Test 4 - validateUpload('test.txt', 15MB) size_exceeded: " 
              << (result4.size_exceeded && !result4.allowed ? "PASS" : "FAIL") << std::endl;
    
    // Test 5: Validate upload - blacklisted executable
    auto result5 = detector->validateUpload("malware.exe", 1024);
    std::cout << "Test 5 - validateUpload('malware.exe', 1KB) blacklisted: " 
              << (result5.blacklisted && !result5.allowed ? "PASS" : "FAIL") << std::endl;
    
    // Test 6: Validate upload - unknown type under default limit
    auto result6 = detector->validateUpload("file.xyz", 50 * 1024 * 1024);  // 50 MB < 100 MB default
    std::cout << "Test 6 - validateUpload('file.xyz', 50MB) allowed (default): " 
              << (result6.allowed ? "PASS" : "FAIL") << std::endl;
    
    // Test 7: Validate upload - unknown type exceeds default limit
    auto result7 = detector->validateUpload("file.xyz", 150 * 1024 * 1024);  // 150 MB > 100 MB default
    std::cout << "Test 7 - validateUpload('file.xyz', 150MB) size_exceeded (default): " 
              << (result7.size_exceeded && !result7.allowed ? "PASS" : "FAIL") << std::endl;
    
    // Test 8: Case-insensitive extension
    auto result8a = detector->fromExtension("TEST.TXT");
    auto result8b = detector->fromExtension("test.txt");
    std::cout << "Test 8 - Case-insensitive extension matching: " 
              << (result8a == result8b ? "PASS" : "FAIL") << std::endl;
    
    // Test 9: Double extension (.tar.gz)
    auto result9 = detector->fromExtension("archive.tar.gz");
    std::cout << "Test 9 - fromExtension('archive.tar.gz') == 'application/gzip': " 
              << (result9 == "application/gzip" ? "PASS" : "FAIL") << std::endl;
    
    // Test 10: Zero-size file
    auto result10 = detector->validateUpload("empty.txt", 0);
    std::cout << "Test 10 - validateUpload('empty.txt', 0B) allowed: " 
              << (result10.allowed ? "PASS" : "FAIL") << std::endl;
}

int main() {
    std::cout << "Content Policy System - Manual Tests\n" << std::endl;
    
    try {
        testContentPolicy();
        testMimeDetector();
        
        std::cout << "\n=== All Manual Tests Completed ===" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << std::endl;
        return 1;
    }
}
