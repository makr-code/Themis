// Minimal stub implementation for TimestampAuthority.
// Deterministic, non-cryptographic timestamps (no OpenSSL / CURL).
#ifndef THEMIS_USE_OPENSSL_TSA

#include "security/timestamp_authority.h"
#include "utils/logger.h"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace themis { namespace security {

class TimestampAuthority::Impl { };

// Helper: hex encode
static std::string hex(const std::vector<uint8_t>& data) {
    static const char* d = "0123456789abcdef";
    std::string out; out.reserve(data.size()*2);
    for (auto b : data) { out.push_back(d[(b>>4)&0xF]); out.push_back(d[b&0xF]); }
    return out;
}

// Very weak deterministic hash (not cryptographic!)
static std::vector<uint8_t> pseudo_hash(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> h; h.reserve(data.size());
    for (size_t i=0;i<data.size();++i) h.push_back(static_cast<uint8_t>(data[i] ^ (i & 0xFF)));
    return h;
}

TimestampAuthority::TimestampAuthority(TSAConfig config)
    : impl_(std::make_unique<Impl>()), config_(std::move(config)) {}

TimestampAuthority::~TimestampAuthority() = default;
TimestampAuthority::TimestampAuthority(TimestampAuthority&&) noexcept = default;
TimestampAuthority& TimestampAuthority::operator=(TimestampAuthority&&) noexcept = default;

TimestampToken TimestampAuthority::getTimestamp(const std::vector<uint8_t>& data) {
    auto hash = computeHash(data);
    TimestampToken tok;
    tok.success = true;
    tok.hash_algorithm = config_.hash_algorithm;
    auto now = std::chrono::system_clock::now();
    tok.timestamp_unix_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm,&tt);
#else
    localtime_r(&tt,&tm);
#endif
    std::ostringstream oss; oss<<std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    tok.timestamp_utc = oss.str();
    tok.serial_number = "STUB-SERIAL";
    tok.policy_oid = config_.policy_oid;
    tok.nonce = generateNonce();
    tok.token_der = hash;
    tok.token_b64 = std::string("hex:")+hex(hash);
    tok.tsa_name = "STUB-TSA";
    tok.tsa_serial = "STUB-TSA-SERIAL";
    tok.verified = true;
    tok.cert_valid = true;
    return tok;
}

TimestampToken TimestampAuthority::getTimestampForHash(const std::vector<uint8_t>& hash) {
    // Reuse getTimestamp for simplicity (non-cryptographic anyway)
    return getTimestamp(hash);
}

bool TimestampAuthority::verifyTimestamp(const std::vector<uint8_t>& data, const TimestampToken& token) {
    auto h = computeHash(data);
    return token.success && token.token_b64 == std::string("hex:")+hex(h);
}

bool TimestampAuthority::verifyTimestampForHash(const std::vector<uint8_t>& hash, const TimestampToken& token) {
    return token.success && token.token_b64 == std::string("hex:")+hex(hash);
}

TimestampToken TimestampAuthority::parseToken(const std::vector<uint8_t>& token_data) {
    TimestampToken tok; tok.success = true; tok.token_der = token_data; tok.token_b64 = std::string("hex:")+hex(token_data); tok.serial_number="PARSE"; return tok;
}

TimestampToken TimestampAuthority::parseToken(const std::string& token_b64) {
    TimestampToken tok; tok.success = true; tok.token_b64 = token_b64; tok.serial_number="PARSE"; return tok;
}

std::optional<std::string> TimestampAuthority::getTSACertificate() { return std::string("-----BEGIN CERTIFICATE-----\nSTUB-TSA\n-----END CERTIFICATE-----\n"); }
bool TimestampAuthority::isAvailable() { return true; }
std::string TimestampAuthority::getLastError() const { return last_error_; }

// Private helpers (stubs)
std::vector<uint8_t> TimestampAuthority::createTSPRequest(const std::vector<uint8_t>&, const std::vector<uint8_t>&) { return {}; }
TimestampToken TimestampAuthority::parseTSPResponse(const std::vector<uint8_t>&) { TimestampToken t; t.success = true; return t; }
std::vector<uint8_t> TimestampAuthority::sendTSPRequest(const std::vector<uint8_t>&) { return {}; }
std::vector<uint8_t> TimestampAuthority::generateNonce(size_t bytes) { std::vector<uint8_t> n(bytes); for(size_t i=0;i<bytes;++i) n[i]=static_cast<uint8_t>(i); return n; }
std::vector<uint8_t> TimestampAuthority::computeHash(const std::vector<uint8_t>& data) { return pseudo_hash(data); }

} } // namespace themis::security

#endif // THEMIS_USE_OPENSSL_TSA
