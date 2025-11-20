#include <gtest/gtest.h>
#include "content/content_policy.h"
#include "content/mime_detector.h"
#include "storage/security_signature_manager.h"
#include <memory>

using namespace themis;
using namespace themis::content;

// ============================================================================
// ContentPolicy Unit Tests
// ============================================================================

class ContentPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        policy_ = ContentPolicy();
        
        // Setup whitelist policies
        policy_.allowed.push_back({
            "text/plain",
            10 * 1024 * 1024,  // 10 MB
            "Plain text files",
            ""
        });
        
        policy_.allowed.push_back({
            "application/json",
            5 * 1024 * 1024,  // 5 MB
            "JSON files",
            ""
        });
        
        policy_.allowed.push_back({
            "image/png",
            20 * 1024 * 1024,  // 20 MB
            "PNG images",
            ""
        });
        
        // Setup blacklist policies
        policy_.denied.push_back({
            "application/x-executable",
            0,
            "",
            "Executable files are not allowed"
        });
        
        policy_.denied.push_back({
            "application/x-msdownload",
            0,
            "",
            "Windows executables are not allowed"
        });
        
        // Setup category rules
        policy_.category_rules["geo"] = {
            "geo",
            true,  // allow
            1024ULL * 1024 * 1024,  // 1 GB
            "Geospatial data category"
        };
        
        policy_.category_rules["themis"] = {
            "themis",
            true,  // allow
            2ULL * 1024 * 1024 * 1024,  // 2 GB
            "ThemisDB data category"
        };
        
        policy_.category_rules["executable"] = {
            "executable",
            false,  // deny
            0,
            "Executable files category is blocked"
        };
        
        // Default policy
        policy_.default_max_size = 100 * 1024 * 1024;  // 100 MB
        policy_.default_action = true;  // allow by default
    }

    ContentPolicy policy_;
};

// ============================================================================
// Whitelist Tests
// ============================================================================

TEST_F(ContentPolicyTest, IsAllowed_WhitelistedType) {
    EXPECT_TRUE(policy_.isAllowed("text/plain"));
    EXPECT_TRUE(policy_.isAllowed("application/json"));
    EXPECT_TRUE(policy_.isAllowed("image/png"));
}

TEST_F(ContentPolicyTest, IsAllowed_NotWhitelisted) {
    EXPECT_FALSE(policy_.isAllowed("video/mp4"));
    EXPECT_FALSE(policy_.isAllowed("application/pdf"));
}

TEST_F(ContentPolicyTest, GetMaxSize_WhitelistedType) {
    EXPECT_EQ(policy_.getMaxSize("text/plain"), 10 * 1024 * 1024);
    EXPECT_EQ(policy_.getMaxSize("application/json"), 5 * 1024 * 1024);
    EXPECT_EQ(policy_.getMaxSize("image/png"), 20 * 1024 * 1024);
}

TEST_F(ContentPolicyTest, GetMaxSize_NotWhitelisted) {
    EXPECT_EQ(policy_.getMaxSize("video/mp4"), 0);
}

// ============================================================================
// Blacklist Tests
// ============================================================================

TEST_F(ContentPolicyTest, IsDenied_BlacklistedType) {
    EXPECT_TRUE(policy_.isDenied("application/x-executable"));
    EXPECT_TRUE(policy_.isDenied("application/x-msdownload"));
}

TEST_F(ContentPolicyTest, IsDenied_NotBlacklisted) {
    EXPECT_FALSE(policy_.isDenied("text/plain"));
    EXPECT_FALSE(policy_.isDenied("application/json"));
}

TEST_F(ContentPolicyTest, GetDenialReason_BlacklistedType) {
    EXPECT_EQ(policy_.getDenialReason("application/x-executable"), 
              "Executable files are not allowed");
    EXPECT_EQ(policy_.getDenialReason("application/x-msdownload"), 
              "Windows executables are not allowed");
}

TEST_F(ContentPolicyTest, GetDenialReason_NotBlacklisted) {
    EXPECT_EQ(policy_.getDenialReason("text/plain"), "");
}

// ============================================================================
// Category Rules Tests
// ============================================================================

TEST_F(ContentPolicyTest, GetCategoryMaxSize_ExistingCategory) {
    EXPECT_EQ(policy_.getCategoryMaxSize("geo"), 1024ULL * 1024 * 1024);
    EXPECT_EQ(policy_.getCategoryMaxSize("themis"), 2ULL * 1024 * 1024 * 1024);
    EXPECT_EQ(policy_.getCategoryMaxSize("executable"), 0);
}

TEST_F(ContentPolicyTest, GetCategoryMaxSize_NonExistingCategory) {
    EXPECT_EQ(policy_.getCategoryMaxSize("unknown"), 0);
}

// ============================================================================
// MimeDetector Integration Tests
// ============================================================================

class MimeDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a mock security signature manager (in-memory)
        auto security_mgr = std::make_shared<storage::SecuritySignatureManager>(
            nullptr  // No RocksDB instance - tests will use internal policy
        );
        
        // Create MimeDetector with default config
        detector_ = std::make_shared<MimeDetector>("", security_mgr);
    }

    std::shared_ptr<MimeDetector> detector_;
};

TEST_F(MimeDetectorTest, ValidateUpload_AllowedType_ValidSize) {
    // Test small text file (should be allowed)
    auto result = detector_->validateUpload("test.txt", 1024 * 1024);  // 1 MB
    
    EXPECT_TRUE(result.allowed);
    EXPECT_FALSE(result.blacklisted);
    EXPECT_FALSE(result.size_exceeded);
    EXPECT_EQ(result.mime_type, "text/plain");
}

TEST_F(MimeDetectorTest, ValidateUpload_AllowedType_SizeExceeded) {
    // Test large text file (exceeds 10 MB limit)
    auto result = detector_->validateUpload("test.txt", 15 * 1024 * 1024);  // 15 MB
    
    EXPECT_FALSE(result.allowed);
    EXPECT_FALSE(result.blacklisted);
    EXPECT_TRUE(result.size_exceeded);
    EXPECT_EQ(result.mime_type, "text/plain");
    EXPECT_GT(result.reason.find("exceeds"), 0);
}

TEST_F(MimeDetectorTest, ValidateUpload_BlacklistedType) {
    // Test executable file (should be denied)
    auto result = detector_->validateUpload("malware.exe", 1024);
    
    EXPECT_FALSE(result.allowed);
    EXPECT_TRUE(result.blacklisted);
    EXPECT_FALSE(result.size_exceeded);
    EXPECT_TRUE(result.mime_type.find("executable") != std::string::npos ||
                result.mime_type.find("msdownload") != std::string::npos);
}

TEST_F(MimeDetectorTest, ValidateUpload_UnknownType_DefaultPolicy) {
    // Test file with unknown extension (should use default policy)
    auto result = detector_->validateUpload("file.xyz", 50 * 1024 * 1024);  // 50 MB
    
    EXPECT_TRUE(result.allowed);  // default_action = true
    EXPECT_FALSE(result.blacklisted);
    EXPECT_FALSE(result.size_exceeded);  // under 100 MB default limit
}

TEST_F(MimeDetectorTest, ValidateUpload_UnknownType_ExceedsDefault) {
    // Test large unknown file (exceeds 100 MB default)
    auto result = detector_->validateUpload("file.xyz", 150 * 1024 * 1024);  // 150 MB
    
    EXPECT_FALSE(result.allowed);
    EXPECT_FALSE(result.blacklisted);
    EXPECT_TRUE(result.size_exceeded);
    EXPECT_GT(result.reason.find("default limit"), 0);
}

TEST_F(MimeDetectorTest, ValidateUpload_CategoryRule_Geo) {
    // Test GeoJSON file (geo category, 1 GB limit)
    auto result = detector_->validateUpload("map.geojson", 500 * 1024 * 1024);  // 500 MB
    
    EXPECT_TRUE(result.allowed);
    EXPECT_FALSE(result.size_exceeded);
    EXPECT_EQ(result.mime_type, "application/geo+json");
}

TEST_F(MimeDetectorTest, ValidateUpload_CategoryRule_GeoExceeded) {
    // Test large GeoJSON file (exceeds 1 GB geo category limit)
    auto result = detector_->validateUpload("bigmap.geojson", 1200ULL * 1024 * 1024);  // 1.2 GB
    
    EXPECT_FALSE(result.allowed);
    EXPECT_TRUE(result.size_exceeded);
    EXPECT_EQ(result.mime_type, "application/geo+json");
}

TEST_F(MimeDetectorTest, ValidateUpload_CategoryRule_ThemisDB) {
    // Test ThemisDB file (themis category, 2 GB limit)
    auto result = detector_->validateUpload("data.themisdb", 1500ULL * 1024 * 1024);  // 1.5 GB
    
    EXPECT_TRUE(result.allowed);
    EXPECT_FALSE(result.size_exceeded);
    EXPECT_EQ(result.mime_type, "application/x-themisdb");
}

TEST_F(MimeDetectorTest, ValidateUpload_CategoryRule_Executable_Denied) {
    // Test executable category (should be denied by category rule)
    auto result = detector_->validateUpload("program.exe", 1024);
    
    EXPECT_FALSE(result.allowed);
    EXPECT_TRUE(result.blacklisted);  // category denial should set blacklisted flag
    EXPECT_GT(result.reason.find("denied"), 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(MimeDetectorTest, ValidateUpload_EmptyFilename) {
    auto result = detector_->validateUpload("", 1024);
    
    // Should handle gracefully (likely uses default policy)
    EXPECT_FALSE(result.mime_type.empty());
}

TEST_F(MimeDetectorTest, ValidateUpload_ZeroSize) {
    auto result = detector_->validateUpload("test.txt", 0);
    
    // Empty files should be allowed (under any limit)
    EXPECT_TRUE(result.allowed);
    EXPECT_FALSE(result.size_exceeded);
}

TEST_F(MimeDetectorTest, ValidateUpload_MaxUint64Size) {
    // Test maximum possible file size
    auto result = detector_->validateUpload("test.txt", UINT64_MAX);
    
    EXPECT_FALSE(result.allowed);
    EXPECT_TRUE(result.size_exceeded);
}

// ============================================================================
// Multiple Extension Tests
// ============================================================================

TEST_F(MimeDetectorTest, DetectMimeType_DoubleExtension) {
    // Test .tar.gz double extension
    auto result = detector_->validateUpload("archive.tar.gz", 10 * 1024 * 1024);
    
    EXPECT_EQ(result.mime_type, "application/gzip");
}

TEST_F(MimeDetectorTest, DetectMimeType_CaseInsensitive) {
    // Test uppercase extension
    auto result1 = detector_->validateUpload("TEST.TXT", 1024);
    auto result2 = detector_->validateUpload("test.txt", 1024);
    
    EXPECT_EQ(result1.mime_type, result2.mime_type);
}

// ============================================================================
// Main Function
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
