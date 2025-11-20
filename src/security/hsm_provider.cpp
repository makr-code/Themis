// Minimal Windows-friendly stub implementation of HSMProvider.
// Provides deterministic, non-cryptographic operations sufficient for tests.

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
    THEMIS_WARN("HSMProvider stub generateKeyPair ignored (label='{}')", label);
    return false;
}

bool HSMProvider::importCertificate(const std::string& key_label, const std::string& cert_pem) {
    THEMIS_WARN("HSMProvider stub importCertificate ignored (key='{}')", key_label);
    return false;
}

std::optional<std::string> HSMProvider::getCertificate(const std::string& key_label) {
    return std::string("-----BEGIN CERTIFICATE-----\nSTUB\n-----END CERTIFICATE-----\n");
}

bool HSMProvider::isReady() const { return initialized_; }

std::string HSMProvider::getTokenInfo() const {
    std::ostringstream oss; oss << "HSM STUB label=" << config_.key_label << " ready=" << (initialized_?"true":"false");
    return oss.str();
}

std::string HSMProvider::getLastError() const { return last_error_; }

// HSMPKIClient
HSMPKIClient::HSMPKIClient(HSMConfig config) : hsm_(std::make_unique<HSMProvider>(std::move(config))) { hsm_->initialize(); }
HSMPKIClient::~HSMPKIClient() { if (hsm_) hsm_->finalize(); }
HSMSignatureResult HSMPKIClient::sign(const std::vector<uint8_t>& data) { return hsm_->sign(data); }
bool HSMPKIClient::verify(const std::vector<uint8_t>& data, const std::string& signature_b64) { return hsm_->verify(data, signature_b64); }
std::optional<std::string> HSMPKIClient::getCertSerial() { return std::string("STUB-SERIAL"); }
bool HSMPKIClient::isReady() const { return hsm_->isReady(); }

} } // namespace themis::security// Minimal Windows-friendly stub implementation of HSMProvider.
// Provides deterministic, non-cryptographic operations sufficient for tests.

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
    THEMIS_WARN("HSMProvider stub generateKeyPair ignored (label='{}')", label);
    return false;
}

bool HSMProvider::importCertificate(const std::string& key_label, const std::string& cert_pem) {
    THEMIS_WARN("HSMProvider stub importCertificate ignored (key='{}')", key_label);
    return false;
}

std::optional<std::string> HSMProvider::getCertificate(const std::string& key_label) {
    return std::string("-----BEGIN CERTIFICATE-----\nSTUB\n-----END CERTIFICATE-----\n");
}

bool HSMProvider::isReady() const { return initialized_; }

std::string HSMProvider::getTokenInfo() const {
    std::ostringstream oss; oss << "HSM STUB label=" << config_.key_label << " ready=" << (initialized_?"true":"false");
    return oss.str();
}

std::string HSMProvider::getLastError() const { return last_error_; }

// HSMPKIClient
HSMPKIClient::HSMPKIClient(HSMConfig config) : hsm_(std::make_unique<HSMProvider>(std::move(config))) { hsm_->initialize(); }
HSMPKIClient::~HSMPKIClient() { if (hsm_) hsm_->finalize(); }
HSMSignatureResult HSMPKIClient::sign(const std::vector<uint8_t>& data) { return hsm_->sign(data); }
bool HSMPKIClient::verify(const std::vector<uint8_t>& data, const std::string& signature_b64) { return hsm_->verify(data, signature_b64); }
std::optional<std::string> HSMPKIClient::getCertSerial() { return std::string("STUB-SERIAL"); }
bool HSMPKIClient::isReady() const { return hsm_->isReady(); }

} } // namespace themis::security// Windows-friendly stub implementation: The full PKCS#11 / HSM integration is
// not required for current hybrid search tests. Provide minimal methods.
#include "security/hsm_provider.h"
#include "utils/logger.h"
#include <vector>
#include <string>
#include <algorithm>

namespace themis { namespace security {

HSMSignatureResult HSMProvider::sign(const std::vector<uint8_t>& data, const std::string& keyId) {
    HSMSignatureResult r;
    r.success = true;
    // Dummy signature: reverse & hex encode
    std::string hex; hex.reserve(data.size()*2);
    static const char* digits = "0123456789abcdef";
    for (auto it = data.rbegin(); it != data.rend(); ++it) {
        hex.push_back(digits[(*it >> 4) & 0xF]);
        // Minimal Windows-friendly stub implementation of HSMProvider.
        // Provides no real cryptography; sufficient only to satisfy linkage for tests.

        \#include "security/hsm_provider.h"
        \#include "utils/logger.h"
        \#include <sstream>
        \#include <chrono>

        namespace themis { namespace security {

        // Simple hex helper
        static std::string to_hex(const std::vector<uint8_t>& data) {
            static const char* d = "0123456789abcdef";
            std::string out; out.reserve(data.size()*2);
            for (auto b : data) { out.push_back(d[(b>>4)&0xF]); out.push_back(d[b&0xF]); }
            return out;
        }

        // Very small base64 (subset) just for deterministic stub output.
        static std::string pseudo_b64(const std::vector<uint8_t>& data) {
            // We do NOT implement real base64; just hex prefixed for clarity.
            return std::string("hex:") + to_hex(data);
        }

        class HSMProvider::Impl { };// empty stub

        HSMProvider::HSMProvider(HSMConfig config)
            : impl_(std::make_unique<Impl>())
            , config_(std::move(config)) {}

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
            // Delegate to signHash using pseudo hash (just data itself for stub)
            return signHash(data, key_label);
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
            THEMIS_WARN("HSMProvider stub generateKeyPair ignored (label='{}')", label);
            return false;
        }

        bool HSMProvider::importCertificate(const std::string& key_label, const std::string& cert_pem) {
            THEMIS_WARN("HSMProvider stub importCertificate ignored (key='{}')", key_label);
            return false;
        }

        std::optional<std::string> HSMProvider::getCertificate(const std::string& key_label) {
            // Return placeholder PEM string
            return std::string("-----BEGIN CERTIFICATE-----\nSTUB\n-----END CERTIFICATE-----\n");
        }

        bool HSMProvider::isReady() const { return initialized_; }

        std::string HSMProvider::getTokenInfo() const {
            std::ostringstream oss; oss << "HSM STUB label=" << config_.key_label << " ready=" << (initialized_?"true":"false");
            return oss.str();
        }

        std::string HSMProvider::getLastError() const { return last_error_; }

        // HSMPKIClient
        HSMPKIClient::HSMPKIClient(HSMConfig config) : hsm_(std::make_unique<HSMProvider>(std::move(config))) {
            hsm_->initialize();
        }

        HSMPKIClient::~HSMPKIClient() { if (hsm_) hsm_->finalize(); }

        HSMSignatureResult HSMPKIClient::sign(const std::vector<uint8_t>& data) { return hsm_->sign(data); }
        bool HSMPKIClient::verify(const std::vector<uint8_t>& data, const std::string& signature_b64) { return hsm_->verify(data, signature_b64); }
        std::optional<std::string> HSMPKIClient::getCertSerial() { return std::string("STUB-SERIAL"); }
        bool HSMPKIClient::isReady() const { return hsm_->isReady(); }

        } } // namespace themis::security
        #undef LOAD_FUNC
