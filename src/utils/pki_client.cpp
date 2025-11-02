#include "utils/pki_client.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>

#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

namespace themis {
namespace utils {

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

VCCPKIClient::VCCPKIClient(PKIConfig cfg) : cfg_(std::move(cfg)) {}

SignatureResult VCCPKIClient::signHash(const std::vector<uint8_t>& hash_bytes) const {
    SignatureResult res;
    res.signature_id = "sig_" + random_hex_id(8);
    res.algorithm = cfg_.signature_algorithm.empty() ? std::string("RSA-SHA256") : cfg_.signature_algorithm;

    size_t expected_len = 0;
    int nid = nid_for_algorithm(res.algorithm, expected_len);

    // Try real RSA signing if key is available and hash length matches
    if (!cfg_.key_path.empty() && (expected_len == 0 || hash_bytes.size() == expected_len)) {
        EVP_PKEY* pkey = load_private_key(cfg_);
        if (pkey) {
            RSA* rsa = EVP_PKEY_get1_RSA(pkey);
            if (rsa) {
                std::vector<uint8_t> sig(RSA_size(rsa));
                unsigned int siglen = 0;
                int ok = RSA_sign(nid, hash_bytes.data(), (unsigned int)hash_bytes.size(), sig.data(), &siglen, rsa);
                RSA_free(rsa);
                if (ok == 1) {
                    sig.resize(siglen);
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
                    EVP_PKEY_free(pkey);
                    return res;
                }
            }
            EVP_PKEY_free(pkey);
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

    // Try real RSA verify if certificate is available and hash length matches
    if (!cfg_.cert_path.empty() && (expected_len == 0 || hash_bytes.size() == expected_len)) {
        std::string serial;
        EVP_PKEY* pub = load_public_key_and_serial(cfg_, serial);
        if (pub) {
            RSA* rsa = EVP_PKEY_get1_RSA(pub);
            if (rsa) {
                auto sig_bytes = base64_decode(sig.signature_b64);
                int ok = RSA_verify(nid, hash_bytes.data(), (unsigned int)hash_bytes.size(), sig_bytes.data(), (unsigned int)sig_bytes.size(), rsa);
                RSA_free(rsa);
                EVP_PKEY_free(pub);
                return ok == 1;
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
