#include <gtest/gtest.h>
#include "utils/update_checker.h"

using namespace themis::utils;

// Version parsing tests
TEST(UpdateCheckerTest, VersionParsing) {
    // Valid versions
    auto v1 = Version::parse("1.2.3");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(v1->major, 1);
    EXPECT_EQ(v1->minor, 2);
    EXPECT_EQ(v1->patch, 3);
    EXPECT_EQ(v1->prerelease, "");
    
    // Version with 'v' prefix
    auto v2 = Version::parse("v2.0.0");
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(v2->major, 2);
    EXPECT_EQ(v2->minor, 0);
    EXPECT_EQ(v2->patch, 0);
    
    // Prerelease version
    auto v3 = Version::parse("1.0.0-beta.1");
    ASSERT_TRUE(v3.has_value());
    EXPECT_EQ(v3->major, 1);
    EXPECT_EQ(v3->minor, 0);
    EXPECT_EQ(v3->patch, 0);
    EXPECT_EQ(v3->prerelease, "beta.1");
    
    // Version with build metadata
    auto v4 = Version::parse("1.0.0+build.123");
    ASSERT_TRUE(v4.has_value());
    EXPECT_EQ(v4->build, "build.123");
    
    // Invalid versions
    EXPECT_FALSE(Version::parse("invalid").has_value());
    EXPECT_FALSE(Version::parse("1.2").has_value());
    EXPECT_FALSE(Version::parse("abc.def.ghi").has_value());
}

// Version comparison tests
TEST(UpdateCheckerTest, VersionComparison) {
    auto v1_0_0 = Version::parse("1.0.0").value();
    auto v1_2_0 = Version::parse("1.2.0").value();
    auto v2_0_0 = Version::parse("2.0.0").value();
    auto v1_0_0_beta = Version::parse("1.0.0-beta").value();
    
    // Basic comparisons
    EXPECT_TRUE(v1_0_0 < v1_2_0);
    EXPECT_TRUE(v1_2_0 < v2_0_0);
    EXPECT_TRUE(v1_0_0 < v2_0_0);
    
    // Prerelease comparison
    EXPECT_TRUE(v1_0_0_beta < v1_0_0);  // prerelease < release
    
    // Equality
    auto v1_0_0_copy = Version::parse("1.0.0").value();
    EXPECT_TRUE(v1_0_0 == v1_0_0_copy);
    EXPECT_FALSE(v1_0_0 == v1_2_0);
    
    // Greater than
    EXPECT_TRUE(v2_0_0 > v1_0_0);
    EXPECT_TRUE(v1_0_0 > v1_0_0_beta);
}

// Version toString tests
TEST(UpdateCheckerTest, VersionToString) {
    EXPECT_EQ(Version::parse("1.2.3")->toString(), "1.2.3");
    EXPECT_EQ(Version::parse("v2.0.0")->toString(), "2.0.0");
    EXPECT_EQ(Version::parse("1.0.0-beta")->toString(), "1.0.0-beta");
    EXPECT_EQ(Version::parse("1.0.0+build")->toString(), "1.0.0+build");
    EXPECT_EQ(Version::parse("1.0.0-rc.1+build.123")->toString(), "1.0.0-rc.1+build.123");
}

// ReleaseInfo critical detection tests
TEST(UpdateCheckerTest, CriticalReleaseDetection) {
    ReleaseInfo release;
    
    // Non-critical release
    release.name = "New Features";
    release.body = "Added new functionality";
    EXPECT_FALSE(release.isCritical());
    
    // Security update
    release.name = "Security Update";
    EXPECT_TRUE(release.isCritical());
    
    release.name = "Fix";
    release.body = "Fixed critical vulnerability in authentication";
    EXPECT_TRUE(release.isCritical());
    
    // CVE mention
    release.body = "Addresses CVE-2024-12345";
    EXPECT_TRUE(release.isCritical());
    
    // Hotfix
    release.name = "Hotfix 1.2.1";
    release.body = "Urgent patch for production";
    EXPECT_TRUE(release.isCritical());
}

// UpdateCheckerConfig tests
TEST(UpdateCheckerTest, ConfigSerialization) {
    UpdateCheckerConfig config;
    config.github_owner = "test-owner";
    config.github_repo = "test-repo";
    config.current_version = "1.0.0";
    config.check_interval = std::chrono::seconds(7200);
    config.auto_update_enabled = true;
    config.github_api_token = "secret-token";
    
    // Test toJson
    auto json = config.toJson();
    EXPECT_EQ(json["github_owner"], "test-owner");
    EXPECT_EQ(json["github_repo"], "test-repo");
    EXPECT_EQ(json["current_version"], "1.0.0");
    EXPECT_EQ(json["check_interval_seconds"], 7200);
    EXPECT_EQ(json["auto_update_enabled"], true);
    
    // Token should be masked
    EXPECT_EQ(json["github_api_token"], "***");
    
    // Test fromJson
    auto config2 = UpdateCheckerConfig::fromJson(json);
    EXPECT_EQ(config2.github_owner, "test-owner");
    EXPECT_EQ(config2.github_repo, "test-repo");
    EXPECT_EQ(config2.current_version, "1.0.0");
    EXPECT_EQ(config2.check_interval.count(), 7200);
    EXPECT_EQ(config2.auto_update_enabled, true);
}

// UpdateCheckResult JSON tests
TEST(UpdateCheckerTest, ResultSerialization) {
    UpdateCheckResult result;
    result.status = UpdateStatus::UPDATE_AVAILABLE;
    result.current_version = "1.0.0";
    
    ReleaseInfo release;
    release.tag_name = "v1.2.0";
    release.name = "Release 1.2.0";
    release.version = Version::parse("1.2.0").value();
    release.published_at = "2025-01-15T10:00:00Z";
    release.html_url = "https://github.com/test/test/releases/tag/v1.2.0";
    result.latest_release = release;
    
    result.last_check_time = std::chrono::system_clock::now();
    
    auto json = result.toJson();
    EXPECT_EQ(json["status"], "update_available");
    EXPECT_EQ(json["current_version"], "1.0.0");
    EXPECT_TRUE(json.contains("latest_release"));
    EXPECT_EQ(json["latest_release"]["version"], "1.2.0");
    EXPECT_EQ(json["latest_release"]["tag_name"], "v1.2.0");
}

// UpdateChecker basic functionality tests
TEST(UpdateCheckerTest, BasicFunctionality) {
    UpdateCheckerConfig config;
    config.current_version = "1.0.0";
    config.check_interval = std::chrono::seconds(3600);
    
    UpdateChecker checker(config);
    
    // Should not be running initially
    EXPECT_FALSE(checker.isRunning());
    
    // Get config
    auto retrieved_config = checker.getConfig();
    EXPECT_EQ(retrieved_config.current_version, "1.0.0");
    
    // Get last result (should be UNKNOWN initially)
    auto result = checker.getLastResult();
    EXPECT_EQ(result.status, UpdateStatus::UNKNOWN);
    EXPECT_EQ(result.current_version, "1.0.0");
}

// Note: Network tests (actual GitHub API calls) should be integration tests
// and require network access, so they're not included here as unit tests.
