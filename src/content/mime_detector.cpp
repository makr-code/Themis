#include "content/mime_detector.h"
#include "storage/security_signature_manager.h"
#include <openssl/sha.h>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace themis {
namespace content {

namespace fs = std::filesystem;

MimeDetector::MimeDetector(const std::string& config_path,
                           std::shared_ptr<storage::SecuritySignatureManager> sig_mgr)
    : sig_mgr_(sig_mgr) {
    std::string path = config_path.empty() ? getDefaultConfigPath() : config_path;
    loadYamlConfig(path);
}

bool MimeDetector::reloadConfig(const std::string& config_path) {
    std::string path = config_path.empty() ? getDefaultConfigPath() : config_path;
    return loadYamlConfig(path);
}

std::string MimeDetector::getDefaultConfigPath() const {
    // Try multiple locations
    std::vector<std::string> candidates = {
        "config/mime_types.yaml",
        "../config/mime_types.yaml",
        "../../config/mime_types.yaml",
        "/etc/themis/mime_types.yaml"
    };
    
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    
    return "config/mime_types.yaml";  // Default fallback
}

bool MimeDetector::loadYamlConfig(const std::string& config_path) {
    try {
        if (!fs::exists(config_path)) {
            return false;
        }
        
        // Security: Verify file integrity BEFORE parsing
        config_verified_ = false;
        if (sig_mgr_) {
            std::string resource_id = storage::SecuritySignatureManager::normalizeResourceId(config_path);
            bool verified = sig_mgr_->verifyFile(config_path, resource_id);
            
            if (verified) {
                config_verified_ = true;
            } else {
                // Policy: Continue loading but mark as unverified
                // Alternatively: return false; to reject unverified configs
            }
        }
        
        YAML::Node config = YAML::LoadFile(config_path);
        
        // Clear existing data
        ext_to_mime_.clear();
        magic_signatures_.clear();
        categories_.clear();
        
        // Load extensions
        if (config["extensions"]) {
            for (const auto& kv : config["extensions"]) {
                std::string ext = kv.first.as<std::string>();
                std::string mime = kv.second.as<std::string>();
                
                // Normalize extension (lowercase, no leading dot)
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (!ext.empty() && ext[0] == '.') {
                    ext = ext.substr(1);
                }
                
                ext_to_mime_[ext] = mime;
            }
        }
        
        // Load magic signatures
        if (config["magic_signatures"]) {
            for (const auto& sig_node : config["magic_signatures"]) {
                MagicSignature sig;
                
                // Parse signature bytes
                if (sig_node["signature"]) {
                    for (const auto& byte : sig_node["signature"]) {
                        sig.signature.push_back(static_cast<uint8_t>(byte.as<int>()));
                    }
                }
                
                // Parse wildcard positions (optional)
                if (sig_node["wildcard_positions"]) {
                    for (const auto& pos : sig_node["wildcard_positions"]) {
                        sig.wildcard_positions.insert(pos.as<size_t>());
                    }
                }
                
                // Parse MIME type and offset
                sig.mime_type = sig_node["mime_type"].as<std::string>();
                sig.offset = sig_node["offset"] ? sig_node["offset"].as<size_t>() : 0;
                
                if (!sig.signature.empty() && !sig.mime_type.empty()) {
                    magic_signatures_.push_back(std::move(sig));
                }
            }
        }
        
        // Load categories
        if (config["categories"]) {
            for (const auto& cat_node : config["categories"]) {
                std::string category = cat_node.first.as<std::string>();
                std::set<std::string> mime_types;
                
                for (const auto& mime : cat_node.second) {
                    mime_types.insert(mime.as<std::string>());
                }
                
                categories_[category] = std::move(mime_types);
            }
        }
        
        // Load policies (whitelist/blacklist, size limits)
        if (config["policies"]) {
            YAML::Node policies = config["policies"];
            
            // Default max size (100 MB)
            policy_.default_max_size = policies["default_max_size"] 
                ? policies["default_max_size"].as<uint64_t>() 
                : 104857600;
            
            // Default action (allow/deny)
            std::string default_action = policies["default_action"] 
                ? policies["default_action"].as<std::string>() 
                : "allow";
            policy_.default_action = (default_action == "allow");
            
            // Parse allowed list
            if (policies["allowed"]) {
                for (const auto& entry : policies["allowed"]) {
                    MimePolicy mp;
                    mp.mime_type = entry["mime_type"].as<std::string>();
                    mp.max_size = entry["max_size"] ? entry["max_size"].as<uint64_t>() : policy_.default_max_size;
                    mp.description = entry["description"] ? entry["description"].as<std::string>() : "";
                    mp.reason = "";  // Allowed entries don't need a reason
                    policy_.allowed.push_back(std::move(mp));
                }
            }
            
            // Parse denied list
            if (policies["denied"]) {
                for (const auto& entry : policies["denied"]) {
                    MimePolicy mp;
                    mp.mime_type = entry["mime_type"].as<std::string>();
                    mp.max_size = 0;  // Denied entries have no size limit
                    mp.description = entry["description"] ? entry["description"].as<std::string>() : "";
                    mp.reason = entry["reason"] ? entry["reason"].as<std::string>() : "Denied by policy";
                    policy_.denied.push_back(std::move(mp));
                }
            }
            
            // Parse category rules
            if (policies["category_rules"]) {
                for (const auto& cat_node : policies["category_rules"]) {
                    std::string category = cat_node.first.as<std::string>();
                    CategoryPolicy cp;
                    cp.category = category;
                    
                    std::string action = cat_node.second["action"] 
                        ? cat_node.second["action"].as<std::string>() 
                        : "allow";
                    cp.action = (action == "allow");
                    
                    cp.max_size = cat_node.second["max_size"] 
                        ? cat_node.second["max_size"].as<uint64_t>() 
                        : policy_.default_max_size;
                    
                    cp.reason = cat_node.second["reason"] 
                        ? cat_node.second["reason"].as<std::string>() 
                        : "";
                    
                    policy_.category_rules[category] = std::move(cp);
                }
            }
        }
        
        config_path_ = config_path;
        return true;
        
    } catch (const YAML::Exception&) {
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

std::string MimeDetector::extractExtension(std::string_view filename) const {
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string_view::npos || dot_pos == filename.length() - 1) {
        return "";
    }

    std::string ext(filename.substr(dot_pos + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string MimeDetector::fromExtension(std::string_view filename) const {
    // Check for compound extensions first (e.g., .vpb.json, .metadata.json)
    size_t last_dot = filename.find_last_of('.');
    if (last_dot != std::string_view::npos && last_dot > 0) {
        size_t second_last_dot = filename.find_last_of('.', last_dot - 1);
        if (second_last_dot != std::string_view::npos) {
            std::string compound_ext(filename.substr(second_last_dot + 1));
            std::transform(compound_ext.begin(), compound_ext.end(), compound_ext.begin(), ::tolower);
            
            auto it = ext_to_mime_.find(compound_ext);
            if (it != ext_to_mime_.end()) {
                return it->second;
            }
        }
    }
    
    // Try simple extension
    std::string ext = extractExtension(filename);
    if (ext.empty()) {
        return "application/octet-stream";
    }

    auto it = ext_to_mime_.find(ext);
    if (it != ext_to_mime_.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

bool MimeDetector::matchesMagicSignature(const std::vector<uint8_t>& content,
                                         const MagicSignature& sig) const {
    if (content.size() < sig.offset + sig.signature.size()) {
        return false;
    }
    
    for (size_t i = 0; i < sig.signature.size(); ++i) {
        // Skip wildcard positions
        if (sig.wildcard_positions.count(i) > 0) {
            continue;
        }
        
        if (content[sig.offset + i] != sig.signature[i]) {
            return false;
        }
    }
    
    return true;
}

std::string MimeDetector::fromContent(const std::vector<uint8_t>& data) const {
    if (data.empty()) {
        return "application/octet-stream";
    }

    // Check all magic signatures
    for (const auto& sig : magic_signatures_) {
        if (matchesMagicSignature(data, sig)) {
            return sig.mime_type;
        }
    }

    return "application/octet-stream";
}

std::string MimeDetector::computeDeterministicHash() const {
    // Deterministic serialization of extensions, magic signatures, categories
    std::string buffer;
    
    // Extensions
    std::vector<std::string> ext_lines;
    ext_lines.reserve(ext_to_mime_.size());
    for (const auto& kv : ext_to_mime_) {
        ext_lines.push_back(kv.first + "=" + kv.second);
    }
    std::sort(ext_lines.begin(), ext_lines.end());
    buffer += "[extensions]\n";
    for (const auto& line : ext_lines) buffer += line + "\n";
    
    // Magic signatures
    std::vector<std::string> magic_lines;
    magic_lines.reserve(magic_signatures_.size());
    for (const auto& sig : magic_signatures_) {
        std::string hex;
        hex.reserve(sig.signature.size() * 2);
        for (auto b : sig.signature) {
            char tmp[3];
            snprintf(tmp, sizeof(tmp), "%02x", static_cast<unsigned int>(b));
            hex += tmp;
        }
        std::string wildcards;
        if (!sig.wildcard_positions.empty()) {
            wildcards += ":";
            bool first = true;
            for (auto pos : sig.wildcard_positions) {
                if (!first) wildcards += ",";
                wildcards += std::to_string(pos);
                first = false;
            }
        }
        magic_lines.push_back(sig.mime_type + "@" + std::to_string(sig.offset) + "=" + hex + wildcards);
    }
    std::sort(magic_lines.begin(), magic_lines.end());
    buffer += "[magic]\n";
    for (const auto& line : magic_lines) buffer += line + "\n";
    
    // Categories
    std::vector<std::string> category_lines;
    category_lines.reserve(categories_.size());
    for (const auto& cat : categories_) {
        std::vector<std::string> mimes(cat.second.begin(), cat.second.end());
        std::sort(mimes.begin(), mimes.end());
        std::string joined;
        for (size_t i = 0; i < mimes.size(); ++i) {
            if (i) joined += ",";
            joined += mimes[i];
        }
        category_lines.push_back(cat.first + "=" + joined);
    }
    std::sort(category_lines.begin(), category_lines.end());
    buffer += "[categories]\n";
    for (const auto& line : category_lines) buffer += line + "\n";
    
    // SHA256
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size(), digest);
    char hex_out[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        snprintf(&hex_out[i * 2], 3, "%02x", static_cast<unsigned int>(digest[i]));
    }
    hex_out[SHA256_DIGEST_LENGTH * 2] = '\0';
    return std::string(hex_out);
}

std::string MimeDetector::detect(std::string_view filename, 
                                 const std::vector<uint8_t>& data) const {
    // Try content detection first (more reliable)
    if (!data.empty()) {
        std::string content_type = fromContent(data);
        if (content_type != "application/octet-stream") {
            return content_type;
        }
    }

    // Fall back to extension-based detection
    return fromExtension(filename);
}

bool MimeDetector::isInCategory(std::string_view mime_type, 
                                const std::string& category) const {
    auto it = categories_.find(category);
    if (it == categories_.end()) {
        return false;
    }
    
    return it->second.count(std::string(mime_type)) > 0;
}

bool MimeDetector::isText(std::string_view mime_type) const {
    return isInCategory(mime_type, "text");
}

bool MimeDetector::isImage(std::string_view mime_type) const {
    return isInCategory(mime_type, "image");
}

bool MimeDetector::isVideo(std::string_view mime_type) const {
    return isInCategory(mime_type, "video");
}

bool MimeDetector::isAudio(std::string_view mime_type) const {
    return isInCategory(mime_type, "audio");
}

bool MimeDetector::isArchive(std::string_view mime_type) const {
    return isInCategory(mime_type, "archive");
}

bool MimeDetector::isDocument(std::string_view mime_type) const {
    return isInCategory(mime_type, "document");
}

bool MimeDetector::isGeo(std::string_view mime_type) const {
    return isInCategory(mime_type, "geo");
}

bool MimeDetector::isThemis(std::string_view mime_type) const {
    return isInCategory(mime_type, "themis");
}

bool MimeDetector::isExecutable(std::string_view mime_type) const {
    return isInCategory(mime_type, "executable");
}

bool MimeDetector::isDatabase(std::string_view mime_type) const {
    return isInCategory(mime_type, "database");
}

bool MimeDetector::isCad(std::string_view mime_type) const {
    return isInCategory(mime_type, "cad");
}

bool MimeDetector::isBinaryData(std::string_view mime_type) const {
    return isInCategory(mime_type, "binary_data");
}

std::vector<std::string> MimeDetector::getCategory(const std::string& category_name) const {
    auto it = categories_.find(category_name);
    if (it == categories_.end()) {
        return {};
    }
    
    return std::vector<std::string>(it->second.begin(), it->second.end());
}

ValidationResult MimeDetector::validateUpload(const std::string& filename, 
                                               uint64_t file_size) const {
    ValidationResult result;
    result.file_size = file_size;
    
    // Detect MIME type from filename
    result.mime_type = fromExtension(filename);
    
    // Step 1: Check if explicitly denied (blacklist)
    if (policy_.isDenied(result.mime_type)) {
        result.allowed = false;
        result.blacklisted = true;
        result.reason = policy_.getDenialReason(result.mime_type);
        if (result.reason.empty()) {
            result.reason = "File type '" + result.mime_type + "' is blacklisted";
        }
        return result;
    }
    
    // Step 2: Check if explicitly allowed (whitelist)
    if (policy_.isAllowed(result.mime_type)) {
        uint64_t max_size = policy_.getMaxSize(result.mime_type);
        result.max_allowed_size = max_size;
        
        if (file_size > max_size) {
            result.allowed = false;
            result.size_exceeded = true;
            result.reason = "File size " + std::to_string(file_size) + " exceeds limit " + 
                           std::to_string(max_size) + " for type '" + result.mime_type + "'";
            return result;
        }
        
        result.allowed = true;
        result.reason = "Allowed by whitelist";
        return result;
    }
    
    // Step 3: Check category-based rules
    for (const auto& cat_pair : categories_) {
        const std::string& category = cat_pair.first;
        const auto& mime_set = cat_pair.second;
        
        if (mime_set.count(result.mime_type) > 0) {
            // MIME type belongs to this category
            uint64_t cat_max_size = policy_.getCategoryMaxSize(category);
            
            // Check if category is denied
            auto cat_it = policy_.category_rules.find(category);
            if (cat_it != policy_.category_rules.end()) {
                if (cat_it->second.action == false) {
                    result.allowed = false;
                    result.blacklisted = true;
                    result.reason = "Category '" + category + "' is denied: " + cat_it->second.reason;
                    return result;
                }
                
                // Category is allowed, check size limit
                result.max_allowed_size = cat_max_size;
                if (file_size > cat_max_size) {
                    result.allowed = false;
                    result.size_exceeded = true;
                    result.reason = "File size " + std::to_string(file_size) + " exceeds category limit " + 
                                   std::to_string(cat_max_size) + " for '" + category + "'";
                    return result;
                }
                
                result.allowed = true;
                result.reason = "Allowed by category '" + category + "'";
                return result;
            }
        }
    }
    
    // Step 4: Apply default policy
    if (policy_.default_action) {
        result.max_allowed_size = policy_.default_max_size;
        if (file_size > policy_.default_max_size) {
            result.allowed = false;
            result.size_exceeded = true;
            result.reason = "File size " + std::to_string(file_size) + " exceeds default limit " + 
                           std::to_string(policy_.default_max_size);
            return result;
        }
        
        result.allowed = true;
        result.reason = "Allowed by default policy";
        return result;
    } else {
        result.allowed = false;
        result.not_whitelisted = true;
        result.reason = "File type '" + result.mime_type + "' not in whitelist and default policy is deny";
        return result;
    }
}

} // namespace content
} // namespace themis

