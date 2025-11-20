#include "content/version_manager.h"
#include <chrono>
#include <unordered_map>

namespace themis {
namespace content {

int VersionManager::createVersion(
    const std::string& content_id,
    const std::string& content_hash,
    size_t size_bytes,
    const std::string& author,
    const std::string& comment
) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();

    Version version;
    version.version_number = getLatestVersion(content_id) + 1;
    version.timestamp = timestamp;
    version.author = author;
    version.comment = comment;
    version.content_hash = content_hash;
    version.size_bytes = size_bytes;

    versions_[content_id].push_back(version);
    return version.version_number;
}

std::vector<VersionManager::Version> VersionManager::getVersionHistory(
    const std::string& content_id
) const {
    auto it = versions_.find(content_id);
    if (it == versions_.end()) {
        return {};
    }
    return it->second;
}

std::optional<VersionManager::Version> VersionManager::getVersion(
    const std::string& content_id,
    int version_number
) const {
    auto it = versions_.find(content_id);
    if (it == versions_.end()) {
        return std::nullopt;
    }

    for (const auto& v : it->second) {
        if (v.version_number == version_number) {
            return v;
        }
    }

    return std::nullopt;
}

int VersionManager::getLatestVersion(const std::string& content_id) const {
    auto it = versions_.find(content_id);
    if (it == versions_.end() || it->second.empty()) {
        return 0;
    }

    int latest = 0;
    for (const auto& v : it->second) {
        if (v.version_number > latest) {
            latest = v.version_number;
        }
    }

    return latest;
}

bool VersionManager::hasVersions(const std::string& content_id) const {
    auto it = versions_.find(content_id);
    return it != versions_.end() && !it->second.empty();
}

} // namespace content
} // namespace themis
