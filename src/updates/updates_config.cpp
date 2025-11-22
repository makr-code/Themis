#include "updates/updates_config.h"
#include "utils/logger.h"
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace themis {
namespace updates {

UpdatesConfig UpdatesConfig::loadFromYaml(const std::string& yaml_path) {
    try {
        YAML::Node config = YAML::LoadFile(yaml_path);
        UpdatesConfig result;
        
        // Load update checker settings
        if (config["updates"] && config["updates"]["checker"]) {
            auto checker = config["updates"]["checker"];
            result.checker.enabled = checker["enabled"].as<bool>(false);
            result.checker.check_interval = std::chrono::seconds(
                checker["check_interval_seconds"].as<int>(3600)
            );
            result.checker.github_owner = checker["github_owner"].as<std::string>("makr-code");
            result.checker.github_repo = checker["github_repo"].as<std::string>("ThemisDB");
            result.checker.github_api_url = checker["github_api_url"].as<std::string>("https://api.github.com");
            
            if (checker["github_api_token"]) {
                result.checker.github_api_token = checker["github_api_token"].as<std::string>();
            }
            if (checker["proxy_url"]) {
                result.checker.proxy_url = checker["proxy_url"].as<std::string>();
            }
        }
        
        // Load auto-update settings
        if (config["updates"] && config["updates"]["auto_update"]) {
            auto auto_update = config["updates"]["auto_update"];
            result.auto_update.enabled = auto_update["enabled"].as<bool>(false);
            result.auto_update.critical_only = auto_update["critical_only"].as<bool>(true);
            result.auto_update.require_approval = auto_update["require_approval"].as<bool>(true);
            result.auto_update.approval_timeout = std::chrono::seconds(
                auto_update["approval_timeout_seconds"].as<int>(300)
            );
            result.auto_update.scheduled = auto_update["scheduled"].as<bool>(false);
            result.auto_update.schedule_time = auto_update["schedule_time"].as<std::string>("02:00");
            
            if (auto_update["schedule_days"]) {
                result.auto_update.schedule_days.clear();
                for (const auto& day : auto_update["schedule_days"]) {
                    result.auto_update.schedule_days.push_back(day.as<std::string>());
                }
            }
        }
        
        // Load hot-reload settings
        if (config["updates"] && config["updates"]["hot_reload"]) {
            auto hot_reload = config["updates"]["hot_reload"];
            result.hot_reload.enabled = hot_reload["enabled"].as<bool>(false);
            result.hot_reload.download_directory = hot_reload["download_directory"].as<std::string>("/tmp/themis_updates");
            result.hot_reload.backup_directory = hot_reload["backup_directory"].as<std::string>("/var/lib/themisdb/rollback");
            result.hot_reload.install_directory = hot_reload["install_directory"].as<std::string>(".");
            result.hot_reload.verify_signatures = hot_reload["verify_signatures"].as<bool>(true);
            result.hot_reload.create_backup = hot_reload["create_backup"].as<bool>(true);
            result.hot_reload.keep_rollback_points = hot_reload["keep_rollback_points"].as<int>(3);
            result.hot_reload.download_timeout_seconds = hot_reload["download_timeout_seconds"].as<int>(300);
            result.hot_reload.max_retries = hot_reload["max_retries"].as<int>(3);
            result.hot_reload.retry_delay_seconds = hot_reload["retry_delay_seconds"].as<int>(5);
        }
        
        // Load notification settings
        if (config["updates"] && config["updates"]["notifications"]) {
            auto notifications = config["updates"]["notifications"];
            result.notifications.enabled = notifications["enabled"].as<bool>(false);
            
            if (notifications["on_events"]) {
                result.notifications.on_events.clear();
                for (const auto& event : notifications["on_events"]) {
                    result.notifications.on_events.push_back(event.as<std::string>());
                }
            }
            
            if (notifications["webhook_url"]) {
                result.notifications.webhook_url = notifications["webhook_url"].as<std::string>();
            }
            if (notifications["email_to"]) {
                result.notifications.email_to = notifications["email_to"].as<std::string>();
            }
        }
        
        LOG_INFO("Loaded updates configuration from {}", yaml_path);
        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load updates configuration from {}: {}", yaml_path, e.what());
        return UpdatesConfig();  // Return default config
    }
}

UpdatesConfig UpdatesConfig::fromJson(const json& j) {
    UpdatesConfig result;
    
    try {
        // Load update checker settings
        if (j.contains("checker")) {
            auto checker = j["checker"];
            result.checker.enabled = checker.value("enabled", false);
            result.checker.check_interval = std::chrono::seconds(
                checker.value("check_interval_seconds", 3600)
            );
            result.checker.github_owner = checker.value("github_owner", "makr-code");
            result.checker.github_repo = checker.value("github_repo", "ThemisDB");
            result.checker.github_api_url = checker.value("github_api_url", "https://api.github.com");
            result.checker.github_api_token = checker.value("github_api_token", "");
            result.checker.proxy_url = checker.value("proxy_url", "");
        }
        
        // Load auto-update settings
        if (j.contains("auto_update")) {
            auto auto_update = j["auto_update"];
            result.auto_update.enabled = auto_update.value("enabled", false);
            result.auto_update.critical_only = auto_update.value("critical_only", true);
            result.auto_update.require_approval = auto_update.value("require_approval", true);
            result.auto_update.approval_timeout = std::chrono::seconds(
                auto_update.value("approval_timeout_seconds", 300)
            );
            result.auto_update.scheduled = auto_update.value("scheduled", false);
            result.auto_update.schedule_time = auto_update.value("schedule_time", "02:00");
            
            if (auto_update.contains("schedule_days")) {
                result.auto_update.schedule_days = auto_update["schedule_days"].get<std::vector<std::string>>();
            }
        }
        
        // Load hot-reload settings
        if (j.contains("hot_reload")) {
            auto hot_reload = j["hot_reload"];
            result.hot_reload.enabled = hot_reload.value("enabled", false);
            result.hot_reload.download_directory = hot_reload.value("download_directory", "/tmp/themis_updates");
            result.hot_reload.backup_directory = hot_reload.value("backup_directory", "/var/lib/themisdb/rollback");
            result.hot_reload.install_directory = hot_reload.value("install_directory", ".");
            result.hot_reload.verify_signatures = hot_reload.value("verify_signatures", true);
            result.hot_reload.create_backup = hot_reload.value("create_backup", true);
            result.hot_reload.keep_rollback_points = hot_reload.value("keep_rollback_points", 3);
            result.hot_reload.download_timeout_seconds = hot_reload.value("download_timeout_seconds", 300);
            result.hot_reload.max_retries = hot_reload.value("max_retries", 3);
            result.hot_reload.retry_delay_seconds = hot_reload.value("retry_delay_seconds", 5);
        }
        
        // Load notification settings
        if (j.contains("notifications")) {
            auto notifications = j["notifications"];
            result.notifications.enabled = notifications.value("enabled", false);
            
            if (notifications.contains("on_events")) {
                result.notifications.on_events = notifications["on_events"].get<std::vector<std::string>>();
            }
            
            result.notifications.webhook_url = notifications.value("webhook_url", "");
            result.notifications.email_to = notifications.value("email_to", "");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse updates configuration from JSON: {}", e.what());
    }
    
    return result;
}

json UpdatesConfig::toJson() const {
    json j;
    
    // Checker config
    j["checker"]["enabled"] = checker.enabled;
    j["checker"]["check_interval_seconds"] = checker.check_interval.count();
    j["checker"]["github_owner"] = checker.github_owner;
    j["checker"]["github_repo"] = checker.github_repo;
    j["checker"]["github_api_url"] = checker.github_api_url;
    
    // Mask token
    if (!checker.github_api_token.empty()) {
        j["checker"]["github_api_token"] = "***";
    }
    if (!checker.proxy_url.empty()) {
        j["checker"]["proxy_url"] = checker.proxy_url;
    }
    
    // Auto-update config
    j["auto_update"]["enabled"] = auto_update.enabled;
    j["auto_update"]["critical_only"] = auto_update.critical_only;
    j["auto_update"]["require_approval"] = auto_update.require_approval;
    j["auto_update"]["approval_timeout_seconds"] = auto_update.approval_timeout.count();
    j["auto_update"]["scheduled"] = auto_update.scheduled;
    j["auto_update"]["schedule_time"] = auto_update.schedule_time;
    j["auto_update"]["schedule_days"] = auto_update.schedule_days;
    
    // Hot-reload config
    j["hot_reload"]["enabled"] = hot_reload.enabled;
    j["hot_reload"]["download_directory"] = hot_reload.download_directory;
    j["hot_reload"]["backup_directory"] = hot_reload.backup_directory;
    j["hot_reload"]["install_directory"] = hot_reload.install_directory;
    j["hot_reload"]["verify_signatures"] = hot_reload.verify_signatures;
    j["hot_reload"]["create_backup"] = hot_reload.create_backup;
    j["hot_reload"]["keep_rollback_points"] = hot_reload.keep_rollback_points;
    j["hot_reload"]["download_timeout_seconds"] = hot_reload.download_timeout_seconds;
    j["hot_reload"]["max_retries"] = hot_reload.max_retries;
    j["hot_reload"]["retry_delay_seconds"] = hot_reload.retry_delay_seconds;
    
    // Notification config
    j["notifications"]["enabled"] = notifications.enabled;
    j["notifications"]["on_events"] = notifications.on_events;
    if (!notifications.webhook_url.empty()) {
        j["notifications"]["webhook_url"] = notifications.webhook_url;
    }
    if (!notifications.email_to.empty()) {
        j["notifications"]["email_to"] = notifications.email_to;
    }
    
    return j;
}

void UpdatesConfig::saveToYaml(const std::string& yaml_path) const {
    try {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "updates";
        out << YAML::Value << YAML::BeginMap;
        
        // Checker config
        out << YAML::Key << "checker";
        out << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "enabled" << YAML::Value << checker.enabled;
        out << YAML::Key << "check_interval_seconds" << YAML::Value << checker.check_interval.count();
        out << YAML::Key << "github_owner" << YAML::Value << checker.github_owner;
        out << YAML::Key << "github_repo" << YAML::Value << checker.github_repo;
        out << YAML::Key << "github_api_url" << YAML::Value << checker.github_api_url;
        if (!checker.github_api_token.empty()) {
            out << YAML::Key << "github_api_token" << YAML::Value << "***";  // Masked
        }
        if (!checker.proxy_url.empty()) {
            out << YAML::Key << "proxy_url" << YAML::Value << checker.proxy_url;
        }
        out << YAML::EndMap;
        
        // Auto-update config
        out << YAML::Key << "auto_update";
        out << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "enabled" << YAML::Value << auto_update.enabled;
        out << YAML::Key << "critical_only" << YAML::Value << auto_update.critical_only;
        out << YAML::Key << "require_approval" << YAML::Value << auto_update.require_approval;
        out << YAML::Key << "approval_timeout_seconds" << YAML::Value << auto_update.approval_timeout.count();
        out << YAML::Key << "scheduled" << YAML::Value << auto_update.scheduled;
        out << YAML::Key << "schedule_time" << YAML::Value << auto_update.schedule_time;
        out << YAML::Key << "schedule_days" << YAML::Value << YAML::Flow << auto_update.schedule_days;
        out << YAML::EndMap;
        
        // Hot-reload config
        out << YAML::Key << "hot_reload";
        out << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "enabled" << YAML::Value << hot_reload.enabled;
        out << YAML::Key << "download_directory" << YAML::Value << hot_reload.download_directory;
        out << YAML::Key << "backup_directory" << YAML::Value << hot_reload.backup_directory;
        out << YAML::Key << "install_directory" << YAML::Value << hot_reload.install_directory;
        out << YAML::Key << "verify_signatures" << YAML::Value << hot_reload.verify_signatures;
        out << YAML::Key << "create_backup" << YAML::Value << hot_reload.create_backup;
        out << YAML::Key << "keep_rollback_points" << YAML::Value << hot_reload.keep_rollback_points;
        out << YAML::Key << "download_timeout_seconds" << YAML::Value << hot_reload.download_timeout_seconds;
        out << YAML::Key << "max_retries" << YAML::Value << hot_reload.max_retries;
        out << YAML::Key << "retry_delay_seconds" << YAML::Value << hot_reload.retry_delay_seconds;
        out << YAML::EndMap;
        
        // Notification config
        out << YAML::Key << "notifications";
        out << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "enabled" << YAML::Value << notifications.enabled;
        out << YAML::Key << "on_events" << YAML::Value << YAML::Flow << notifications.on_events;
        if (!notifications.webhook_url.empty()) {
            out << YAML::Key << "webhook_url" << YAML::Value << notifications.webhook_url;
        }
        if (!notifications.email_to.empty()) {
            out << YAML::Key << "email_to" << YAML::Value << notifications.email_to;
        }
        out << YAML::EndMap;
        
        out << YAML::EndMap;  // updates
        out << YAML::EndMap;  // root
        
        std::ofstream fout(yaml_path);
        fout << out.c_str();
        
        LOG_INFO("Saved updates configuration to {}", yaml_path);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save updates configuration to {}: {}", yaml_path, e.what());
    }
}

} // namespace updates
} // namespace themis
