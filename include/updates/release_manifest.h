#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>

namespace themis {
namespace updates {

using json = nlohmann::json;

/**
 * @brief Information about a single file in a release
 */
struct ReleaseFile {
    // File Identity
    std::string path;                 // Relative path, e.g. "bin/themis_server"
    std::string type;                 // "executable", "library", "config", "data"
    
    // Hash & Size
    std::string sha256_hash;          // SHA-256 hash of the file
    uint64_t size_bytes = 0;          // File size
    
    // Signature
    std::string file_signature;       // Individual file signature
    
    // Platform
    std::string platform;             // "windows", "linux", "macos"
    std::string architecture;         // "x64", "arm64"
    
    // Permissions (Unix)
    std::string permissions;          // e.g. "0755" for executables
    
    // Download Info
    std::string download_url;         // GitHub Release Asset URL
    
    // File-specific metadata
    json metadata;                    // Additional metadata
    
    /**
     * @brief Convert to JSON
     */
    json toJson() const;
    
    /**
     * @brief Parse from JSON
     */
    static std::optional<ReleaseFile> fromJson(const json& j);
};

/**
 * @brief Complete release manifest with all files and signatures
 */
struct ReleaseManifest {
    // Release Info
    std::string version;              // e.g. "1.2.3"
    std::string tag_name;             // e.g. "v1.2.3"
    std::string release_notes;        // Changelog
    std::chrono::system_clock::time_point release_date;
    bool is_critical = false;         // Critical security update?
    
    // Files in this release
    std::vector<ReleaseFile> files;
    
    // Signature & Verification
    std::string manifest_hash;        // SHA-256 of entire manifest
    std::string signature;            // CMS/PKCS#7 signature
    std::string signing_certificate;  // X.509 certificate
    std::string timestamp_token;      // RFC 3161 timestamp
    
    // Metadata
    std::string build_commit;         // Git commit hash
    std::string build_date;           // Build timestamp
    std::string compiler_version;     // Compiler info
    
    // Dependencies
    std::vector<std::string> dependencies;  // Dependencies to other components
    
    // Minimum required version for upgrade
    std::string min_upgrade_from;     // e.g. "1.0.0"
    
    // Schema version
    int schema_version = 1;
    
    /**
     * @brief Convert to JSON
     */
    json toJson() const;
    
    /**
     * @brief Parse from JSON
     */
    static std::optional<ReleaseManifest> fromJson(const json& j);
    
    /**
     * @brief Calculate manifest hash (excluding signature fields)
     */
    std::string calculateHash() const;
};

} // namespace updates
} // namespace themis
