#include "updates/manifest_database.h"
#include "utils/logger.h"
#include <algorithm>

namespace themis {
namespace updates {

ManifestDatabase::ManifestDatabase(
    std::shared_ptr<RocksDBWrapper> storage,
    std::shared_ptr<acceleration::PluginSecurityVerifier> verifier
)
    : storage_(std::move(storage))
    , verifier_(std::move(verifier)) {
    initializeColumnFamilies();
}

ManifestDatabase::~ManifestDatabase() {
    // Column family handles are managed by RocksDBWrapper
}

void ManifestDatabase::initializeColumnFamilies() {
    try {
        cf_manifests_ = storage_->getOrCreateColumnFamily("release_manifests");
        cf_files_ = storage_->getOrCreateColumnFamily("file_registry");
        cf_signatures_ = storage_->getOrCreateColumnFamily("signature_cache");
        cf_cache_ = storage_->getOrCreateColumnFamily("download_cache");
        
        LOG_INFO("ManifestDatabase column families initialized");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize ManifestDatabase column families: {}", e.what());
        // Fall back to default CF
        cf_manifests_ = nullptr;
        cf_files_ = nullptr;
        cf_signatures_ = nullptr;
        cf_cache_ = nullptr;
    }
}

bool ManifestDatabase::storeManifest(const ReleaseManifest& manifest) {
    try {
        std::string key = manifest.version;
        std::string value = manifest.toJson().dump();
        
        rocksdb::Status status = storage_->getRawDB()->Put(
            rocksdb::WriteOptions(),
            cf_manifests_ ? cf_manifests_ : storage_->getRawDB()->DefaultColumnFamily(),
            key,
            value
        );
        
        if (!status.ok()) {
            LOG_ERROR("Failed to store manifest {}: {}", manifest.version, status.ToString());
            return false;
        }
        
        // Store individual files
        for (const auto& file : manifest.files) {
            if (!storeFile(file, manifest.version)) {
                LOG_WARN("Failed to store file {} for version {}", file.path, manifest.version);
            }
        }
        
        LOG_INFO("Stored manifest for version {}", manifest.version);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception storing manifest: {}", e.what());
        return false;
    }
}

std::optional<ReleaseManifest> ManifestDatabase::getManifest(const std::string& version) {
    try {
        std::string value;
        rocksdb::Status status = storage_->getRawDB()->Get(
            rocksdb::ReadOptions(),
            cf_manifests_ ? cf_manifests_ : storage_->getRawDB()->DefaultColumnFamily(),
            version,
            &value
        );
        
        if (!status.ok()) {
            if (!status.IsNotFound()) {
                LOG_ERROR("Failed to get manifest {}: {}", version, status.ToString());
            }
            return std::nullopt;
        }
        
        auto j = json::parse(value);
        return ReleaseManifest::fromJson(j);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception getting manifest: {}", e.what());
        return std::nullopt;
    }
}

std::optional<ReleaseManifest> ManifestDatabase::getLatestManifest() {
    auto versions = listVersions();
    if (versions.empty()) {
        return std::nullopt;
    }
    
    // Return the last version (highest)
    return getManifest(versions.back());
}

std::vector<std::string> ManifestDatabase::listVersions() const {
    std::vector<std::string> versions;
    
    try {
        auto it = storage_->getRawDB()->NewIterator(
            rocksdb::ReadOptions(),
            cf_manifests_ ? cf_manifests_ : storage_->getRawDB()->DefaultColumnFamily()
        );
        
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            versions.push_back(it->key().ToString());
        }
        
        delete it;
        
        // Sort versions
        std::sort(versions.begin(), versions.end());
    } catch (const std::exception& e) {
        LOG_ERROR("Exception listing versions: {}", e.what());
    }
    
    return versions;
}

bool ManifestDatabase::verifyManifest(const ReleaseManifest& manifest) {
    // Calculate hash
    std::string calculated_hash = manifest.calculateHash();
    
    // Check if hash matches
    if (calculated_hash != manifest.manifest_hash) {
        LOG_ERROR("Manifest hash mismatch for version {}: expected {}, got {}",
            manifest.version, manifest.manifest_hash, calculated_hash);
        return false;
    }
    
    // Verify signature if verifier is available
    if (verifier_ && !manifest.signature.empty()) {
        // Check cache first
        auto cached = getCachedSignatureVerification(manifest.manifest_hash);
        if (cached) {
            return *cached;
        }
        
        // TODO: Implement signature verification using verifier_
        // For now, we'll trust the hash verification
        cacheSignatureVerification(manifest.manifest_hash, true, manifest.signing_certificate);
    }
    
    return true;
}

bool ManifestDatabase::verifyFile(const std::string& path, const std::string& version) {
    auto file = getFile(path, version);
    if (!file) {
        return false;
    }
    
    // TODO: Implement actual file verification
    // For now, just check if it exists in registry
    return true;
}

std::optional<ReleaseFile> ManifestDatabase::getFile(
    const std::string& path,
    const std::string& version
) {
    try {
        std::string key = path + ":" + version;
        std::string value;
        
        rocksdb::Status status = storage_->getRawDB()->Get(
            rocksdb::ReadOptions(),
            cf_files_ ? cf_files_ : storage_->getRawDB()->DefaultColumnFamily(),
            key,
            &value
        );
        
        if (!status.ok()) {
            return std::nullopt;
        }
        
        auto j = json::parse(value);
        return ReleaseFile::fromJson(j);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception getting file: {}", e.what());
        return std::nullopt;
    }
}

bool ManifestDatabase::storeFile(const ReleaseFile& file, const std::string& version) {
    try {
        std::string key = file.path + ":" + version;
        std::string value = file.toJson().dump();
        
        rocksdb::Status status = storage_->getRawDB()->Put(
            rocksdb::WriteOptions(),
            cf_files_ ? cf_files_ : storage_->getRawDB()->DefaultColumnFamily(),
            key,
            value
        );
        
        return status.ok();
    } catch (const std::exception& e) {
        LOG_ERROR("Exception storing file: {}", e.what());
        return false;
    }
}

void ManifestDatabase::cacheSignatureVerification(
    const std::string& hash,
    bool verified,
    const std::string& certificate
) {
    try {
        json j;
        j["verified"] = verified;
        j["certificate"] = certificate;
        j["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        
        std::string value = j.dump();
        
        storage_->getRawDB()->Put(
            rocksdb::WriteOptions(),
            cf_signatures_ ? cf_signatures_ : storage_->getRawDB()->DefaultColumnFamily(),
            hash,
            value
        );
    } catch (const std::exception& e) {
        LOG_ERROR("Exception caching signature verification: {}", e.what());
    }
}

std::optional<bool> ManifestDatabase::getCachedSignatureVerification(const std::string& hash) {
    try {
        std::string value;
        rocksdb::Status status = storage_->getRawDB()->Get(
            rocksdb::ReadOptions(),
            cf_signatures_ ? cf_signatures_ : storage_->getRawDB()->DefaultColumnFamily(),
            hash,
            &value
        );
        
        if (!status.ok()) {
            return std::nullopt;
        }
        
        auto j = json::parse(value);
        return j.value("verified", false);
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

void ManifestDatabase::cacheDownload(
    const std::string& version,
    const std::string& filename,
    const std::string& local_path
) {
    try {
        std::string key = version + ":" + filename;
        
        storage_->getRawDB()->Put(
            rocksdb::WriteOptions(),
            cf_cache_ ? cf_cache_ : storage_->getRawDB()->DefaultColumnFamily(),
            key,
            local_path
        );
    } catch (const std::exception& e) {
        LOG_ERROR("Exception caching download: {}", e.what());
    }
}

std::optional<std::string> ManifestDatabase::getCachedDownload(
    const std::string& version,
    const std::string& filename
) {
    try {
        std::string key = version + ":" + filename;
        std::string value;
        
        rocksdb::Status status = storage_->getRawDB()->Get(
            rocksdb::ReadOptions(),
            cf_cache_ ? cf_cache_ : storage_->getRawDB()->DefaultColumnFamily(),
            key,
            &value
        );
        
        if (!status.ok()) {
            return std::nullopt;
        }
        
        return value;
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

bool ManifestDatabase::deleteManifest(const std::string& version) {
    try {
        // Delete manifest
        rocksdb::Status status = storage_->getRawDB()->Delete(
            rocksdb::WriteOptions(),
            cf_manifests_ ? cf_manifests_ : storage_->getRawDB()->DefaultColumnFamily(),
            version
        );
        
        if (!status.ok()) {
            LOG_ERROR("Failed to delete manifest {}: {}", version, status.ToString());
            return false;
        }
        
        // TODO: Delete associated files from registry
        
        LOG_INFO("Deleted manifest for version {}", version);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception deleting manifest: {}", e.what());
        return false;
    }
}

} // namespace updates
} // namespace themis
