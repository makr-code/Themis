#pragma once

#include <string>
#include <chrono>
#include <nlohmann/json.hpp>

namespace themis {
namespace updates {

using json = nlohmann::json;

/**
 * @brief Configuration for update checker and hot-reload system
 */
struct UpdatesConfig {
    // Update Checker Settings
    struct CheckerConfig {
        bool enabled = false;                           // Enable update checker
        std::chrono::seconds check_interval{3600};      // Check interval (default: 1 hour)
        std::string github_owner = "makr-code";
        std::string github_repo = "ThemisDB";
        std::string github_api_url = "https://api.github.com";
        std::string github_api_token;                   // Optional API token for higher rate limits
        std::string proxy_url;                          // Optional proxy
    } checker;
    
    // Auto-Update Settings
    struct AutoUpdateConfig {
        bool enabled = false;                           // Enable auto-update
        bool critical_only = true;                      // Only auto-apply critical security updates
        bool require_approval = true;                   // Require manual approval even for critical updates
        std::chrono::seconds approval_timeout{300};     // Auto-approve after timeout (0 = never)
        
        // Schedule settings
        bool scheduled = false;                         // Use scheduled updates
        std::string schedule_time = "02:00";            // Time to apply updates (HH:MM format)
        std::vector<std::string> schedule_days = {"Sunday"}; // Days of week
    } auto_update;
    
    // Hot-Reload Settings
    struct HotReloadConfig {
        bool enabled = false;                           // Enable hot-reload capability
        std::string download_directory = "/tmp/themis_updates";
        std::string backup_directory = "/var/lib/themisdb/rollback";
        std::string install_directory = ".";
        bool verify_signatures = true;                  // Always verify signatures
        bool create_backup = true;                      // Always create backup
        int keep_rollback_points = 3;                   // Number of rollback points to keep
        
        // Download settings
        int download_timeout_seconds = 300;             // Download timeout per file
        int max_retries = 3;                            // Max download retries
        int retry_delay_seconds = 5;                    // Delay between retries
    } hot_reload;
    
    // Notification Settings
    struct NotificationConfig {
        bool enabled = false;                           // Enable notifications
        std::vector<std::string> on_events = {          // Events to notify on
            "update_available",
            "critical_update",
            "update_applied",
            "update_failed",
            "rollback_performed"
        };
        std::string webhook_url;                        // Webhook URL for notifications
        std::string email_to;                           // Email address for notifications
    } notifications;
    
    /**
     * @brief Load configuration from YAML file
     */
    static UpdatesConfig loadFromYaml(const std::string& yaml_path);
    
    /**
     * @brief Load configuration from JSON
     */
    static UpdatesConfig fromJson(const json& j);
    
    /**
     * @brief Convert to JSON
     */
    json toJson() const;
    
    /**
     * @brief Save configuration to YAML file
     */
    void saveToYaml(const std::string& yaml_path) const;
};

} // namespace updates
} // namespace themis
