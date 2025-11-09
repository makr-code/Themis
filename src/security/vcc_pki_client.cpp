#include "security/vcc_pki_client.h"
#include <curl/curl.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace themis {

// ────────────────────────────────────────────────────────────────────────────
// X509Certificate Implementation
// ────────────────────────────────────────────────────────────────────────────

bool X509Certificate::isValid() const {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    return now >= not_before_ms && now <= not_after_ms;
}

bool X509Certificate::isExpired(int64_t now_ms) const {
    return now_ms > not_after_ms;
}

nlohmann::json X509Certificate::toJson() const {
    return {
        {"id", id},
        {"pem", pem},
        {"subject", subject},
        {"issuer", issuer},
        {"not_before_ms", not_before_ms},
        {"not_after_ms", not_after_ms},
        {"key_usage", key_usage},
        {"san", san}
    };
}

X509Certificate X509Certificate::fromJson(const nlohmann::json& j) {
    X509Certificate cert;
    cert.id = j.value("id", "");
    cert.pem = j.value("pem", "");
    cert.subject = j.value("subject", "");
    cert.issuer = j.value("issuer", "");
    cert.not_before_ms = j.value("not_before_ms", static_cast<int64_t>(0));
    cert.not_after_ms = j.value("not_after_ms", static_cast<int64_t>(0));
    cert.key_usage = j.value("key_usage", "");
    cert.san = j.value("san", std::vector<std::string>{});
    return cert;
}

// ────────────────────────────────────────────────────────────────────────────
// CRLEntry Implementation
// ────────────────────────────────────────────────────────────────────────────

nlohmann::json CRLEntry::toJson() const {
    return {
        {"serial_number", serial_number},
        {"revocation_time_ms", revocation_time_ms},
        {"reason", reason}
    };
}

CRLEntry CRLEntry::fromJson(const nlohmann::json& j) {
    CRLEntry entry;
    entry.serial_number = j.value("serial_number", "");
    entry.revocation_time_ms = j.value("revocation_time_ms", static_cast<int64_t>(0));
    entry.reason = j.value("reason", "");
    return entry;
}

// ────────────────────────────────────────────────────────────────────────────
// CertificateRequest Implementation
// ────────────────────────────────────────────────────────────────────────────

nlohmann::json CertificateRequest::toJson() const {
    return {
        {"common_name", common_name},
        {"organization", organization},
        {"san", san},
        {"key_usage", key_usage},
        {"validity_days", validity_days}
    };
}

// ────────────────────────────────────────────────────────────────────────────
// VCCPKIClient::Impl - HTTP Client using libcurl
// ────────────────────────────────────────────────────────────────────────────

struct VCCPKIClient::Impl {
    CURL* curl;
    struct curl_slist* headers;
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
    bool verify_server;
    bool use_mtls;
    
    Impl() : curl(nullptr), headers(nullptr), verify_server(true), use_mtls(false) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // Default headers
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
    }
    
    ~Impl() {
        if (headers) {
            curl_slist_free_all(headers);
        }
        if (curl) {
            curl_easy_cleanup(curl);
        }
        curl_global_cleanup();
    }
    
    // Callback for reading response data
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        size_t total_size = size * nmemb;
        userp->append(static_cast<char*>(contents), total_size);
        return total_size;
    }
    
    std::string execute(const std::string& url, const std::string& method, const std::string& body, int timeout_ms) {
        std::string response;
        
        // Reset CURL for new request
        curl_easy_reset(curl);
        
        // Set URL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        
        // Set timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms / 2);
        
        // Set headers
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Set write callback
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // TLS/SSL configuration
        if (verify_server) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            
            if (!ca_cert_path.empty()) {
                curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path.c_str());
            }
        } else {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }
        
        // mTLS (Mutual TLS)
        if (use_mtls) {
            if (client_cert_path.empty() || client_key_path.empty()) {
                throw std::runtime_error("mTLS enabled but client cert/key not provided");
            }
            curl_easy_setopt(curl, CURLOPT_SSLCERT, client_cert_path.c_str());
            curl_easy_setopt(curl, CURLOPT_SSLKEY, client_key_path.c_str());
        }
        
        // HTTP method
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
        } else if (method == "GET") {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }
        
        // Execute request
        CURLcode res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            std::string error = "CURL request failed: ";
            error += curl_easy_strerror(res);
            throw std::runtime_error(error);
        }
        
        // Check HTTP status code
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if (http_code < 200 || http_code >= 300) {
            std::ostringstream oss;
            oss << "HTTP error " << http_code << ": " << response;
            throw std::runtime_error(oss.str());
        }
        
        return response;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// VCCPKIClient Implementation
// ────────────────────────────────────────────────────────────────────────────

VCCPKIClient::VCCPKIClient(
    const std::string& base_url,
    const TLSConfig& tls_config,
    int timeout_ms
)
    : base_url_(base_url)
    , tls_config_(tls_config)
    , timeout_ms_(timeout_ms)
    , impl_(std::make_unique<Impl>())
{
    // Configure TLS
    impl_->ca_cert_path = tls_config.ca_cert_path;
    impl_->client_cert_path = tls_config.client_cert_path;
    impl_->client_key_path = tls_config.client_key_path;
    impl_->verify_server = tls_config.verify_server;
    impl_->use_mtls = tls_config.use_mtls;
}

VCCPKIClient::~VCCPKIClient() = default;

VCCPKIClient::VCCPKIClient(VCCPKIClient&&) noexcept = default;
VCCPKIClient& VCCPKIClient::operator=(VCCPKIClient&&) noexcept = default;

std::string VCCPKIClient::httpGet(const std::string& path) {
    std::string url = base_url_ + path;
    return impl_->execute(url, "GET", "", timeout_ms_);
}

std::string VCCPKIClient::httpPost(const std::string& path, const nlohmann::json& body) {
    std::string url = base_url_ + path;
    std::string body_str = body.dump();
    return impl_->execute(url, "POST", body_str, timeout_ms_);
}

X509Certificate VCCPKIClient::requestCertificate(const CertificateRequest& request) {
    nlohmann::json body = request.toJson();
    
    std::string response = httpPost("/api/v1/certificates/request", body);
    
    nlohmann::json response_json = nlohmann::json::parse(response);
    
    return X509Certificate::fromJson(response_json);
}

X509Certificate VCCPKIClient::getCertificate(const std::string& cert_id) {
    std::string path = "/api/v1/certificates/" + cert_id;
    
    std::string response = httpGet(path);
    
    nlohmann::json response_json = nlohmann::json::parse(response);
    
    return X509Certificate::fromJson(response_json);
}

std::vector<CRLEntry> VCCPKIClient::getCRL() {
    std::string response = httpGet("/api/v1/crl");
    
    nlohmann::json response_json = nlohmann::json::parse(response);
    
    std::vector<CRLEntry> crl;
    
    if (response_json.contains("revoked_certificates")) {
        for (const auto& entry_json : response_json["revoked_certificates"]) {
            crl.push_back(CRLEntry::fromJson(entry_json));
        }
    }
    
    return crl;
}

bool VCCPKIClient::isRevoked(const std::string& cert_id, const std::vector<CRLEntry>& crl) const {
    return std::any_of(crl.begin(), crl.end(), [&cert_id](const CRLEntry& entry) {
        return entry.serial_number == cert_id;
    });
}

bool VCCPKIClient::healthCheck() {
    try {
        std::string response = httpGet("/api/v1/health");
        nlohmann::json response_json = nlohmann::json::parse(response);
        return response_json.value("status", "") == "ok";
    } catch (...) {
        return false;
    }
}

X509Certificate VCCPKIClient::parseCertificate(const std::string& pem) {
    X509Certificate cert;
    
    // Parse PEM using OpenSSL
    BIO* bio = BIO_new_mem_buf(pem.c_str(), static_cast<int>(pem.size()));
    if (!bio) {
        throw std::runtime_error("Failed to create BIO from PEM");
    }
    
    X509* x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    
    if (!x509) {
        throw std::runtime_error("Failed to parse X.509 certificate");
    }
    
    // Extract serial number (ID)
    ASN1_INTEGER* serial = X509_get_serialNumber(x509);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    char* hex = BN_bn2hex(bn);
    cert.id = hex;
    OPENSSL_free(hex);
    BN_free(bn);
    
    // Extract subject
    X509_NAME* subject = X509_get_subject_name(x509);
    char subject_buf[256];
    X509_NAME_oneline(subject, subject_buf, sizeof(subject_buf));
    cert.subject = subject_buf;
    
    // Extract issuer
    X509_NAME* issuer = X509_get_issuer_name(x509);
    char issuer_buf[256];
    X509_NAME_oneline(issuer, issuer_buf, sizeof(issuer_buf));
    cert.issuer = issuer_buf;
    
    // Extract validity period
    ASN1_TIME* not_before = X509_get_notBefore(x509);
    ASN1_TIME* not_after = X509_get_notAfter(x509);
    
    // Convert ASN1_TIME to milliseconds (simplified)
    // Production: Use ASN1_TIME_to_tm() for proper conversion
    cert.not_before_ms = 0; // Placeholder
    cert.not_after_ms = 0;  // Placeholder
    
    cert.pem = pem;
    
    X509_free(x509);
    
    return cert;
}

bool VCCPKIClient::validateCertChain(const X509Certificate& cert) const {
    // TODO: Implement full X.509 chain validation
    // 1. Load Root CA from tls_config_.ca_cert_path
    // 2. Build certificate chain
    // 3. Verify signatures up to Root CA
    // 4. Check CRL
    // 5. Check expiry
    
    // For now: Basic expiry check
    return cert.isValid();
}

} // namespace themis
