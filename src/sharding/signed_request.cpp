#include "sharding/signed_request.h"
#include "sharding/pki_shard_certificate.h"
#include <sstream>
#include <chrono>
#include <random>
#include <algorithm>

// OpenSSL for signing
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace themis::sharding {

namespace {
    // Base64 encode helper
    std::string base64Encode(const unsigned char* data, size_t len) {
        BIO* bio = BIO_new(BIO_s_mem());
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64, bio);
        
        BIO_write(bio, data, static_cast<int>(len));
        BIO_flush(bio);
        
        BUF_MEM* buffer_ptr;
        BIO_get_mem_ptr(bio, &buffer_ptr);
        
        std::string result(buffer_ptr->data, buffer_ptr->length);
        BIO_free_all(bio);
        
        return result;
    }
    
    // Base64 decode helper
    std::optional<std::vector<unsigned char>> base64Decode(const std::string& encoded) {
        BIO* bio = BIO_new_mem_buf(encoded.c_str(), static_cast<int>(encoded.size()));
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64, bio);
        
        std::vector<unsigned char> decoded(encoded.size());
        int decoded_len = BIO_read(bio, decoded.data(), static_cast<int>(decoded.size()));
        BIO_free_all(bio);
        
        if (decoded_len < 0) {
            return std::nullopt;
        }
        
        decoded.resize(decoded_len);
        return decoded;
    }
}

// ============================================================================
// SignedRequest
// ============================================================================

nlohmann::json SignedRequest::toJSON() const {
    return nlohmann::json{
        {"shard_id", shard_id},
        {"operation", operation},
        {"path", path},
        {"body", body},
        {"timestamp_ms", timestamp_ms},
        {"nonce", nonce},
        {"signature_b64", signature_b64},
        {"cert_serial", cert_serial}
    };
}

std::optional<SignedRequest> SignedRequest::fromJSON(const nlohmann::json& j) {
    try {
        SignedRequest req;
        req.shard_id = j.at("shard_id").get<std::string>();
        req.operation = j.at("operation").get<std::string>();
        req.path = j.at("path").get<std::string>();
        req.body = j.value("body", nlohmann::json{});
        req.timestamp_ms = j.at("timestamp_ms").get<uint64_t>();
        req.nonce = j.at("nonce").get<uint64_t>();
        req.signature_b64 = j.at("signature_b64").get<std::string>();
        req.cert_serial = j.at("cert_serial").get<std::string>();
        
        return req;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::string SignedRequest::getCanonicalString() const {
    std::ostringstream oss;
    oss << shard_id << "|"
        << operation << "|"
        << path << "|"
        << body.dump() << "|"
        << timestamp_ms << "|"
        << nonce;
    return oss.str();
}

// ============================================================================
// SignedRequestSigner
// ============================================================================

SignedRequestSigner::SignedRequestSigner(const Config& config)
    : config_(config) {
    // Extract certificate serial
    auto cert_info = PKIShardCertificate::parseCertificate(config.cert_path);
    if (cert_info) {
        cert_serial_ = cert_info->serial_number;
    }
}

bool SignedRequestSigner::sign(SignedRequest& request) {
    // Set shard ID
    request.shard_id = config_.shard_id;
    
    // Set timestamp
    request.timestamp_ms = getCurrentTimestampMs();
    
    // Generate nonce
    request.nonce = generateNonce();
    
    // Set certificate serial
    request.cert_serial = cert_serial_;
    
    // Create canonical string
    std::string canonical = request.getCanonicalString();
    
    // Sign
    auto signature = signData(canonical);
    if (!signature) {
        return false;
    }
    
    request.signature_b64 = *signature;
    return true;
}

SignedRequest SignedRequestSigner::createSignedRequest(const std::string& operation,
                                                       const std::string& path,
                                                       const nlohmann::json& body) {
    SignedRequest request;
    request.operation = operation;
    request.path = path;
    request.body = body;
    
    sign(request);
    return request;
}

uint64_t SignedRequestSigner::generateNonce() const {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    return dis(gen);
}

uint64_t SignedRequestSigner::getCurrentTimestampMs() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

std::optional<std::string> SignedRequestSigner::signData(const std::string& data) {
    // Read private key
    FILE* key_file = fopen(config_.key_path.c_str(), "r");
    if (!key_file) {
        return std::nullopt;
    }
    
    EVP_PKEY* pkey = nullptr;
    if (!config_.key_passphrase.empty()) {
        pkey = PEM_read_PrivateKey(key_file, nullptr, nullptr,
                                   const_cast<char*>(config_.key_passphrase.c_str()));
    } else {
        pkey = PEM_read_PrivateKey(key_file, nullptr, nullptr, nullptr);
    }
    fclose(key_file);
    
    if (!pkey) {
        return std::nullopt;
    }
    
    // Create signature context
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }
    
    // Initialize signing
    if (EVP_DigestSignInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }
    
    // Update with data
    if (EVP_DigestSignUpdate(md_ctx, data.c_str(), data.size()) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }
    
    // Get signature length
    size_t sig_len = 0;
    if (EVP_DigestSignFinal(md_ctx, nullptr, &sig_len) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }
    
    // Get signature
    std::vector<unsigned char> signature(sig_len);
    if (EVP_DigestSignFinal(md_ctx, signature.data(), &sig_len) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }
    
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
    
    // Base64 encode signature
    return base64Encode(signature.data(), sig_len);
}

// ============================================================================
// SignedRequestVerifier
// ============================================================================

SignedRequestVerifier::SignedRequestVerifier(const Config& config)
    : config_(config) {
}

bool SignedRequestVerifier::verify(const SignedRequest& request,
                                   const std::string& expected_shard_id) {
    // 1. Verify timestamp
    if (!verifyTimestamp(request.timestamp_ms)) {
        return false;
    }
    
    // 2. Verify nonce (replay protection)
    if (!verifyNonce(request.nonce, request.timestamp_ms)) {
        return false;
    }
    
    // 3. Verify expected shard ID if provided
    if (!expected_shard_id.empty() && request.shard_id != expected_shard_id) {
        return false;
    }
    
    // 4. Verify signature
    if (!verifySignature(request)) {
        return false;
    }
    
    return true;
}

void SignedRequestVerifier::cleanupExpiredNonces() {
    std::lock_guard<std::mutex> lock(nonce_mutex_);
    
    uint64_t current_time = getCurrentTimestampMs();
    uint64_t expiry_threshold = current_time - config_.nonce_expiry_ms;
    
    // In production, would track (nonce, timestamp) pairs
    // For Phase 2, we keep it simple
    if (seen_nonces_.size() > config_.max_nonce_cache) {
        seen_nonces_.clear();
    }
}

bool SignedRequestVerifier::verifyTimestamp(uint64_t timestamp_ms) const {
    uint64_t current_time = getCurrentTimestampMs();
    uint64_t time_diff = (current_time > timestamp_ms) ?
        (current_time - timestamp_ms) : (timestamp_ms - current_time);
    
    return time_diff <= config_.max_time_skew_ms;
}

bool SignedRequestVerifier::verifyNonce(uint64_t nonce, uint64_t timestamp_ms) {
    std::lock_guard<std::mutex> lock(nonce_mutex_);
    
    // Check if nonce was seen before
    if (seen_nonces_.find(nonce) != seen_nonces_.end()) {
        return false; // Replay attack detected
    }
    
    // Add nonce to seen set
    seen_nonces_.insert(nonce);
    
    // Cleanup if cache is too large
    if (seen_nonces_.size() > config_.max_nonce_cache) {
        // Remove oldest half (simplified cleanup)
        auto it = seen_nonces_.begin();
        std::advance(it, config_.max_nonce_cache / 2);
        seen_nonces_.erase(seen_nonces_.begin(), it);
    }
    
    return true;
}

bool SignedRequestVerifier::verifySignature(const SignedRequest& request) {
    // For Phase 2, we provide the structure
    // Full implementation would:
    // 1. Load certificate by serial number
    // 2. Extract public key
    // 3. Verify signature against canonical string
    // 4. Check certificate is not revoked (CRL)
    
    // Decode signature
    auto signature_bytes = base64Decode(request.signature_b64);
    if (!signature_bytes) {
        return false;
    }
    
    // Get canonical string
    std::string canonical = request.getCanonicalString();
    
    // In production: verify signature with public key from certificate
    // For now, return true if signature is present
    return !request.signature_b64.empty();
}

uint64_t SignedRequestVerifier::getCurrentTimestampMs() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

} // namespace themis::sharding
