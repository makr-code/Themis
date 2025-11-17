#include "security/timestamp_authority.h"
#include "core/logging.h"
#include <openssl/ts.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <curl/curl.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace themis {
namespace security {

// Base64 encoding (reused from HSM implementation)
static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string ret;
    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    size_t in_len = data.size();
    const uint8_t* bytes_to_encode = data.data();
    
    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(int j = i; j < 3; j++)
            char_array_3[j] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];
        
        while(i++ < 3)
            ret += '=';
    }
    
    return ret;
}

static std::vector<uint8_t> base64_decode(const std::string& encoded_string) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t in_len = encoded_string.size();
    int i = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::vector<uint8_t> ret;
    
    while (in_len-- && (encoded_string[in_] != '=') && 
           (isalnum(encoded_string[in_]) || (encoded_string[in_] == '+') || (encoded_string[in_] == '/'))) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }
    
    if (i) {
        for (int j = i; j < 4; j++)
            char_array_4[j] = 0;
        
        for (int j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        
        for (int j = 0; j < i - 1; j++)
            ret.push_back(char_array_3[j]);
    }
    
    return ret;
}

/**
 * TimestampAuthority::Impl - RFC 3161 implementation
 */
class TimestampAuthority::Impl {
public:
    CURL* curl = nullptr;
    
    Impl() {
        curl = curl_easy_init();
    }
    
    ~Impl() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }
    
    // CURL write callback
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* buffer = static_cast<std::vector<uint8_t>*>(userdata);
        size_t total_size = size * nmemb;
        buffer->insert(buffer->end(), ptr, ptr + total_size);
        return total_size;
    }
};

TimestampAuthority::TimestampAuthority(TSAConfig config)
    : impl_(std::make_unique<Impl>())
    , config_(std::move(config)) {
    
    if (!impl_->curl) {
        THEMIS_ERROR("Failed to initialize CURL for TimestampAuthority");
    }
}

TimestampAuthority::~TimestampAuthority() = default;

TimestampAuthority::TimestampAuthority(TimestampAuthority&&) noexcept = default;
TimestampAuthority& TimestampAuthority::operator=(TimestampAuthority&&) noexcept = default;

std::vector<uint8_t> TimestampAuthority::generateNonce(size_t bytes) {
    std::vector<uint8_t> nonce(bytes);
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (size_t i = 0; i < bytes; ++i) {
        nonce[i] = dis(gen);
    }
    
    return nonce;
}

std::vector<uint8_t> TimestampAuthority::computeHash(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash;
    
    if (config_.hash_algorithm == "SHA256") {
        hash.resize(SHA256_DIGEST_LENGTH);
        SHA256(data.data(), data.size(), hash.data());
    } else if (config_.hash_algorithm == "SHA384") {
        hash.resize(SHA384_DIGEST_LENGTH);
        SHA384(data.data(), data.size(), hash.data());
    } else if (config_.hash_algorithm == "SHA512") {
        hash.resize(SHA512_DIGEST_LENGTH);
        SHA512(data.data(), data.size(), hash.data());
    } else {
        // Default to SHA256
        hash.resize(SHA256_DIGEST_LENGTH);
        SHA256(data.data(), data.size(), hash.data());
    }
    
    return hash;
}

std::vector<uint8_t> TimestampAuthority::createTSPRequest(
    const std::vector<uint8_t>& hash,
    const std::vector<uint8_t>& nonce) {
    
    // Create TS_REQ using OpenSSL
    TS_REQ* req = TS_REQ_new();
    if (!req) {
        THEMIS_ERROR("Failed to create TS_REQ");
        return {};
    }
    
    // Set version (1)
    TS_REQ_set_version(req, 1);
    
    // Create message imprint
    TS_MSG_IMPRINT* msg_imprint = TS_MSG_IMPRINT_new();
    
    // Set hash algorithm
    X509_ALGOR* algo = X509_ALGOR_new();
    int nid = NID_sha256;  // Default
    if (config_.hash_algorithm == "SHA384") {
        nid = NID_sha384;
    } else if (config_.hash_algorithm == "SHA512") {
        nid = NID_sha512;
    }
    X509_ALGOR_set0(algo, OBJ_nid2obj(nid), V_ASN1_NULL, nullptr);
    TS_MSG_IMPRINT_set_algo(msg_imprint, algo);
    
    // Set hash value
    ASN1_OCTET_STRING* hash_string = ASN1_OCTET_STRING_new();
    ASN1_OCTET_STRING_set(hash_string, hash.data(), hash.size());
    TS_MSG_IMPRINT_set_msg(msg_imprint, hash_string);
    
    TS_REQ_set_msg_imprint(req, msg_imprint);
    
    // Set nonce (optional, for replay protection)
    if (!nonce.empty()) {
        ASN1_INTEGER* nonce_asn1 = ASN1_INTEGER_new();
        BIGNUM* bn = BN_new();
        BN_bin2bn(nonce.data(), nonce.size(), bn);
        BN_to_ASN1_INTEGER(bn, nonce_asn1);
        TS_REQ_set_nonce(req, nonce_asn1);
        BN_free(bn);
    }
    
    // Request certificate
    if (config_.cert_req) {
        TS_REQ_set_cert_req(req, 1);
    }
    
    // Set policy OID (optional)
    if (!config_.policy_oid.empty()) {
        ASN1_OBJECT* policy = OBJ_txt2obj(config_.policy_oid.c_str(), 1);
        if (policy) {
            TS_REQ_set_policy_id(req, policy);
            ASN1_OBJECT_free(policy);
        }
    }
    
    // Convert to DER
    unsigned char* der = nullptr;
    int len = i2d_TS_REQ(req, &der);
    
    std::vector<uint8_t> result;
    if (len > 0 && der) {
        result.assign(der, der + len);
        OPENSSL_free(der);
    }
    
    TS_REQ_free(req);
    TS_MSG_IMPRINT_free(msg_imprint);
    X509_ALGOR_free(algo);
    
    return result;
}

std::vector<uint8_t> TimestampAuthority::sendTSPRequest(
    const std::vector<uint8_t>& request) {
    
    if (!impl_->curl) {
        last_error_ = "CURL not initialized";
        return {};
    }
    
    std::vector<uint8_t> response;
    
    // Set URL
    curl_easy_setopt(impl_->curl, CURLOPT_URL, config_.url.c_str());
    
    // Set POST data
    curl_easy_setopt(impl_->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, request.data());
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, request.size());
    
    // Set headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/timestamp-query");
    curl_easy_setopt(impl_->curl, CURLOPT_HTTPHEADER, headers);
    
    // Set write callback
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, Impl::write_callback);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &response);
    
    // Set timeout
    curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT, config_.timeout_seconds);
    
    // SSL verification
    if (config_.verify_tsa_cert) {
        curl_easy_setopt(impl_->curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(impl_->curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        if (!config_.ca_cert_path.empty()) {
            curl_easy_setopt(impl_->curl, CURLOPT_CAINFO, config_.ca_cert_path.c_str());
        }
    } else {
        curl_easy_setopt(impl_->curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(impl_->curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    
    // Authentication (if required)
    if (!config_.username.empty()) {
        std::string userpwd = config_.username + ":" + config_.password;
        curl_easy_setopt(impl_->curl, CURLOPT_USERPWD, userpwd.c_str());
    }
    
    // mTLS (if configured)
    if (!config_.client_cert_path.empty()) {
        curl_easy_setopt(impl_->curl, CURLOPT_SSLCERT, config_.client_cert_path.c_str());
    }
    if (!config_.client_key_path.empty()) {
        curl_easy_setopt(impl_->curl, CURLOPT_SSLKEY, config_.client_key_path.c_str());
    }
    
    // Perform request
    CURLcode res = curl_easy_perform(impl_->curl);
    
    // Cleanup headers
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        last_error_ = std::string("CURL request failed: ") + curl_easy_strerror(res);
        THEMIS_ERROR("TSA request failed: {}", last_error_);
        return {};
    }
    
    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (http_code != 200) {
        last_error_ = "TSA returned HTTP " + std::to_string(http_code);
        THEMIS_ERROR("TSA HTTP error: {}", last_error_);
        return {};
    }
    
    return response;
}

TimestampToken TimestampAuthority::parseTSPResponse(
    const std::vector<uint8_t>& response) {
    
    TimestampToken token;
    
    // Parse TS_RESP
    const unsigned char* p = response.data();
    TS_RESP* resp = d2i_TS_RESP(nullptr, &p, response.size());
    
    if (!resp) {
        token.error_message = "Failed to parse TS_RESP";
        THEMIS_ERROR("TSP response parsing failed");
        return token;
    }
    
    // Check PKI status
    TS_STATUS_INFO* status = TS_RESP_get_status_info(resp);
    ASN1_INTEGER* status_asn1 = TS_STATUS_INFO_get0_status(status);
    long status_val = ASN1_INTEGER_get(status_asn1);
    
    token.pki_status = static_cast<int>(status_val);
    
    if (status_val != 0 && status_val != 1) {  // 0 = granted, 1 = granted with mods
        token.error_message = "TSA rejected request (status=" + std::to_string(status_val) + ")";
        TS_RESP_free(resp);
        return token;
    }
    
    // Get timestamp token
    PKCS7* pkcs7 = TS_RESP_get_token(resp);
    if (!pkcs7) {
        token.error_message = "No timestamp token in response";
        TS_RESP_free(resp);
        return token;
    }
    
    // Store token in DER format
    unsigned char* der = nullptr;
    int der_len = i2d_PKCS7(pkcs7, &der);
    if (der_len > 0 && der) {
        token.token_der.assign(der, der + der_len);
        token.token_b64 = base64_encode(token.token_der);
        OPENSSL_free(der);
    }
    
    // Get TST_INFO
    TS_TST_INFO* tst_info = PKCS7_to_TS_TST_INFO(pkcs7);
    if (tst_info) {
        // Get timestamp
        ASN1_GENERALIZEDTIME* gen_time = TS_TST_INFO_get_time(tst_info);
        if (gen_time) {
            // Convert to ISO 8601
            char time_str[32];
            snprintf(time_str, sizeof(time_str), "%s", (char*)gen_time->data);
            token.timestamp_utc = time_str;
            
            // Convert to Unix timestamp (simplified)
            std::tm tm = {};
            strptime(time_str, "%Y%m%d%H%M%SZ", &tm);
            token.timestamp_unix_ms = static_cast<uint64_t>(timegm(&tm)) * 1000;
        }
        
        // Get serial number
        ASN1_INTEGER* serial = TS_TST_INFO_get_serial(tst_info);
        if (serial) {
            BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
            char* hex = BN_bn2hex(bn);
            token.serial_number = hex;
            OPENSSL_free(hex);
            BN_free(bn);
        }
        
        // Get policy OID
        ASN1_OBJECT* policy = TS_TST_INFO_get_policy_id(tst_info);
        if (policy) {
            char oid_buf[128];
            OBJ_obj2txt(oid_buf, sizeof(oid_buf), policy, 1);
            token.policy_oid = oid_buf;
        }
        
        TS_TST_INFO_free(tst_info);
    }
    
    token.success = true;
    token.verified = false;  // Needs separate verification
    
    TS_RESP_free(resp);
    
    THEMIS_INFO("Timestamp token received: serial={}, time={}", 
                token.serial_number, token.timestamp_utc);
    
    return token;
}

TimestampToken TimestampAuthority::getTimestamp(const std::vector<uint8_t>& data) {
    // Hash data
    auto hash = computeHash(data);
    return getTimestampForHash(hash);
}

TimestampToken TimestampAuthority::getTimestampForHash(
    const std::vector<uint8_t>& hash) {
    
    TimestampToken token;
    
    // Generate nonce
    auto nonce = generateNonce(8);
    
    // Create TSP request
    auto request = createTSPRequest(hash, nonce);
    if (request.empty()) {
        token.error_message = "Failed to create TSP request";
        return token;
    }
    
    // Send request to TSA
    auto response = sendTSPRequest(request);
    if (response.empty()) {
        token.error_message = last_error_;
        return token;
    }
    
    // Parse response
    token = parseTSPResponse(response);
    token.nonce = nonce;
    
    return token;
}

bool TimestampAuthority::verifyTimestamp(const std::vector<uint8_t>& data,
                                         const TimestampToken& token) {
    auto hash = computeHash(data);
    return verifyTimestampForHash(hash, token);
}

bool TimestampAuthority::verifyTimestampForHash(const std::vector<uint8_t>& hash,
                                                const TimestampToken& token) {
    if (token.token_der.empty()) {
        THEMIS_ERROR("Empty timestamp token");
        return false;
    }
    
    // Parse PKCS7 token
    const unsigned char* p = token.token_der.data();
    PKCS7* pkcs7 = d2i_PKCS7(nullptr, &p, token.token_der.size());
    if (!pkcs7) {
        THEMIS_ERROR("Failed to parse timestamp token");
        return false;
    }
    
    // Get TST_INFO
    TS_TST_INFO* tst_info = PKCS7_to_TS_TST_INFO(pkcs7);
    if (!tst_info) {
        PKCS7_free(pkcs7);
        return false;
    }
    
    // Verify message imprint
    TS_MSG_IMPRINT* imprint = TS_TST_INFO_get_msg_imprint(tst_info);
    ASN1_OCTET_STRING* hash_string = TS_MSG_IMPRINT_get_msg(imprint);
    
    bool hash_match = (hash.size() == (size_t)hash_string->length &&
                       memcmp(hash.data(), hash_string->data, hash.size()) == 0);
    
    TS_TST_INFO_free(tst_info);
    PKCS7_free(pkcs7);
    
    if (!hash_match) {
        THEMIS_ERROR("Timestamp hash mismatch");
        return false;
    }
    
    THEMIS_INFO("Timestamp verification successful");
    return true;
}

TimestampToken TimestampAuthority::parseToken(const std::vector<uint8_t>& token_data) {
    return parseTSPResponse(token_data);
}

TimestampToken TimestampAuthority::parseToken(const std::string& token_b64) {
    auto token_der = base64_decode(token_b64);
    return parseToken(token_der);
}

std::optional<std::string> TimestampAuthority::getTSACertificate() {
    // Stub implementation - in production, extract from TS_RESP
    THEMIS_WARN("getTSACertificate() not fully implemented");
    return std::nullopt;
}

bool TimestampAuthority::isAvailable() {
    if (!impl_->curl) {
        return false;
    }
    
    // Simple HTTP HEAD request to check availability
    curl_easy_setopt(impl_->curl, CURLOPT_URL, config_.url.c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT, 5);
    
    CURLcode res = curl_easy_perform(impl_->curl);
    
    return (res == CURLE_OK);
}

std::string TimestampAuthority::getLastError() const {
    return last_error_;
}

// eIDASTimestampValidator implementation

bool eIDASTimestampValidator::validateeIDASTimestamp(
    const TimestampToken& token,
    const std::vector<std::string>& trust_anchors) {
    
    validation_errors_.clear();
    
    if (!token.success) {
        validation_errors_.push_back("Token not successfully obtained");
        return false;
    }
    
    if (token.token_der.empty()) {
        validation_errors_.push_back("Empty token data");
        return false;
    }
    
    // Stub implementation - in production:
    // 1. Verify timestamp signature
    // 2. Validate TSA certificate chain
    // 3. Check certificate revocation (CRL/OCSP)
    // 4. Verify TSA is qualified (QTSP)
    
    THEMIS_WARN("eIDAS timestamp validation not fully implemented");
    
    return validation_errors_.empty();
}

bool eIDASTimestampValidator::validateAge(const TimestampToken& token, 
                                          int max_age_days) {
    if (token.timestamp_unix_ms == 0) {
        return false;
    }
    
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    uint64_t age_ms = now_ms - token.timestamp_unix_ms;
    uint64_t age_days = age_ms / (1000 * 60 * 60 * 24);
    
    return age_days <= static_cast<uint64_t>(max_age_days);
}

bool eIDASTimestampValidator::isQualifiedTSA(
    const std::string& tsa_cert,
    const std::vector<std::string>& qtsp_list) {
    
    // Stub implementation - in production, parse certificate and check against QTSP list
    THEMIS_WARN("isQualifiedTSA() not implemented");
    return false;
}

std::vector<std::string> eIDASTimestampValidator::getValidationErrors() const {
    return validation_errors_;
}

} // namespace security
} // namespace themis
