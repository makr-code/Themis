#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>

namespace themis {
namespace utils {

using json = nlohmann::json;

/**
 * @brief Represents a software version using semantic versioning
 */
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;  // e.g., "alpha", "beta.1"
    std::string build;       // e.g., build metadata
    
    /**
     * @brief Parse version string (e.g., "1.2.3", "1.2.3-beta", "v1.2.3")
     */
    static std::optional<Version> parse(const std::string& version_str);
    
    /**
     * @brief Convert to string representation
     */
    std::string toString() const;
    
    /**
     * @brief Compare versions (semantic versioning rules)
     */
    bool operator<(const Version& other) const;
    bool operator>(const Version& other) const;
    bool operator==(const Version& other) const;
    bool operator<=(const Version& other) const { return *this < other || *this == other; }
    bool operator>=(const Version& other) const { return *this > other || *this == other; }
    bool operator!=(const Version& other) const { return !(*this == other); }
};

/**
 * @brief Information about a GitHub release
 */
struct ReleaseInfo {
    std::string tag_name;         // e.g., "v1.2.3"
    std::string name;             // Release title
    std::string body;             // Release notes
    Version version;              // Parsed version
    std::string published_at;     // ISO 8601 timestamp
    std::string html_url;         // URL to release page
    bool prerelease = false;      // Is this a prerelease?
    bool draft = false;           // Is this a draft?
    bool critical_patch = false;  // Determined by keywords in body/name
    
    /**
     * @brief Check if this release contains critical security patches
     */
    bool isCritical() const;
    
    /**
     * @brief Parse from GitHub API JSON response
     */
    static std::optional<ReleaseInfo> fromJson(const json& j);
};

/**
 * @brief Update check status
 */
enum class UpdateStatus {
    UP_TO_DATE,           // Running latest version
    UPDATE_AVAILABLE,     // New version available
    CRITICAL_UPDATE,      // Critical security update available
    CHECK_FAILED,         // Failed to check for updates
    CHECKING,             // Currently checking
    UNKNOWN               // Not yet checked
};

/**
 * @brief Result of an update check
 */
struct UpdateCheckResult {
    UpdateStatus status = UpdateStatus::UNKNOWN;
    std::string current_version;
    std::optional<ReleaseInfo> latest_release;
    std::optional<ReleaseInfo> latest_critical_release;
    std::string error_message;
    std::chrono::system_clock::time_point last_check_time;
    
    /**
     * @brief Convert to JSON for HTTP API
     */
    json toJson() const;
};

/**
 * @brief Configuration for UpdateChecker
 */
struct UpdateCheckerConfig {
    std::string github_owner = "makr-code";
    std::string github_repo = "ThemisDB";
    std::string current_version = "1.0.0";  // Will be set from build config
    
    // Check interval
    std::chrono::seconds check_interval{3600};  // Default: 1 hour
    
    // Auto-update settings
    bool auto_update_enabled = false;
    bool auto_update_critical_only = true;  // Only auto-update critical patches
    
    // GitHub API settings
    std::string github_api_token;  // Optional, for higher rate limits
    std::string github_api_url = "https://api.github.com";
    
    // Proxy settings (optional)
    std::string proxy_url;
    
    /**
     * @brief Load from JSON
     */
    static UpdateCheckerConfig fromJson(const json& j);
    
    /**
     * @brief Convert to JSON
     */
    json toJson() const;
};

/**
 * @brief GitHub Update Checker Subsystem
 * 
 * This class provides functionality to:
 * - Periodically check GitHub for new releases
 * - Compare versions using semantic versioning
 * - Identify critical security patches
 * - Expose update status via HTTP API
 * - Support hot-reload for critical patches (future)
 * 
 * Thread-Safety: All public methods are thread-safe
 */
class UpdateChecker {
public:
    /**
     * @brief Construct update checker with configuration
     */
    explicit UpdateChecker(const UpdateCheckerConfig& config);
    
    /**
     * @brief Destructor - stops background checking
     */
    ~UpdateChecker();
    
    // Prevent copying
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;
    
    /**
     * @brief Start periodic background checking
     */
    void start();
    
    /**
     * @brief Stop periodic background checking
     */
    void stop();
    
    /**
     * @brief Check for updates now (blocking)
     * @return Update check result
     */
    UpdateCheckResult checkNow();
    
    /**
     * @brief Get last check result
     */
    UpdateCheckResult getLastResult() const;
    
    /**
     * @brief Get configuration
     */
    UpdateCheckerConfig getConfig() const;
    
    /**
     * @brief Update configuration (stops and restarts if running)
     */
    void updateConfig(const UpdateCheckerConfig& config);
    
    /**
     * @brief Check if checker is running
     */
    bool isRunning() const;
    
    /**
     * @brief Fetch releases from GitHub API
     * @param limit Maximum number of releases to fetch (default 10)
     * @return Vector of releases or error
     */
    std::variant<std::vector<ReleaseInfo>, std::string> fetchReleases(int limit = 10);
    
    /**
     * @brief Register callback for update notifications
     * @param callback Function called when new update is found
     */
    void onUpdateAvailable(std::function<void(const UpdateCheckResult&)> callback);
    
private:
    UpdateCheckerConfig config_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> check_thread_;
    
    UpdateCheckResult last_result_;
    std::function<void(const UpdateCheckResult&)> update_callback_;
    
    /**
     * @brief Background check loop
     */
    void checkLoop();
    
    /**
     * @brief Perform HTTP request to GitHub API
     */
    std::variant<json, std::string> httpGet(const std::string& url);
    
    /**
     * @brief Compare current version with releases
     */
    UpdateCheckResult analyzeReleases(const std::vector<ReleaseInfo>& releases);
};

} // namespace utils
} // namespace themis
