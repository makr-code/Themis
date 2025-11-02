#pragma once

#include <string>
#include <vector>
#include <memory>

namespace themis {
namespace utils {

struct PKIConfig {
    std::string service_id;
    std::string endpoint;           // e.g. https://localhost:8443/api/v1
    std::string cert_path;          // optional: client cert path
    std::string key_path;           // optional: client key path
    std::string signature_algorithm = "RSA-SHA256";
};

struct SignatureResult {
    bool ok = false;
    std::string signature_id;       // opaque id from PKI
    std::string algorithm;          // e.g. RSA-SHA256
    std::string signature_b64;      // signature over provided hash (base64)
    std::string cert_serial;        // certificate serial used
};

// Minimal PKI client (stub) to sign/verify data hashes.
// Implementation is local-only for now; can later call a real REST API.
class VCCPKIClient {
public:
    explicit VCCPKIClient(PKIConfig cfg);

    // Sign a precomputed hash (e.g. SHA-256 over ciphertext batch)
    SignatureResult signHash(const std::vector<uint8_t>& hash_bytes) const;

    // Verify a signature against a precomputed hash
    bool verifyHash(const std::vector<uint8_t>& hash_bytes, const SignatureResult& sig) const;

    const PKIConfig& config() const { return cfg_; }

private:
    PKIConfig cfg_;
};

} // namespace utils
} // namespace themis
