#include "storage/blob_storage_backend.h"
#include "utils/logger.h"
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

namespace themis {
namespace storage {

/**
 * @brief WebDAV Blob Storage Backend
 * 
 * Supports WebDAV-based storage including:
 * - SharePoint
 * - ActiveDirectory integrated file shares
 * - Generic WebDAV servers
 * 
 * Authentication: Basic Auth (username/password)
 * Transport: HTTPS (TLS/SSL)
 */
class WebDAVBlobBackend : public IBlobStorageBackend {
private:
    std::string base_url_;
    std::string username_;
    std::string password_;
    bool verify_ssl_;
    
    // CURL helper for writing data
    static size_t writeCallback(void* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* vec = static_cast<std::vector<uint8_t>*>(userdata);
        size_t total = size * nmemb;
        vec->insert(vec->end(), static_cast<uint8_t*>(ptr), static_cast<uint8_t*>(ptr) + total);
        return total;
    }
    
    // CURL helper for reading data
    struct ReadData {
        const uint8_t* data;
        size_t size;
        size_t offset;
    };
    
    static size_t readCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* rd = static_cast<ReadData*>(userdata);
        size_t total = size * nmemb;
        size_t remaining = rd->size - rd->offset;
        size_t to_copy = std::min(total, remaining);
        
        if (to_copy > 0) {
            std::memcpy(ptr, rd->data + rd->offset, to_copy);
            rd->offset += to_copy;
        }
        
        return to_copy;
    }
    
    // Compute SHA256 hash
    static std::string computeSHA256(const std::vector<uint8_t>& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') 
               << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
    
    std::string getBlobUrl(const std::string& blob_id) const {
        // Remove trailing slash from base_url
        std::string url = base_url_;
        if (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        return url + "/" + blob_id + ".blob";
    }
    
public:
    WebDAVBlobBackend(
        const std::string& base_url,
        const std::string& username,
        const std::string& password,
        bool verify_ssl = true
    ) : base_url_(base_url),
        username_(username),
        password_(password),
        verify_ssl_(verify_ssl) {
        
        THEMIS_INFO("WebDAVBlobBackend initialized: url={}, user={}, ssl_verify={}", 
            base_url_, username_, verify_ssl_);
    }
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        std::string url = getBlobUrl(blob_id);
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        try {
            // Setup CURL for PUT request
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERNAME, username_.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);
            
            // Setup headers
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            
            // Setup data
            ReadData rd;
            rd.data = data.data();
            rd.size = data.size();
            rd.offset = 0;
            
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
            curl_easy_setopt(curl, CURLOPT_READDATA, &rd);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(data.size()));
            
            // Perform request
            CURLcode res = curl_easy_perform(curl);
            
            curl_slist_free_all(headers);
            
            if (res != CURLE_OK) {
                curl_easy_cleanup(curl);
                throw std::runtime_error(
                    "WebDAV PUT failed: " + std::string(curl_easy_strerror(res))
                );
            }
            
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            curl_easy_cleanup(curl);
            
            if (response_code < 200 || response_code >= 300) {
                throw std::runtime_error(
                    "WebDAV PUT failed with HTTP " + std::to_string(response_code)
                );
            }
            
            // Create BlobRef
            BlobRef ref;
            ref.id = blob_id;
            ref.type = BlobStorageType::WEBDAV;
            ref.uri = url;
            ref.size_bytes = static_cast<int64_t>(data.size());
            ref.hash_sha256 = computeSHA256(data);
            ref.created_at = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            
            THEMIS_DEBUG("WebDAVBlobBackend: Stored blob {} ({} bytes) at {}", 
                blob_id, data.size(), url);
            
            return ref;
            
        } catch (...) {
            curl_easy_cleanup(curl);
            throw;
        }
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        CURL* curl = curl_easy_init();
        if (!curl) {
            THEMIS_ERROR("Failed to initialize CURL");
            return std::nullopt;
        }
        
        try {
            curl_easy_setopt(curl, CURLOPT_URL, ref.uri.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERNAME, username_.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);
            
            std::vector<uint8_t> data;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
            
            CURLcode res = curl_easy_perform(curl);
            
            if (res != CURLE_OK) {
                THEMIS_ERROR("WebDAV GET failed: {}", curl_easy_strerror(res));
                curl_easy_cleanup(curl);
                return std::nullopt;
            }
            
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            curl_easy_cleanup(curl);
            
            if (response_code == 404) {
                THEMIS_WARN("WebDAVBlobBackend: Blob not found: {}", ref.uri);
                return std::nullopt;
            }
            
            if (response_code < 200 || response_code >= 300) {
                THEMIS_ERROR("WebDAV GET failed with HTTP {}", response_code);
                return std::nullopt;
            }
            
            THEMIS_DEBUG("WebDAVBlobBackend: Retrieved blob {} ({} bytes)", 
                ref.id, data.size());
            
            return data;
            
        } catch (const std::exception& e) {
            THEMIS_ERROR("WebDAVBlobBackend::get failed: {}", e.what());
            curl_easy_cleanup(curl);
            return std::nullopt;
        }
    }
    
    bool remove(const BlobRef& ref) override {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return false;
        }
        
        try {
            curl_easy_setopt(curl, CURLOPT_URL, ref.uri.c_str());
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERNAME, username_.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);
            
            CURLcode res = curl_easy_perform(curl);
            
            long response_code = 0;
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            }
            
            curl_easy_cleanup(curl);
            
            if (res == CURLE_OK && (response_code == 200 || response_code == 204)) {
                THEMIS_DEBUG("WebDAVBlobBackend: Removed blob {}", ref.id);
                return true;
            }
            
            return false;
            
        } catch (...) {
            curl_easy_cleanup(curl);
            return false;
        }
    }
    
    bool exists(const BlobRef& ref) override {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return false;
        }
        
        try {
            curl_easy_setopt(curl, CURLOPT_URL, ref.uri.c_str());
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);  // HEAD request
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERNAME, username_.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);
            
            CURLcode res = curl_easy_perform(curl);
            
            long response_code = 0;
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            }
            
            curl_easy_cleanup(curl);
            
            return res == CURLE_OK && response_code == 200;
            
        } catch (...) {
            curl_easy_cleanup(curl);
            return false;
        }
    }
    
    std::string name() const override {
        return "webdav";
    }
    
    bool isAvailable() const override {
        // Test with PROPFIND on base_url
        CURL* curl = curl_easy_init();
        if (!curl) {
            return false;
        }
        
        try {
            curl_easy_setopt(curl, CURLOPT_URL, base_url_.c_str());
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERNAME, username_.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);  // 5 second timeout
            
            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            
            return res == CURLE_OK;
            
        } catch (...) {
            curl_easy_cleanup(curl);
            return false;
        }
    }
};

} // namespace storage
} // namespace themis
