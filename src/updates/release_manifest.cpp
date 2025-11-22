#include "updates/release_manifest.h"
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

namespace themis {
namespace updates {

// ============================================================================
// ReleaseFile Implementation
// ============================================================================

json ReleaseFile::toJson() const {
    json j;
    j["path"] = path;
    j["type"] = type;
    j["sha256_hash"] = sha256_hash;
    j["size_bytes"] = size_bytes;
    
    if (!file_signature.empty()) {
        j["file_signature"] = file_signature;
    }
    
    j["platform"] = platform;
    j["architecture"] = architecture;
    
    if (!permissions.empty()) {
        j["permissions"] = permissions;
    }
    
    if (!download_url.empty()) {
        j["download_url"] = download_url;
    }
    
    if (!metadata.empty()) {
        j["metadata"] = metadata;
    }
    
    return j;
}

std::optional<ReleaseFile> ReleaseFile::fromJson(const json& j) {
    try {
        ReleaseFile file;
        file.path = j.value("path", "");
        file.type = j.value("type", "");
        file.sha256_hash = j.value("sha256_hash", "");
        file.size_bytes = j.value("size_bytes", 0ULL);
        file.file_signature = j.value("file_signature", "");
        file.platform = j.value("platform", "");
        file.architecture = j.value("architecture", "");
        file.permissions = j.value("permissions", "");
        file.download_url = j.value("download_url", "");
        
        if (j.contains("metadata")) {
            file.metadata = j["metadata"];
        }
        
        return file;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// ============================================================================
// ReleaseManifest Implementation
// ============================================================================

json ReleaseManifest::toJson() const {
    json j;
    
    // Release info
    j["version"] = version;
    j["tag_name"] = tag_name;
    j["release_notes"] = release_notes;
    j["is_critical"] = is_critical;
    
    // Convert time_point to ISO 8601 string
    auto time_t_val = std::chrono::system_clock::to_time_t(release_date);
    std::tm tm_val;
    #ifdef _WIN32
        gmtime_s(&tm_val, &time_t_val);
    #else
        gmtime_r(&time_t_val, &tm_val);
    #endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
    j["release_date"] = buf;
    
    // Files
    json files_array = json::array();
    for (const auto& file : files) {
        files_array.push_back(file.toJson());
    }
    j["files"] = files_array;
    
    // Signature & verification
    j["manifest_hash"] = manifest_hash;
    j["signature"] = signature;
    j["signing_certificate"] = signing_certificate;
    j["timestamp_token"] = timestamp_token;
    
    // Metadata
    j["build_commit"] = build_commit;
    j["build_date"] = build_date;
    j["compiler_version"] = compiler_version;
    
    // Dependencies
    j["dependencies"] = dependencies;
    
    // Min upgrade version
    if (!min_upgrade_from.empty()) {
        j["min_upgrade_from"] = min_upgrade_from;
    }
    
    // Schema version
    j["schema_version"] = schema_version;
    
    return j;
}

std::optional<ReleaseManifest> ReleaseManifest::fromJson(const json& j) {
    try {
        ReleaseManifest manifest;
        
        // Release info
        manifest.version = j.value("version", "");
        manifest.tag_name = j.value("tag_name", "");
        manifest.release_notes = j.value("release_notes", "");
        manifest.is_critical = j.value("is_critical", false);
        
        // Parse release date
        if (j.contains("release_date")) {
            std::string date_str = j["release_date"];
            std::tm tm_val = {};
            std::istringstream ss(date_str);
            ss >> std::get_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");
            if (!ss.fail()) {
                #ifdef _WIN32
                    auto time_t_val = _mkgmtime(&tm_val);
                #else
                    auto time_t_val = timegm(&tm_val);
                #endif
                manifest.release_date = std::chrono::system_clock::from_time_t(time_t_val);
            }
        }
        
        // Files
        if (j.contains("files") && j["files"].is_array()) {
            for (const auto& file_json : j["files"]) {
                auto file = ReleaseFile::fromJson(file_json);
                if (file) {
                    manifest.files.push_back(*file);
                }
            }
        }
        
        // Signature & verification
        manifest.manifest_hash = j.value("manifest_hash", "");
        manifest.signature = j.value("signature", "");
        manifest.signing_certificate = j.value("signing_certificate", "");
        manifest.timestamp_token = j.value("timestamp_token", "");
        
        // Metadata
        manifest.build_commit = j.value("build_commit", "");
        manifest.build_date = j.value("build_date", "");
        manifest.compiler_version = j.value("compiler_version", "");
        
        // Dependencies
        if (j.contains("dependencies") && j["dependencies"].is_array()) {
            manifest.dependencies = j["dependencies"].get<std::vector<std::string>>();
        }
        
        // Min upgrade version
        manifest.min_upgrade_from = j.value("min_upgrade_from", "");
        
        // Schema version
        manifest.schema_version = j.value("schema_version", 1);
        
        return manifest;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string ReleaseManifest::calculateHash() const {
    // Create a copy without signature fields for hashing
    json j;
    j["version"] = version;
    j["tag_name"] = tag_name;
    j["release_notes"] = release_notes;
    j["is_critical"] = is_critical;
    
    // Files
    json files_array = json::array();
    for (const auto& file : files) {
        files_array.push_back(file.toJson());
    }
    j["files"] = files_array;
    
    // Metadata
    j["build_commit"] = build_commit;
    j["build_date"] = build_date;
    j["compiler_version"] = compiler_version;
    j["dependencies"] = dependencies;
    j["min_upgrade_from"] = min_upgrade_from;
    j["schema_version"] = schema_version;
    
    // Serialize to string (deterministic)
    std::string content = j.dump();
    
    // Calculate SHA-256
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(content.c_str()),
           content.length(), hash);
    
    // Convert to hex string
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    
    return ss.str();
}

} // namespace updates
} // namespace themis
