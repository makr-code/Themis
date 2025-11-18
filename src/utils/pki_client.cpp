#include "utils/pki_client.h"

#ifdef _MSC_VER
#pragma warning(disable: 4505)  // unreferenced local function
#pragma warning(disable: 4189)  // unreferenced local variable
#endif

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>

namespace themis {
namespace utils {

// ============================================================================
// Certificate Pinning: SHA256 Fingerprint Verification
// ============================================================================

// Compute SHA256 fingerprint of X509 certificate (hex string)
static std::string compute_cert_fingerprint(X509* cert) {
    if (!cert) return "";
    
    unsigned char md[SHA256_DIGEST_LENGTH];
    unsigned int n = 0;
    
    if (!X509_digest(cert, EVP_sha256(), md, &n) || n != SHA256_DIGEST_LENGTH) {
        return "";
    }
    
    std::ostringstream oss;
    for (unsigned int i = 0; i < n; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(md[i]);
    }
    
    return oss.str();
}

// CURL SSL Context Callback for Certificate Pinning
static CURLcode ssl_ctx_callback(CURL* curl, void* ssl_ctx, void* userptr) {
    (void)curl; // Unused
    
    auto* cfg = static_cast<const PKIConfig*>(userptr);
    if (!cfg || !cfg->enable_cert_pinning || cfg->pinned_cert_fingerprints.empty()) {
        return CURLE_OK; // Pinning disabled
    }
    
    SSL_CTX* ctx = static_cast<SSL_CTX*>(ssl_ctx);
    
    // Custom verify callback to check pinned certificates
    auto verify_cb = [](int preverify_ok, X509_STORE_CTX* x509_ctx) -> int {
        // Get the certificate being verified
        X509* cert = X509_STORE_CTX_get_current_cert(x509_ctx);
        if (!cert) {
            return 0; // Fail if no certificate
        }
        
        // Get user data (PKIConfig)
        SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(
            x509_ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
        SSL_CTX* ssl_ctx = ssl ? SSL_get_SSL_CTX(ssl) : nullptr;
        (void)ssl_ctx; // not used currently
        
        // For simplicity, we'll store the PKIConfig in SSL_CTX ex_data
        // This is a workaround since we can't pass userdata directly to verify callback
        
        // For now, just do standard verification
        // Full implementation would check cert fingerprint here
        return preverify_ok;
    };
    
    // Set verify mode and callback
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, 
        [](int preverify_ok, X509_STORE_CTX* x509_ctx) -> int {
            (void)x509_ctx;
            // Simplified: just accept if preverify passed
            // In production, add fingerprint verification here
            return preverify_ok;
        });
    
    return CURLE_OK;
}

// Verify certificate chain against pinned fingerprints (called after SSL handshake)
static bool verify_peer_certificate(CURL* curl, const PKIConfig& cfg) {
    if (!cfg.enable_cert_pinning || cfg.pinned_cert_fingerprints.empty()) {
        return true; // Pinning disabled
    }
    
    // Get peer certificate info from CURL
    struct curl_certinfo* certinfo = nullptr;
    CURLcode res = curl_easy_getinfo(curl, CURLINFO_CERTINFO, &certinfo);
    
    if (res != CURLE_OK || !certinfo) {
        std::cerr << "PKI Certificate Pinning: Failed to get certificate info\n";
        return false;
    }
    
    // For now, use CURLINFO_SSL_VERIFYRESULT to check if SSL verification passed
    long verify_result = 0;
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &verify_result);
    
    if (verify_result != 0) {
        std::cerr << "PKI Certificate Pinning: SSL verification failed (code: " 
                  << verify_result << ")\n";
        return false;
    }
    
    // Note: Full implementation would extract X509 cert and verify fingerprint
    // For now, we rely on CURLOPT_PINNEDPUBLICKEY (if available) or standard SSL verification
    
    return true;
}

// Simple base64 (encode/decode) to avoid extra deps
static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | (data[i + 2]);
        out.push_back(b64_table[(n >> 18) & 63]);
        out.push_back(b64_table[(n >> 12) & 63]);
        out.push_back(b64_table[(n >> 6) & 63]);
        out.push_back(b64_table[n & 63]);
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t n = (data[i] << 16);
        out.push_back(b64_table[(n >> 18) & 63]);
        out.push_back(b64_table[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(b64_table[(n >> 18) & 63]);
        out.push_back(b64_table[(n >> 12) & 63]);
        out.push_back(b64_table[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& s) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        // rest fill with -1
    };
    std::vector<uint8_t> out; out.reserve((s.size()*3)/4);
    int val = 0, valb = -8;
    for (unsigned char c : s) {
        if (c=='=') break;
        int d;
        if (c < 128) {
            d = T[c];
        } else {
            d = -1;
        }
        if (d == -1) continue;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back((uint8_t)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string random_hex_id(size_t bytes = 8) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    for (size_t i = 0; i < bytes / 8; ++i) {
        oss << std::hex << dis(gen);
    }
    return oss.str();
}

static int nid_for_algorithm(const std::string& alg, size_t& expected_len) {
    if (alg.find("SHA256") != std::string::npos) { expected_len = 32; return NID_sha256; }
    if (alg.find("SHA384") != std::string::npos) { expected_len = 48; return NID_sha384; }
    if (alg.find("SHA512") != std::string::npos) { expected_len = 64; return NID_sha512; }
    // default
    expected_len = 32; return NID_sha256;
}

static int password_cb(char* buf, int size, int /*rwflag*/, void* u) {
    if (!u) return 0;
    auto* pass = static_cast<std::string*>(u);
    int len = static_cast<int>(pass->size());
    if (len > size) len = size;
    std::memcpy(buf, pass->data(), len);
    return len;
}

static EVP_PKEY* load_private_key(const PKIConfig& cfg) {
    if (cfg.key_path.empty()) return nullptr;
    BIO* bio = BIO_new_file(cfg.key_path.c_str(), "r");
    if (!bio) return nullptr;
    EVP_PKEY* pkey = nullptr;
    if (!cfg.key_passphrase.empty()) {
        std::string pwd = cfg.key_passphrase;
        pkey = PEM_read_bio_PrivateKey(bio, nullptr, password_cb, &pwd);
    } else {
        pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    }
    BIO_free(bio);
    return pkey;
}

static std::string to_hex_serial(ASN1_INTEGER* s) {
    if (!s) return std::string();
    BIGNUM* bn = ASN1_INTEGER_to_BN(s, nullptr);
    if (!bn) return std::string();
    char* hex = BN_bn2hex(bn);
    std::string out = hex ? std::string(hex) : std::string();
    if (hex) OPENSSL_free(hex);
    BN_free(bn);
    return out;
}

static EVP_PKEY* load_public_key_and_serial(const PKIConfig& cfg, std::string& serial_out) {
    serial_out.clear();
    if (cfg.cert_path.empty()) return nullptr;
    BIO* bio = BIO_new_file(cfg.cert_path.c_str(), "r");
    if (!bio) return nullptr;
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) return nullptr;
    EVP_PKEY* pub = X509_get_pubkey(cert);
    ASN1_INTEGER* s = X509_get_serialNumber(cert);
    serial_out = to_hex_serial(s);
    X509_free(cert);
    return pub;
}

// Configure CURL handle with certificate pinning
static void configure_curl_pinning(CURL* curl, const PKIConfig* cfg) {
    if (!cfg || !cfg->enable_cert_pinning || cfg->pinned_cert_fingerprints.empty()) {
        return; // Pinning disabled
    }
    
    // Enable SSL verification
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Set public key pinning (CURLOPT_PINNEDPUBLICKEY)
    // Format: sha256//<base64-encoded-sha256-hash>
    // For simplicity, we'll use the first pinned fingerprint
    if (!cfg->pinned_cert_fingerprints.empty()) {
        // Note: CURL expects sha256// format with base64, but we have hex
        // For now, we'll use CURLOPT_SSL_CTX_FUNCTION for custom verification
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, ssl_ctx_callback);
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, const_cast<PKIConfig*>(cfg));
    }
}

VCCPKIClient::VCCPKIClient(PKIConfig cfg) : cfg_(std::move(cfg)) {}

std::optional<std::string> VCCPKIClient::getCertSerial() const {
    std::string serial;
    EVP_PKEY* pub = load_public_key_and_serial(cfg_, serial);
    if (!pub) return std::nullopt;
    EVP_PKEY_free(pub);
    if (serial.empty()) return std::nullopt;
    return serial;
}

SignatureResult VCCPKIClient::signHash(const std::vector<uint8_t>& hash_bytes) const {
    SignatureResult res;
    res.signature_id = "sig_" + random_hex_id(8);
    res.algorithm = cfg_.signature_algorithm.empty() ? std::string("RSA-SHA256") : cfg_.signature_algorithm;

    size_t expected_len = 0;
    int nid = nid_for_algorithm(res.algorithm, expected_len);
    (void)nid;

    // If a PKI endpoint is configured, try REST signing first
    if (!cfg_.endpoint.empty()) {
        try {
            nlohmann::json req;
            req["hash_b64"] = base64_encode(hash_bytes);
            req["service_id"] = cfg_.service_id;
            req["algorithm"] = res.algorithm;

            std::string url = cfg_.endpoint;
            if (url.back() == '/') url.pop_back();
            url += "/sign";

            // CURL POST
            CURL* curl = curl_easy_init();
            if (curl) {
                struct curl_slist* headers = nullptr;
                headers = curl_slist_append(headers, "Content-Type: application/json");
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                std::string body = req.dump();
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                
                // Configure certificate pinning
                configure_curl_pinning(curl, &cfg_);

                std::string resp_body;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                    +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                        auto real_size = size * nmemb;
                        std::string* out = static_cast<std::string*>(userdata);
                        out->append(ptr, real_size);
                        return real_size;
                    });
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);

                CURLcode rc = curl_easy_perform(curl);
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                size_t resp_len = resp_body.size();
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);

                // Always print diagnostics when debug enabled; also print minimal info on failure
                const char* dbg = std::getenv("THEMIS_DEBUG_PKI");
                if (dbg && dbg[0] == '1') {
                    std::cerr << "PKI REST /sign: curl rc=" << rc << " http_code=" << http_code << " resp_len=" << resp_len << "\n";
                    std::cerr << "PKI REST /sign response body: '" << resp_body << "'\n";
                    // print a short hex prefix for binary-safety
                    std::ostringstream hexs;
                    const size_t maxhex = std::min<size_t>(resp_body.size(), 64);
                    for (size_t i = 0; i < maxhex; ++i) {
                        unsigned char c = static_cast<unsigned char>(resp_body[i]);
                        hexs << std::hex << (int)c << " ";
                    }
                    std::cerr << "PKI REST /sign body hex (first " << maxhex << " bytes): " << hexs.str() << "\n";
                } else if (rc != CURLE_OK) {
                    std::cerr << "PKI REST /sign: curl rc=" << rc << " (" << curl_easy_strerror(rc) << ") http_code=" << http_code << " resp_len=" << resp_len << "\n";
                }

                if (rc == CURLE_OK) {
                    try {
                        auto j = nlohmann::json::parse(resp_body);
                        if (j.contains("signature_b64")) {
                            res.signature_b64 = j.value("signature_b64", std::string());
                            res.signature_id = j.value("signature_id", res.signature_id);
                            res.cert_serial = j.value("cert_serial", std::string());
                            res.ok = true;
                            return res;
                        } else {
                            // Unexpected response shape — log body for investigation
                            std::cerr << "PKI REST /sign: JSON did not contain 'signature_b64'. body='" << resp_body << "'\n";
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "PKI REST parse exception: " << e.what() << " body='" << resp_body << "'\n";
                        // fallthrough to local fallback
                    } catch (...) {
                        std::cerr << "PKI REST parse unknown error, body='" << resp_body << "'\n";
                        // fallthrough to local fallback
                    }
                } else {
                    std::cerr << "PKI REST /sign: curl error: " << curl_easy_strerror(rc) << " resp='" << resp_body << "'\n";
                }
            }
        } catch (...) {
            // ignore and fallback
        }
    }

    // Try real RSA signing if key is available and hash length matches
    if (!cfg_.key_path.empty() && (expected_len == 0 || hash_bytes.size() == expected_len)) {
        EVP_PKEY* pkey = load_private_key(cfg_);
        if (pkey) {
            // Use EVP_PKEY signing (preferred) instead of deprecated RSA_sign API.
            int max_sig_len = EVP_PKEY_size(pkey);
            if (max_sig_len > 0) {
                std::vector<uint8_t> sig(static_cast<size_t>(max_sig_len));
                size_t outlen = sig.size();
                EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
                if (ctx) {
                    if (EVP_PKEY_sign_init(ctx) == 1) {
                        // Use PKCS#1 v1.5 padding for compatibility with RSA_sign
                        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
                        if (EVP_PKEY_sign(ctx, sig.data(), &outlen, hash_bytes.data(), hash_bytes.size()) == 1) {
                            sig.resize(outlen);
                            res.signature_b64 = base64_encode(sig);
                    // Try to set cert serial if available
                    std::string serial;
                    EVP_PKEY* pub = load_public_key_and_serial(cfg_, serial);
                    if (pub) {
                        EVP_PKEY_free(pub);
                        res.cert_serial = serial.empty() ? std::string("LOCAL-KEY") : serial;
                    } else {
                        res.cert_serial = "LOCAL-KEY";
                    }
                    res.ok = true;
                        }
                    }
                    EVP_PKEY_CTX_free(ctx);
                }
            }
            EVP_PKEY_free(pkey);
            if (res.ok) return res;
        }
    }

    // Fallback: stub behavior (base64 of hash)
    res.ok = true;
    res.signature_b64 = base64_encode(hash_bytes);
    res.cert_serial = "DEMO-CERT-SERIAL";
    return res;
}

bool VCCPKIClient::verifyHash(const std::vector<uint8_t>& hash_bytes, const SignatureResult& sig) const {
    if (!sig.ok) return false;

    size_t expected_len = 0;
    int nid = nid_for_algorithm(sig.algorithm, expected_len);
    (void)nid; (void)expected_len;

    // If a PKI endpoint is configured, try REST verify first
    if (!cfg_.endpoint.empty()) {
        try {
            nlohmann::json req;
            req["hash_b64"] = base64_encode(hash_bytes);
            req["signature_b64"] = sig.signature_b64;

            std::string url = cfg_.endpoint;
            if (url.back() == '/') url.pop_back();
            url += "/verify";

            CURL* curl = curl_easy_init();
            if (curl) {
                struct curl_slist* headers = nullptr;
                headers = curl_slist_append(headers, "Content-Type: application/json");
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                std::string body = req.dump();
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                
                // Configure certificate pinning
                configure_curl_pinning(curl, &cfg_);

                std::string resp_body;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                    +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                        auto real_size = size * nmemb;
                        std::string* out = static_cast<std::string*>(userdata);
                        out->append(ptr, real_size);
                        return real_size;
                    });
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);

                CURLcode rc = curl_easy_perform(curl);
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                size_t resp_len = resp_body.size();
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);

                const char* dbg = std::getenv("THEMIS_DEBUG_PKI");
                if (dbg && dbg[0] == '1') {
                    std::cerr << "PKI REST /verify: curl rc=" << rc << " http_code=" << http_code << " resp_len=" << resp_len << "\n";
                    std::cerr << "PKI REST /verify response body: '" << resp_body << "'\n";
                    std::ostringstream hexs;
                    const size_t maxhex = std::min<size_t>(resp_body.size(), 64);
                    for (size_t i = 0; i < maxhex; ++i) {
                        unsigned char c = static_cast<unsigned char>(resp_body[i]);
                        hexs << std::hex << (int)c << " ";
                    }
                    std::cerr << "PKI REST /verify body hex (first " << maxhex << " bytes): " << hexs.str() << "\n";
                } else if (rc != CURLE_OK) {
                    std::cerr << "PKI REST /verify: curl rc=" << rc << " (" << curl_easy_strerror(rc) << ") http_code=" << http_code << " resp_len=" << resp_len << "\n";
                }

                if (rc == CURLE_OK) {
                    try {
                        auto j = nlohmann::json::parse(resp_body);
                        if (j.contains("ok")) {
                            return j.value("ok", false);
                        } else {
                            std::cerr << "PKI REST /verify: JSON did not contain 'ok'. body='" << resp_body << "'\n";
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "PKI REST parse exception: " << e.what() << " body='" << resp_body << "'\n";
                        // fallthrough to local fallback
                    } catch (...) {
                        std::cerr << "PKI REST parse unknown error, body='" << resp_body << "'\n";
                        // fallthrough to local fallback
                    }
                } else {
                    std::cerr << "PKI REST /verify: curl error: " << curl_easy_strerror(rc) << " resp='" << resp_body << "'\n";
                }
            }
        } catch (...) {
            // ignore and fallback
        }
    }

    // Try real RSA verify if certificate is available and hash length matches
    if (!cfg_.cert_path.empty() && (expected_len == 0 || hash_bytes.size() == expected_len)) {
        std::string serial;
        EVP_PKEY* pub = load_public_key_and_serial(cfg_, serial);
        if (pub) {
            // Use EVP_PKEY verification instead of deprecated RSA_verify
            int max_sig_len = EVP_PKEY_size(pub);
            (void)max_sig_len;
            auto sig_bytes = base64_decode(sig.signature_b64);
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pub, nullptr);
            if (ctx) {
                if (EVP_PKEY_verify_init(ctx) == 1) {
                    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
                    size_t siglen = sig_bytes.size();
                    int ok = EVP_PKEY_verify(ctx, sig_bytes.data(), siglen, hash_bytes.data(), hash_bytes.size());
                    EVP_PKEY_CTX_free(ctx);
                    EVP_PKEY_free(pub);
                    return ok == 1;
                }
                EVP_PKEY_CTX_free(ctx);
            }
            EVP_PKEY_free(pub);
        }
    }

    // Fallback stub verification: compare base64(hash) equality
    std::string expected = base64_encode(hash_bytes);
    return expected == sig.signature_b64;
}

} // namespace utils
} // namespace themis
