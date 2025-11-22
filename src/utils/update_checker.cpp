#include "utils/update_checker.h"
#include "utils/logger.h"
#include <algorithm>
#include <regex>
#include <sstream>
#include <variant>

#ifdef THEMIS_ENABLE_CURL
#include <curl/curl.h>
#endif

namespace themis {
namespace utils {

// ============================================================================
// Version Implementation
// ============================================================================

std::optional<Version> Version::parse(const std::string& version_str) {
    // Match semantic versioning: v?major.minor.patch[-prerelease][+build]
    // Examples: "1.2.3", "v1.2.3", "1.2.3-beta", "1.2.3-rc.1+build.123"
    static const std::regex version_regex(
        R"(^v?(\d+)\.(\d+)\.(\d+)(?:-([a-zA-Z0-9.-]+))?(?:\+([a-zA-Z0-9.-]+))?$)"
    );
    
    std::smatch matches;
    if (!std::regex_match(version_str, matches, version_regex)) {
        return std::nullopt;
    }
    
    Version v;
    v.major = std::stoi(matches[1].str());
    v.minor = std::stoi(matches[2].str());
    v.patch = std::stoi(matches[3].str());
    
    if (matches[4].matched) {
        v.prerelease = matches[4].str();
    }
    if (matches[5].matched) {
        v.build = matches[5].str();
    }
    
    return v;
}

std::string Version::toString() const {
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    if (!prerelease.empty()) {
        oss << "-" << prerelease;
    }
    if (!build.empty()) {
        oss << "+" << build;
    }
    return oss.str();
}

bool Version::operator<(const Version& other) const {
    // Compare major.minor.patch first
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    if (patch != other.patch) return patch < other.patch;
    
    // Prerelease versions have lower precedence than normal versions
    // 1.0.0-alpha < 1.0.0
    if (prerelease.empty() && !other.prerelease.empty()) return false;
    if (!prerelease.empty() && other.prerelease.empty()) return true;
    
    // Compare prerelease strings lexicographically
    return prerelease < other.prerelease;
}

bool Version::operator>(const Version& other) const {
    return other < *this;
}

bool Version::operator==(const Version& other) const {
    return major == other.major && 
           minor == other.minor && 
           patch == other.patch && 
           prerelease == other.prerelease;
}

// ============================================================================
// ReleaseInfo Implementation
// ============================================================================

bool ReleaseInfo::isCritical() const {
    // Check for critical keywords in release notes or title
    std::string search_text = name + " " + body;
    std::transform(search_text.begin(), search_text.end(), search_text.begin(), ::tolower);
    
    std::vector<std::string> critical_keywords = {
        "security", "critical", "vulnerability", "cve-",
        "exploit", "patch", "urgent", "hotfix"
    };
    
    for (const auto& keyword : critical_keywords) {
        if (search_text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    
    return critical_patch;
}

std::optional<ReleaseInfo> ReleaseInfo::fromJson(const json& j) {
    try {
        ReleaseInfo info;
        info.tag_name = j.value("tag_name", "");
        info.name = j.value("name", "");
        info.body = j.value("body", "");
        info.published_at = j.value("published_at", "");
        info.html_url = j.value("html_url", "");
        info.prerelease = j.value("prerelease", false);
        info.draft = j.value("draft", false);
        
        // Parse version from tag name
        auto version = Version::parse(info.tag_name);
        if (!version) {
            return std::nullopt;  // Invalid version format
        }
        info.version = *version;
        
        // Check if critical
        info.critical_patch = info.isCritical();
        
        return info;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse release info from JSON: {}", e.what());
        return std::nullopt;
    }
}

// ============================================================================
// UpdateCheckResult Implementation
// ============================================================================

json UpdateCheckResult::toJson() const {
    json j;
    
    // Status
    switch (status) {
        case UpdateStatus::UP_TO_DATE:
            j["status"] = "up_to_date";
            j["message"] = "ThemisDB is up to date";
            break;
        case UpdateStatus::UPDATE_AVAILABLE:
            j["status"] = "update_available";
            j["message"] = "A new version is available";
            break;
        case UpdateStatus::CRITICAL_UPDATE:
            j["status"] = "critical_update";
            j["message"] = "A critical security update is available";
            break;
        case UpdateStatus::CHECK_FAILED:
            j["status"] = "check_failed";
            j["message"] = "Failed to check for updates";
            break;
        case UpdateStatus::CHECKING:
            j["status"] = "checking";
            j["message"] = "Checking for updates...";
            break;
        default:
            j["status"] = "unknown";
            j["message"] = "Update status unknown";
            break;
    }
    
    j["current_version"] = current_version;
    
    if (latest_release) {
        j["latest_release"] = {
            {"version", latest_release->version.toString()},
            {"tag_name", latest_release->tag_name},
            {"name", latest_release->name},
            {"published_at", latest_release->published_at},
            {"url", latest_release->html_url},
            {"prerelease", latest_release->prerelease}
        };
    }
    
    if (latest_critical_release) {
        j["latest_critical_release"] = {
            {"version", latest_critical_release->version.toString()},
            {"tag_name", latest_critical_release->tag_name},
            {"name", latest_critical_release->name},
            {"published_at", latest_critical_release->published_at},
            {"url", latest_critical_release->html_url}
        };
    }
    
    if (!error_message.empty()) {
        j["error"] = error_message;
    }
    
    // Convert time_point to ISO 8601 string
    auto time_t_val = std::chrono::system_clock::to_time_t(last_check_time);
    std::tm tm_val;
    #ifdef _WIN32
        gmtime_s(&tm_val, &time_t_val);
    #else
        gmtime_r(&time_t_val, &tm_val);
    #endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
    j["last_check_time"] = buf;
    
    return j;
}

// ============================================================================
// UpdateCheckerConfig Implementation
// ============================================================================

UpdateCheckerConfig UpdateCheckerConfig::fromJson(const json& j) {
    UpdateCheckerConfig config;
    
    if (j.contains("github_owner")) {
        config.github_owner = j["github_owner"].get<std::string>();
    }
    if (j.contains("github_repo")) {
        config.github_repo = j["github_repo"].get<std::string>();
    }
    if (j.contains("current_version")) {
        config.current_version = j["current_version"].get<std::string>();
    }
    if (j.contains("check_interval_seconds")) {
        config.check_interval = std::chrono::seconds(j["check_interval_seconds"].get<int>());
    }
    if (j.contains("auto_update_enabled")) {
        config.auto_update_enabled = j["auto_update_enabled"].get<bool>();
    }
    if (j.contains("auto_update_critical_only")) {
        config.auto_update_critical_only = j["auto_update_critical_only"].get<bool>();
    }
    if (j.contains("github_api_token")) {
        config.github_api_token = j["github_api_token"].get<std::string>();
    }
    if (j.contains("github_api_url")) {
        config.github_api_url = j["github_api_url"].get<std::string>();
    }
    if (j.contains("proxy_url")) {
        config.proxy_url = j["proxy_url"].get<std::string>();
    }
    
    return config;
}

json UpdateCheckerConfig::toJson() const {
    json j;
    j["github_owner"] = github_owner;
    j["github_repo"] = github_repo;
    j["current_version"] = current_version;
    j["check_interval_seconds"] = check_interval.count();
    j["auto_update_enabled"] = auto_update_enabled;
    j["auto_update_critical_only"] = auto_update_critical_only;
    j["github_api_url"] = github_api_url;
    
    // Don't expose sensitive tokens in JSON
    if (!github_api_token.empty()) {
        j["github_api_token"] = "***";
    }
    if (!proxy_url.empty()) {
        j["proxy_url"] = proxy_url;
    }
    
    return j;
}

// ============================================================================
// UpdateChecker Implementation
// ============================================================================

UpdateChecker::UpdateChecker(const UpdateCheckerConfig& config)
    : config_(config) {
    last_result_.current_version = config_.current_version;
    last_result_.status = UpdateStatus::UNKNOWN;
}

UpdateChecker::~UpdateChecker() {
    stop();
}

void UpdateChecker::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_) {
        LOG_WARN("UpdateChecker already running");
        return;
    }
    
    running_ = true;
    check_thread_ = std::make_unique<std::thread>(&UpdateChecker::checkLoop, this);
    
    LOG_INFO("UpdateChecker started (interval: {}s)", config_.check_interval.count());
}

void UpdateChecker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    
    if (check_thread_ && check_thread_->joinable()) {
        check_thread_->join();
    }
    
    LOG_INFO("UpdateChecker stopped");
}

void UpdateChecker::checkLoop() {
    // Initial check
    checkNow();
    
    while (running_) {
        // Sleep in small increments to allow quick shutdown
        auto interval = config_.check_interval;
        auto sleep_duration = std::chrono::seconds(1);
        auto elapsed = std::chrono::seconds(0);
        
        while (running_ && elapsed < interval) {
            std::this_thread::sleep_for(sleep_duration);
            elapsed += sleep_duration;
        }
        
        if (running_) {
            checkNow();
        }
    }
}

UpdateCheckResult UpdateChecker::checkNow() {
    LOG_INFO("Checking for ThemisDB updates...");
    
    UpdateCheckResult result;
    result.current_version = config_.current_version;
    result.last_check_time = std::chrono::system_clock::now();
    result.status = UpdateStatus::CHECKING;
    
    // Fetch releases from GitHub
    auto releases_result = fetchReleases(10);
    
    if (auto* error = std::get_if<std::string>(&releases_result)) {
        result.status = UpdateStatus::CHECK_FAILED;
        result.error_message = *error;
        LOG_ERROR("Update check failed: {}", *error);
    } else {
        auto& releases = std::get<std::vector<ReleaseInfo>>(releases_result);
        result = analyzeReleases(releases);
        result.last_check_time = std::chrono::system_clock::now();
        
        LOG_INFO("Update check completed: {}", result.toJson()["status"].get<std::string>());
        
        // Notify callback if status changed
        if (update_callback_ && 
            (result.status == UpdateStatus::UPDATE_AVAILABLE || 
             result.status == UpdateStatus::CRITICAL_UPDATE)) {
            update_callback_(result);
        }
    }
    
    // Update last result
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_result_ = result;
    }
    
    return result;
}

UpdateCheckResult UpdateChecker::getLastResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_result_;
}

UpdateCheckerConfig UpdateChecker::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void UpdateChecker::updateConfig(const UpdateCheckerConfig& config) {
    bool was_running = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        was_running = running_;
    }
    
    if (was_running) {
        stop();
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }
    
    if (was_running) {
        start();
    }
}

bool UpdateChecker::isRunning() const {
    return running_;
}

std::variant<std::vector<ReleaseInfo>, std::string> UpdateChecker::fetchReleases(int limit) {
#ifdef THEMIS_ENABLE_CURL
    // Build GitHub API URL
    std::string url = config_.github_api_url + "/repos/" + 
                     config_.github_owner + "/" + config_.github_repo + 
                     "/releases?per_page=" + std::to_string(limit);
    
    auto response = httpGet(url);
    
    if (auto* error = std::get_if<std::string>(&response)) {
        return *error;
    }
    
    auto& json_response = std::get<json>(response);
    
    // Parse releases
    std::vector<ReleaseInfo> releases;
    
    if (!json_response.is_array()) {
        return std::string("Invalid response from GitHub API: expected array");
    }
    
    for (const auto& release_json : json_response) {
        auto release = ReleaseInfo::fromJson(release_json);
        if (release) {
            // Skip drafts
            if (!release->draft) {
                releases.push_back(*release);
            } else {
                LOG_DEBUG("Skipping draft release: {}", release->tag_name);
            }
        } else {
            LOG_DEBUG("Skipping release with invalid format");
        }
    }
    
    return releases;
#else
    return std::string("CURL support not enabled - cannot fetch releases");
#endif
}

#ifdef THEMIS_ENABLE_CURL
// CURL callback for writing response data
static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}
#endif

std::variant<json, std::string> UpdateChecker::httpGet(const std::string& url) {
#ifdef THEMIS_ENABLE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::string("Failed to initialize CURL");
    }
    
    std::string response_data;
    CURLcode res;
    
    // Set options
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ThemisDB-UpdateChecker/1.0");
    
    // Set GitHub API token if provided (for higher rate limits)
    struct curl_slist* headers = nullptr;
    if (!config_.github_api_token.empty()) {
        std::string auth_header = "Authorization: token " + config_.github_api_token;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    // Set proxy if configured
    if (!config_.proxy_url.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, config_.proxy_url.c_str());
    }
    
    // Perform request
    res = curl_easy_perform(curl);
    
    // Check for errors
    std::variant<json, std::string> result;
    if (res != CURLE_OK) {
        result = std::string("CURL error: ") + curl_easy_strerror(res);
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code == 200) {
            try {
                result = json::parse(response_data);
            } catch (const json::exception& e) {
                result = std::string("Failed to parse JSON response: ") + e.what();
            }
        } else {
            result = std::string("HTTP error: ") + std::to_string(http_code);
        }
    }
    
    // Cleanup
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    
    return result;
#else
    return std::string("CURL support not enabled");
#endif
}

UpdateCheckResult UpdateChecker::analyzeReleases(const std::vector<ReleaseInfo>& releases) {
    UpdateCheckResult result;
    result.current_version = config_.current_version;
    
    // Parse current version
    auto current_ver = Version::parse(config_.current_version);
    if (!current_ver) {
        result.status = UpdateStatus::CHECK_FAILED;
        result.error_message = "Invalid current version format: " + config_.current_version;
        return result;
    }
    
    // Find latest release and latest critical release
    std::optional<ReleaseInfo> latest;
    std::optional<ReleaseInfo> latest_critical;
    
    for (const auto& release : releases) {
        // Skip prereleases unless they're critical
        if (release.prerelease && !release.critical_patch) {
            continue;
        }
        
        // Update latest release
        if (!latest || release.version > latest->version) {
            latest = release;
        }
        
        // Update latest critical release
        if (release.critical_patch) {
            if (!latest_critical || release.version > latest_critical->version) {
                latest_critical = release;
            }
        }
    }
    
    result.latest_release = latest;
    result.latest_critical_release = latest_critical;
    
    // Determine status
    if (!latest) {
        result.status = UpdateStatus::UP_TO_DATE;
    } else if (latest->version > *current_ver) {
        if (latest_critical && latest_critical->version > *current_ver) {
            result.status = UpdateStatus::CRITICAL_UPDATE;
        } else {
            result.status = UpdateStatus::UPDATE_AVAILABLE;
        }
    } else {
        result.status = UpdateStatus::UP_TO_DATE;
    }
    
    return result;
}

void UpdateChecker::onUpdateAvailable(std::function<void(const UpdateCheckResult&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    update_callback_ = std::move(callback);
}

} // namespace utils
} // namespace themis
