#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>
#include <set>
#include <memory>
#include "content/content_policy.h"

namespace themis {
namespace storage {
    class SecuritySignatureManager;
}
namespace content {

/// MIME Type Detector - YAML-configurable
/// Detects MIME types based on file extensions and magic numbers
/// Configuration loaded from config/mime_types.yaml
class MimeDetector {
public:
    /// Constructor with optional custom config path and signature manager
    /// If config_path is empty, uses default config/mime_types.yaml
    /// If sig_mgr is provided, file integrity will be verified before loading
    explicit MimeDetector(const std::string& config_path = "",
                         std::shared_ptr<storage::SecuritySignatureManager> sig_mgr = nullptr);
    
    /// Reload configuration from YAML file
    bool reloadConfig(const std::string& config_path = "");

    /// Detect MIME type from file extension
    std::string fromExtension(std::string_view filename) const;

    /// Detect MIME type from file content (magic numbers)
    std::string fromContent(const std::vector<uint8_t>& data) const;

    /// Detect MIME type using both extension and content
    /// Returns best guess (content detection takes priority if available)
    std::string detect(std::string_view filename, const std::vector<uint8_t>& data) const;

    /// Category checks (using YAML categories configuration)
    bool isText(std::string_view mime_type) const;
    bool isImage(std::string_view mime_type) const;
    bool isVideo(std::string_view mime_type) const;
    bool isAudio(std::string_view mime_type) const;
    bool isArchive(std::string_view mime_type) const;
    bool isDocument(std::string_view mime_type) const;
    bool isGeo(std::string_view mime_type) const;
    bool isThemis(std::string_view mime_type) const;
    bool isExecutable(std::string_view mime_type) const;
    bool isDatabase(std::string_view mime_type) const;
    bool isCad(std::string_view mime_type) const;
    bool isBinaryData(std::string_view mime_type) const;
    
    /// Get all MIME types in a category
    std::vector<std::string> getCategory(const std::string& category_name) const;
    
    /// Returns true if config was verified against DB signature
    bool isConfigVerified() const { return config_verified_; }
    
    /// Validate file upload against policy (whitelist/blacklist + size limits)
    /// Returns ValidationResult with allowed flag and detailed reason if denied
    ValidationResult validateUpload(const std::string& filename, uint64_t file_size) const;

private:
    // Extension -> MIME type mapping
    std::unordered_map<std::string, std::string> ext_to_mime_;

    // Magic number signatures
    struct MagicSignature {
        std::vector<uint8_t> signature;
            std::set<size_t> wildcard_positions;  // Byte positions that can vary
        std::string mime_type;
        size_t offset = 0;  // Offset in file where signature appears
    };
    std::vector<MagicSignature> magic_signatures_;

    // Category -> MIME types mapping
    std::unordered_map<std::string, std::set<std::string>> categories_;
    
    // Content Policy (whitelist/blacklist, size limits)
    ContentPolicy policy_;
    
    // Configuration
    std::string config_path_;
    
    // Security signature manager (optional)
    std::shared_ptr<storage::SecuritySignatureManager> sig_mgr_;
    bool config_verified_ = false;
    
    std::string computeDeterministicHash() const;
    
    // Initialization from YAML
    bool loadYamlConfig(const std::string& config_path);
    std::string getDefaultConfigPath() const;
    bool matchesMagicSignature(const std::vector<uint8_t>& content, 
                               const MagicSignature& sig) const;
    std::string extractExtension(std::string_view filename) const;
    bool isInCategory(std::string_view mime_type, const std::string& category) const;
};

} // namespace content
} // namespace themis
