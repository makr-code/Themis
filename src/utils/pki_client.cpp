#include "utils/pki_client.h"

#include <openssl/sha.h>
#include <random>
#include <sstream>

namespace themis {
namespace utils {

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

VCCPKIClient::VCCPKIClient(PKIConfig cfg) : cfg_(std::move(cfg)) {}

SignatureResult VCCPKIClient::signHash(const std::vector<uint8_t>& hash_bytes) const {
    // Stub: simply base64-encode the provided hash and return a fake signature id
    SignatureResult res;
    res.ok = true;
    res.signature_id = "sig_" + random_hex_id(8);
    res.algorithm = cfg_.signature_algorithm.empty() ? std::string("RSA-SHA256") : cfg_.signature_algorithm;
    res.signature_b64 = base64_encode(hash_bytes);
    res.cert_serial = "DEMO-CERT-SERIAL";
    return res;
}

bool VCCPKIClient::verifyHash(const std::vector<uint8_t>& hash_bytes, const SignatureResult& sig) const {
    if (!sig.ok) return false;
    // Stub verification: recompute base64 of hash and compare
    std::string expected = base64_encode(hash_bytes);
    return expected == sig.signature_b64;
}

} // namespace utils
} // namespace themis
