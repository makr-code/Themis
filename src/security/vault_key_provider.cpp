#include "security/vault_key_provider.h"
#include "security/key_provider.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <map>
#include <mutex>
#include <chrono>

using json = nlohmann::json;

namespace themis {

// CURL write callback
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Base64 decode helper
static std::vector<uint8_t> base64_decode(const std::string& encoded) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::vector<uint8_t> result;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;
    
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

// Base64 encode helper
static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    int val = 0, valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

// Internal implementation (PIMPL pattern)
struct VaultKeyProvider::Impl {
    Config config;
    CURL* curl;
    std::mutex mutex;
    
    // Cache structure: "key_id:version" -> {key_bytes, expiry_time}
    struct CacheEntry {
        std::vector<uint8_t> key_bytes;
        int64_t expiry_ms;
        int64_t last_access_ms;
    };
    std::map<std::string, CacheEntry> cache;
    
    // Metrics
    size_t total_requests = 0;
    size_t cache_hits = 0;
    
    Impl(const Config& cfg) : config(cfg), curl(nullptr) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        if (!curl) {
            throw KeyOperationException("Failed to initialize libcurl");
        }
        
        // Set common options
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.request_timeout_ms);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config.verify_ssl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config.verify_ssl ? 2L : 0L);
    }
    
    ~Impl() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
        curl_global_cleanup();
    }
    
    std::string performRequest(const std::string& url, const std::string& method, 
                                const std::string& body = "") {
        std::lock_guard<std::mutex> lock(mutex);
        
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // Set custom request method
        if (method == "GET") {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        } else if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        } else if (method == "LIST") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "LIST");
        }
        
        // Set headers
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("X-Vault-Token: " + config.vault_token).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            throw KeyOperationException(std::string("CURL error: ") + curl_easy_strerror(res));
        }
        
        // Check HTTP status
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code == 404) {
            throw KeyNotFoundException("key", 0);  // Will be refined by caller
        } else if (http_code == 403) {
            throw KeyOperationException("Vault authentication failed (403 Forbidden)");
        } else if (http_code >= 500) {
            throw KeyOperationException("Vault server error (HTTP " + std::to_string(http_code) + ")");
        } else if (http_code >= 400) {
            throw KeyOperationException("Vault request failed (HTTP " + std::to_string(http_code) + "): " + response);
        }
        
        return response;
    }
    
    void evictExpiredCache() {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        for (auto it = cache.begin(); it != cache.end();) {
            if (it->second.expiry_ms < now) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void evictLRU() {
        if (cache.size() < (size_t)config.cache_capacity) {
            return;
        }
        
        // Find oldest accessed entry
        auto oldest = cache.begin();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.last_access_ms < oldest->second.last_access_ms) {
                oldest = it;
            }
        }
        cache.erase(oldest);
    }
};

VaultKeyProvider::VaultKeyProvider(const Config& config) 
    : impl_(std::make_unique<Impl>(config)) 
{}

VaultKeyProvider::VaultKeyProvider(
    const std::string& vault_addr,
    const std::string& vault_token,
    const std::string& kv_mount_path
) : impl_(std::make_unique<Impl>(Config())) {
    impl_->config.vault_addr = vault_addr;
    impl_->config.vault_token = vault_token;
    impl_->config.kv_mount_path = kv_mount_path;
}

VaultKeyProvider::~VaultKeyProvider() = default;

std::string VaultKeyProvider::httpGet(const std::string& path) {
    std::string url = impl_->config.vault_addr + path;
    return impl_->performRequest(url, "GET");
}

std::string VaultKeyProvider::httpPost(const std::string& path, const std::string& body) {
    std::string url = impl_->config.vault_addr + path;
    return impl_->performRequest(url, "POST", body);
}

std::string VaultKeyProvider::httpList(const std::string& path) {
    std::string url = impl_->config.vault_addr + path;
    return impl_->performRequest(url, "LIST");
}

std::string VaultKeyProvider::readSecret(const std::string& key_id, uint32_t version) {
    std::string path;
    if (impl_->config.kv_version == "v2") {
        path = "/v1/" + impl_->config.kv_mount_path + "/data/keys/" + key_id;
        if (version > 0) {
            path += "?version=" + std::to_string(version);
        }
    } else {
        // KV v1
        path = "/v1/" + impl_->config.kv_mount_path + "/keys/" + key_id;
    }
    
    try {
        return httpGet(path);
    } catch (const KeyNotFoundException&) {
        throw KeyNotFoundException(key_id, version);
    }
}

std::string VaultKeyProvider::readSecretMetadata(const std::string& key_id) {
    if (impl_->config.kv_version != "v2") {
        throw KeyOperationException("Metadata only available in KV v2");
    }
    
    std::string path = "/v1/" + impl_->config.kv_mount_path + "/metadata/keys/" + key_id;
    try {
        return httpGet(path);
    } catch (const KeyNotFoundException&) {
        throw KeyNotFoundException(key_id, 0);
    }
}

void VaultKeyProvider::writeSecret(const std::string& key_id, const std::string& key_b64, uint32_t version) {
    json payload;
    
    if (impl_->config.kv_version == "v2") {
        payload["data"] = {
            {"key", key_b64},
            {"algorithm", "AES-256-GCM"},
            {"version", version}
        };
    } else {
        payload = {
            {"key", key_b64},
            {"algorithm", "AES-256-GCM"},
            {"version", version}
        };
    }
    
    std::string path = "/v1/" + impl_->config.kv_mount_path + 
                       (impl_->config.kv_version == "v2" ? "/data/keys/" : "/keys/") + key_id;
    
    httpPost(path, payload.dump());
}

std::vector<std::string> VaultKeyProvider::listSecrets() {
    std::string path = "/v1/" + impl_->config.kv_mount_path + 
                       (impl_->config.kv_version == "v2" ? "/metadata/keys" : "/keys");
    
    std::string response = httpList(path);
    json j = json::parse(response);
    
    std::vector<std::string> keys;
    if (j.contains("data") && j["data"].contains("keys")) {
        for (const auto& key : j["data"]["keys"]) {
            keys.push_back(key.get<std::string>());
        }
    }
    return keys;
}

std::vector<uint8_t> VaultKeyProvider::parseKeyFromVaultResponse(const std::string& json_response) {
    json j = json::parse(json_response);
    
    std::string key_b64;
    if (impl_->config.kv_version == "v2") {
        if (!j.contains("data") || !j["data"].contains("data")) {
            throw KeyOperationException("Invalid Vault response format (missing data.data)");
        }
        key_b64 = j["data"]["data"]["key"].get<std::string>();
    } else {
        if (!j.contains("data")) {
            throw KeyOperationException("Invalid Vault response format (missing data)");
        }
        key_b64 = j["data"]["key"].get<std::string>();
    }
    
    return base64_decode(key_b64);
}

KeyMetadata VaultKeyProvider::parseMetadataFromVaultResponse(const std::string& json_response) {
    json j = json::parse(json_response);
    
    KeyMetadata meta;
    
    if (impl_->config.kv_version == "v2") {
        if (!j.contains("data")) {
            throw KeyOperationException("Invalid Vault metadata response");
        }
        
        const auto& data = j["data"];
        
        // Get current version
        if (data.contains("current_version")) {
            meta.version = data["current_version"].get<uint32_t>();
        }
        
        // Get creation time from versions
        if (data.contains("versions") && !data["versions"].empty()) {
            auto version_key = std::to_string(meta.version);
            if (data["versions"].contains(version_key)) {
                const auto& version_data = data["versions"][version_key];
                if (version_data.contains("created_time")) {
                    // Parse RFC3339 timestamp (simplified)
                    std::string created = version_data["created_time"].get<std::string>();
                    // For now, use current time (proper parsing would use strptime)
                    meta.created_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();
                }
            }
        }
    }
    
    meta.algorithm = "AES-256-GCM";
    meta.status = KeyStatus::ACTIVE;
    meta.expires_at_ms = 0;
    
    return meta;
}

std::string VaultKeyProvider::makeCacheKey(const std::string& key_id, uint32_t version) const {
    return key_id + ":" + std::to_string(version);
}

std::vector<uint8_t> VaultKeyProvider::getKey(const std::string& key_id) {
    return getKey(key_id, 0);  // 0 = latest version
}

std::vector<uint8_t> VaultKeyProvider::getKey(const std::string& key_id, uint32_t version) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    impl_->total_requests++;
    
    // Check cache
    std::string cache_key = makeCacheKey(key_id, version);
    auto it = impl_->cache.find(cache_key);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    if (it != impl_->cache.end() && it->second.expiry_ms > now) {
        // Cache hit
        impl_->cache_hits++;
        it->second.last_access_ms = now;
        return it->second.key_bytes;
    }
    
    // Cache miss - fetch from Vault
    impl_->mutex.unlock();  // Release lock during network call
    std::string response = readSecret(key_id, version);
    std::vector<uint8_t> key_bytes = parseKeyFromVaultResponse(response);
    impl_->mutex.lock();
    
    // Store in cache
    impl_->evictExpiredCache();
    impl_->evictLRU();
    
    Impl::CacheEntry entry;
    entry.key_bytes = key_bytes;
    entry.expiry_ms = now + (impl_->config.cache_ttl_seconds * 1000);
    entry.last_access_ms = now;
    
    impl_->cache[cache_key] = entry;
    
    return key_bytes;
}

uint32_t VaultKeyProvider::rotateKey(const std::string& key_id) {
    // Get current metadata to find latest version
    std::string metadata_response = readSecretMetadata(key_id);
    KeyMetadata meta = parseMetadataFromVaultResponse(metadata_response);
    
    uint32_t new_version = meta.version + 1;
    
    // Generate new random key
    std::vector<uint8_t> new_key(32);  // 256 bits
    RAND_bytes(new_key.data(), 32);
    
    std::string key_b64 = base64_encode(new_key);
    
    // Write new version to Vault
    writeSecret(key_id, key_b64, new_version);
    
    // Invalidate cache for this key_id
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto it = impl_->cache.begin(); it != impl_->cache.end();) {
        if (it->first.find(key_id + ":") == 0) {
            it = impl_->cache.erase(it);
        } else {
            ++it;
        }
    }
    
    return new_version;
}

std::vector<KeyMetadata> VaultKeyProvider::listKeys() {
    std::vector<std::string> key_ids = listSecrets();
    std::vector<KeyMetadata> result;
    
    for (const auto& key_id : key_ids) {
        try {
            KeyMetadata meta = getKeyMetadata(key_id, 0);
            meta.key_id = key_id;
            result.push_back(meta);
        } catch (const std::exception&) {
            // Skip keys that can't be read
            continue;
        }
    }
    
    return result;
}

KeyMetadata VaultKeyProvider::getKeyMetadata(const std::string& key_id, uint32_t version) {
    std::string response = readSecretMetadata(key_id);
    KeyMetadata meta = parseMetadataFromVaultResponse(response);
    meta.key_id = key_id;
    
    if (version > 0) {
        meta.version = version;
    }
    
    return meta;
}

void VaultKeyProvider::deleteKey(const std::string& key_id, uint32_t version) {
    // In Vault KV v2, deletion is done via DELETE /metadata/keys/:path
    // This soft-deletes the key (marks as deleted, can be recovered)
    
    if (impl_->config.kv_version != "v2") {
        throw KeyOperationException("Key deletion only supported in KV v2");
    }
    
    // Check if key is deprecated first (safety check)
    try {
        KeyMetadata meta = getKeyMetadata(key_id, version);
        if (meta.status == KeyStatus::ACTIVE) {
            throw KeyOperationException("Cannot delete ACTIVE key. Deprecate it first via rotation.");
        }
    } catch (const KeyNotFoundException&) {
        // Key already doesn't exist, that's fine
        return;
    }
    
    // Perform deletion via Vault API
    std::string path = "/v1/" + impl_->config.kv_mount_path + "/metadata/keys/" + key_id;
    
    // Vault uses DELETE HTTP method
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    std::string url = impl_->config.vault_addr + path;
    curl_easy_setopt(impl_->curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    
    std::string response;
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &response);
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-Vault-Token: " + impl_->config.vault_token).c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_HTTPHEADER, headers);
    
    CURLcode res = curl_easy_perform(impl_->curl);
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        throw KeyOperationException(std::string("Failed to delete key: ") + curl_easy_strerror(res));
    }
    
    // Clear from cache
    for (auto it = impl_->cache.begin(); it != impl_->cache.end();) {
        if (it->first.find(key_id + ":") == 0) {
            it = impl_->cache.erase(it);
        } else {
            ++it;
        }
    }
}

void VaultKeyProvider::clearCache() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->cache.clear();
}

VaultKeyProvider::CacheStats VaultKeyProvider::getCacheStats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    CacheStats stats;
    stats.total_requests = impl_->total_requests;
    stats.cache_hits = impl_->cache_hits;
    stats.cache_size = impl_->cache.size();
    stats.hit_rate = impl_->total_requests > 0 
        ? (double)impl_->cache_hits / impl_->total_requests 
        : 0.0;
    
    return stats;
}

} // namespace themis
