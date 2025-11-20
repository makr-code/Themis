// Clean minimal stub implementation of HSMProvider.
// Provides deterministic, nicht-kryptographische Operationen fuer Developer-Fallback.
// Wird nur eingebaut, wenn THEMIS_ENABLE_HSM_REAL NICHT definiert ist.

#ifdef THEMIS_ENABLE_HSM_REAL
// Real PKCS#11 Implementierung in hsm_provider_pkcs11.cpp
#else

#include "security/hsm_provider.h"
#include "utils/logger.h"
#include <sstream>
#include <chrono>

namespace themis { namespace security {

class HSMProvider::Impl { };

static std::string to_hex(const std::vector<uint8_t>& data) {
    static const char* d = "0123456789abcdef";
    std::string out; out.reserve(data.size()*2);
    for (auto b : data) { out.push_back(d[(b>>4)&0xF]); out.push_back(d[b&0xF]); }
    return out;
}

static std::string pseudo_b64(const std::vector<uint8_t>& data) {
    return std::string("hex:") + to_hex(data);
}

HSMProvider::HSMProvider(HSMConfig config)
    : impl_(std::make_unique<Impl>()), config_(std::move(config)) {}

HSMProvider::~HSMProvider() = default;
HSMProvider::HSMProvider(HSMProvider&&) noexcept = default;
HSMProvider& HSMProvider::operator=(HSMProvider&&) noexcept = default;

bool HSMProvider::initialize() {
    if (initialized_) return true;
    initialized_ = true;
    THEMIS_INFO("HSMProvider stub initialized (label='{}')", config_.key_label);
    return true;
}

void HSMProvider::finalize() {
    initialized_ = false;
    impl_.reset();
    THEMIS_INFO("HSMProvider stub finalized");
}

HSMSignatureResult HSMProvider::sign(const std::vector<uint8_t>& data, const std::string& key_label) {
    return signHash(data, key_label); // treat data as pre-hash
}

HSMSignatureResult HSMProvider::signHash(const std::vector<uint8_t>& hash, const std::string& key_label) {
    HSMSignatureResult r;
    if (!initialized_) { r.error_message = "HSM stub not initialized"; return r; }
    r.success = true;
    r.signature_b64 = pseudo_b64(hash);
    r.algorithm = config_.signature_algorithm;
    r.key_id = key_label.empty() ? config_.key_label : key_label;
    r.cert_serial = "STUB-CERT";
    r.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return r;
}

bool HSMProvider::verify(const std::vector<uint8_t>& data, const std::string& signature_b64, const std::string& key_label) {
    auto expected = pseudo_b64(data);
    bool ok = (expected == signature_b64);
    THEMIS_DEBUG("HSMProvider stub verify key='{}' ok={}", key_label.empty()?config_.key_label:key_label, ok);
    return ok;
}

std::vector<HSMKeyInfo> HSMProvider::listKeys() {
    HSMKeyInfo info;
    info.label = config_.key_label;
    info.id = "stub-id";
    info.algorithm = config_.signature_algorithm;
    info.can_sign = true;
    info.can_verify = true;
    info.extractable = false;
    info.key_size = 0;
    return {info};
}

bool HSMProvider::generateKeyPair(const std::string& label, uint32_t key_size, bool extractable) {
    (void)key_size; (void)extractable; // Stub: unused
    THEMIS_WARN("HSMProvider stub generateKeyPair ignored (label='{}')", label);
    return false;
}

bool HSMProvider::importCertificate(const std::string& key_label, const std::string& cert_pem) {
    (void)cert_pem; // Stub: unused
    THEMIS_WARN("HSMProvider stub importCertificate ignored (key='{}')", key_label);
    return false;
}

std::optional<std::string> HSMProvider::getCertificate(const std::string& key_label) {
    (void)key_label; // Stub: unused
    return std::string("-----BEGIN CERTIFICATE-----\nSTUB\n-----END CERTIFICATE-----\n");
}

bool HSMProvider::isReady() const { return initialized_; }

std::string HSMProvider::getTokenInfo() const {
    std::ostringstream oss; oss << "HSM STUB label=" << config_.key_label << " ready=" << (initialized_?"true":"false");
    return oss.str();
}

std::string HSMProvider::getLastError() const { return last_error_; }

HSMPerformanceStats HSMProvider::getStats() const {
    // Stub: return empty stats
    return HSMPerformanceStats{};
}

void HSMProvider::resetStats() {
    // Stub: no-op
}

// HSMPKIClient
HSMPKIClient::HSMPKIClient(HSMConfig config) : hsm_(std::make_unique<HSMProvider>(std::move(config))) { hsm_->initialize(); }
HSMPKIClient::~HSMPKIClient() { if (hsm_) hsm_->finalize(); }
HSMSignatureResult HSMPKIClient::sign(const std::vector<uint8_t>& data) { return hsm_->sign(data); }
bool HSMPKIClient::verify(const std::vector<uint8_t>& data, const std::string& signature_b64) { return hsm_->verify(data, signature_b64); }
std::optional<std::string> HSMPKIClient::getCertSerial() { return std::string("STUB-SERIAL"); }
bool HSMPKIClient::isReady() const { return hsm_->isReady(); }

} } // namespace themis::security

#endif // !THEMIS_ENABLE_HSM_REAL
