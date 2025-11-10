#include "utils/audit_logger.h"

#include <filesystem>
#include <openssl/sha.h>

namespace themis {
namespace utils {

// Local base64 (kept minimal to avoid new deps here)
static std::string base64_encode_local(const std::vector<uint8_t>& data) {
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

AuditLogger::AuditLogger(std::shared_ptr<themis::FieldEncryption> enc,
                         std::shared_ptr<VCCPKIClient> pki,
                         AuditLoggerConfig cfg,
                         std::shared_ptr<LEKManager> lek_manager)
    : enc_(std::move(enc)), pki_(std::move(pki)), lek_manager_(std::move(lek_manager)), cfg_(std::move(cfg)) {}

std::vector<uint8_t> AuditLogger::sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    ::SHA256(data.data(), data.size(), out.data());
    return out;
}

void AuditLogger::appendJsonLine(const nlohmann::json& j) {
    std::scoped_lock lk(file_mu_);
    auto path = std::filesystem::path(cfg_.log_path);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(cfg_.log_path, std::ios::app | std::ios::binary);
    ofs << j.dump() << "\n";
}

void AuditLogger::logEvent(const nlohmann::json& event) {
    if (!cfg_.enabled) return;

    // Canonical-ish JSON (nlohmann::json preserves insertion order; for true canonicalization more work is needed)
    std::string plain = event.dump();

    nlohmann::json record;
    record["ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    record["category"] = "AUDIT";

    // Determine key_id for encryption (LEK or fallback)
    std::string actual_key_id = cfg_.key_id;
    std::string log_date;
    if (cfg_.use_lek && lek_manager_) {
        actual_key_id = lek_manager_->getCurrentLEK();
        log_date = LEKManager::getCurrentDateString();
        record["log_date"] = log_date;
        record["lek_id"] = actual_key_id;
    }

    if (cfg_.encrypt_then_sign && enc_) {
        // Encrypt plaintext JSON with actual key
        auto blob = enc_->encrypt(plain, actual_key_id);

        // Build bytes for hashing: iv || ciphertext || tag
        std::vector<uint8_t> to_hash;
        to_hash.reserve(blob.iv.size() + blob.ciphertext.size() + blob.tag.size());
        to_hash.insert(to_hash.end(), blob.iv.begin(), blob.iv.end());
        to_hash.insert(to_hash.end(), blob.ciphertext.begin(), blob.ciphertext.end());
        to_hash.insert(to_hash.end(), blob.tag.begin(), blob.tag.end());

        auto hash = sha256(to_hash);
        auto sig = pki_ ? pki_->signHash(hash) : SignatureResult{};

        auto jblob = themis::EncryptedBlob{blob}.toJson();
        // Persist encrypted payload and signature metadata
        record["payload"] = {
            {"type", "ciphertext"},
            {"key_id", blob.key_id},
            {"key_version", blob.key_version},
            {"iv_b64", jblob["iv"]},
            {"ciphertext_b64", jblob["ciphertext"]},
            {"tag_b64", jblob["tag"]}
        };
        record["signature"] = {
            {"ok", sig.ok},
            {"id", sig.signature_id},
            {"algorithm", sig.algorithm},
            {"sig_b64", sig.signature_b64},
            {"cert_serial", sig.cert_serial}
        };
    } else {
        // No encryption: sign plaintext bytes (if PKI available)
        std::vector<uint8_t> bytes(plain.begin(), plain.end());
        auto hash = sha256(bytes);
        auto sig = pki_ ? pki_->signHash(hash) : SignatureResult{};
        record["payload"] = {
            {"type", "plaintext"},
            {"data_b64", base64_encode_local(bytes)}
        };
        record["signature"] = {
            {"ok", sig.ok},
            {"id", sig.signature_id},
            {"algorithm", sig.algorithm},
            {"sig_b64", sig.signature_b64},
            {"cert_serial", sig.cert_serial}
        };
    }

    appendJsonLine(record);
}

} // namespace utils
} // namespace themis
