#include "server/keys_api_handler.h"
#include "utils/logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace themis { namespace server {

KeysApiHandler::KeysApiHandler(std::shared_ptr<KeyProvider> key_provider)
    : key_provider_(key_provider) {
}

nlohmann::json KeysApiHandler::listKeys() {
    try {
        if (!key_provider_) {
            THEMIS_WARN("Keys API: KeyProvider not initialized, returning empty list");
            return {
                {"items", nlohmann::json::array()},
                {"total", 0}
            };
        }
        
        auto keys = key_provider_->listKeys();
        nlohmann::json items = nlohmann::json::array();
        
        for (const auto& key_meta : keys) {
            // Convert KeyMetadata to API response format
            auto created_time = static_cast<time_t>(key_meta.created_at_ms / 1000);
            auto expires_time = key_meta.expires_at_ms > 0 ? 
                static_cast<time_t>(key_meta.expires_at_ms / 1000) : 0;
            
            std::ostringstream created_str, expires_str;
            created_str << std::put_time(std::gmtime(&created_time), "%Y-%m-%dT%H:%M:%SZ");
            
            std::string status_str = "Active";
            switch (key_meta.status) {
                case KeyStatus::ACTIVE: status_str = "Active"; break;
                case KeyStatus::ROTATING: status_str = "Rotating"; break;
                case KeyStatus::DEPRECATED: status_str = "Deprecated"; break;
                case KeyStatus::DELETED: status_str = "Deleted"; break;
            }
            
            nlohmann::json key_obj = {
                {"id", key_meta.key_id},
                {"version", key_meta.version},
                {"status", status_str},
                {"created", created_str.str()},
                {"algorithm", key_meta.algorithm}
            };
            
            if (key_meta.expires_at_ms > 0) {
                expires_str << std::put_time(std::gmtime(&expires_time), "%Y-%m-%dT%H:%M:%SZ");
                key_obj["expires"] = expires_str.str();
            }
            
            items.push_back(key_obj);
        }
        
        THEMIS_INFO("Keys API: Listed {} keys", items.size());
        
        return {
            {"items", items},
            {"total", static_cast<int>(items.size())}
        };
        
    } catch (const std::exception& ex) {
        THEMIS_ERROR("Keys API listKeys failed: {}", ex.what());
        return {
            {"error", "Internal Server Error"},
            {"message", ex.what()},
            {"status_code", 500}
        };
    }
}

nlohmann::json KeysApiHandler::rotateKey(const std::string& key_id, const nlohmann::json& body) {
    (void)body;
    try {
        if (!key_provider_) {
            THEMIS_ERROR("Keys API: KeyProvider not initialized");
            return {
                {"error", "Service Unavailable"},
                {"message", "Key management service not available"},
                {"status_code", 503}
            };
        }
        
        // Check if key exists
        if (!key_provider_->hasKey(key_id)) {
            THEMIS_WARN("Keys API: Key not found: {}", key_id);
            return {
                {"error", "Not Found"},
                {"message", "Key '" + key_id + "' does not exist"},
                {"status_code", 404}
            };
        }
        
        // Perform rotation
        uint32_t new_version = key_provider_->rotateKey(key_id);
        
        THEMIS_INFO("Keys API: Rotated key '{}' to version {}", key_id, new_version);
        
        return {
            {"success", true},
            {"key_id", key_id},
            {"new_version", new_version},
            {"message", "Key rotated successfully"}
        };
        
    } catch (const std::exception& ex) {
        THEMIS_ERROR("Keys API rotateKey failed for '{}': {}", key_id, ex.what());
        return {
            {"error", "Internal Server Error"},
            {"message", ex.what()},
            {"status_code", 500}
        };
    }
}

}} // namespace themis::server
