#include "security/encryption.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include "utils/hkdf_cache.h"
#include "utils/logger.h"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace themis {

static void write_debug_dump(const std::string& prefix, const EncryptedBlob& blob, const std::vector<uint8_t>& key, bool success) {
    try {
        namespace fs = std::filesystem;

        // Only write debug dumps when user explicitly sets THEMIS_DEBUG_ENC_DIR
        const char* env_dir = std::getenv("THEMIS_DEBUG_ENC_DIR");
        if (!env_dir || !*env_dir) return; // disabled by default

        fs::path dir = fs::path(env_dir);

        try {
            fs::create_directories(dir);
        } catch (const std::exception& e) {
            fprintf(stderr, "write_debug_dump: failed to create directory '%s': %s\n", dir.string().c_str(), e.what());
            return;
        }

        // timestamp
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        // short key fingerprint (first 8 bytes hex)
        std::ostringstream kf;
        kf << std::hex << std::setfill('0');
        for (size_t i = 0; i < key.size() && i < 8; ++i) kf << std::setw(2) << static_cast<int>(key[i]);

        nlohmann::json j = blob.toJson();
        j["key_fingerprint_prefix"] = kf.str();
        j["success"] = success;
        j["ts_ms"] = ms;

        fs::path file = dir / (prefix + "_" + std::to_string(ms) + ".json");
        std::ofstream ofs(file.string());
        if (ofs.is_open()) {
            ofs << j.dump(2) << std::endl;
            fprintf(stderr, "write_debug_dump: wrote '%s'\n", file.string().c_str());
        } else {
            fprintf(stderr, "write_debug_dump: failed to open '%s' for writing\n", file.string().c_str());
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "write_debug_dump: exception: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "write_debug_dump: unknown exception\n");
    }
}

// ===== Base64 Encoding/Decoding Helpers =====

static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string ret;
    int i = 0;
    int j = 0;
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

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while (i++ < 3)
            ret += '=';
    }

    return ret;
}

static bool is_base64(uint8_t c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

static std::vector<uint8_t> base64_decode(const std::string& encoded_string) {
    size_t in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::vector<uint8_t> ret;

    while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = static_cast<uint8_t>(base64_chars.find(char_array_4[i]));

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (j = 0; j < i; j++)
            char_array_4[j] = static_cast<uint8_t>(base64_chars.find(char_array_4[j]));

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

        for (j = 0; j < i - 1; j++)
            ret.push_back(char_array_3[j]);
    }

    return ret;
}

// ===== EncryptedBlob Implementation =====

std::string EncryptedBlob::toBase64() const {
    // Format: key_id:version:base64(iv):base64(ciphertext):base64(tag)
    std::ostringstream oss;
    oss << key_id << ":"
        << key_version << ":"
        << base64_encode(iv) << ":"
        << base64_encode(ciphertext) << ":"
        << base64_encode(tag);
    return oss.str();
}

EncryptedBlob EncryptedBlob::fromBase64(const std::string& b64) {
    EncryptedBlob blob;
    
    // Split by ':'
    std::vector<std::string> parts;
    std::stringstream ss(b64);
    std::string part;
    while (std::getline(ss, part, ':')) {
        parts.push_back(part);
    }
    
    if (parts.size() != 5) {
        throw std::runtime_error("Invalid EncryptedBlob format: expected 5 parts, got " + std::to_string(parts.size()));
    }
    
    blob.key_id = parts[0];
    blob.key_version = std::stoul(parts[1]);
    blob.iv = base64_decode(parts[2]);
    blob.ciphertext = base64_decode(parts[3]);
    blob.tag = base64_decode(parts[4]);
    
    return blob;
}

nlohmann::json EncryptedBlob::toJson() const {
    return nlohmann::json{
        {"key_id", key_id},
        {"key_version", key_version},
        {"iv", base64_encode(iv)},
        {"ciphertext", base64_encode(ciphertext)},
        {"tag", base64_encode(tag)}
    };
}

EncryptedBlob EncryptedBlob::fromJson(const nlohmann::json& j) {
    EncryptedBlob blob;

    if (!j.is_object()) {
        throw std::runtime_error("EncryptedBlob::fromJson: expected JSON object");
    }

    try {
        blob.key_id = j.at("key_id").get<std::string>();
        blob.key_version = j.at("key_version").get<uint32_t>();

        std::string iv_b64 = j.at("iv").get<std::string>();
        std::string ct_b64 = j.at("ciphertext").get<std::string>();
        std::string tag_b64 = j.at("tag").get<std::string>();

        blob.iv = base64_decode(iv_b64);
        blob.ciphertext = base64_decode(ct_b64);
        blob.tag = base64_decode(tag_b64);

    } catch (const nlohmann::json::exception& ex) {
        throw std::runtime_error(std::string("EncryptedBlob::fromJson: JSON error: ") + ex.what());
    }

    return blob;
}

std::vector<EncryptedBlob> FieldEncryption::encryptEntityBatch(const std::vector<std::pair<std::string,std::string>>& items,
                                                                const std::string& key_id) {
    std::vector<EncryptedBlob> out;
    out.resize(items.size());

    // Fetch base key once
    auto base_key = key_provider_->getKey(key_id);
    auto metadata = key_provider_->getKeyMetadata(key_id);

    // If environment variable THEMIS_ENC_PARALLEL is set, run encryptions in parallel for stress testing.
    const char* parallel_env = std::getenv("THEMIS_ENC_PARALLEL");
    bool do_parallel = (parallel_env && *parallel_env);

    if (do_parallel) {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, items.size()), [&](const tbb::blocked_range<size_t>& r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                const auto& ent = items[i];
                try {
                    out[i] = encryptWithKey(ent.second, key_id, metadata.version, base_key);
                    // best-effort debug write (opt-in via env)
                    try { write_debug_dump("encrypt", out[i], base_key, true); } catch(...) {}
                } catch (...) {
                    // ignore per-item errors here
                }
            }
        });
    } else {
        // Use sequential loop to avoid potential threading issues with OpenSSL in tests.
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& ent = items[i];
            try {
                out[i] = encryptWithKey(ent.second, key_id, metadata.version, base_key);
                // best-effort debug write (opt-in via env)
                try { write_debug_dump("encrypt", out[i], base_key, true); } catch(...) {}
            } catch (...) {
                // On error, leave default constructed blob; errors should be handled by caller
            }
        }
    }

    return out;
}

// ===== FieldEncryption Implementation =====

FieldEncryption::FieldEncryption(std::shared_ptr<KeyProvider> key_provider)
    : key_provider_(key_provider)
{
    if (!key_provider_) {
        throw std::invalid_argument("FieldEncryption: key_provider cannot be null");
    }
}

FieldEncryption::~FieldEncryption() = default;

EncryptedBlob FieldEncryption::encrypt(const std::string& plaintext, const std::string& key_id) {
    std::vector<uint8_t> plaintext_bytes(plaintext.begin(), plaintext.end());
    return encrypt(plaintext_bytes, key_id);
}

EncryptedBlob FieldEncryption::encrypt(const std::vector<uint8_t>& plaintext, const std::string& key_id) {
    // Get active key
    auto key = key_provider_->getKey(key_id);
    auto metadata = key_provider_->getKeyMetadata(key_id);
    
    return encryptInternal(plaintext, key_id, metadata.version, key);
}

std::string FieldEncryption::decryptToString(const EncryptedBlob& blob) {
    auto plaintext_bytes = decryptToBytes(blob);
    return std::string(plaintext_bytes.begin(), plaintext_bytes.end());
}

std::vector<uint8_t> FieldEncryption::decryptToBytes(const EncryptedBlob& blob) {
    // Get specific key version
    auto key = key_provider_->getKey(blob.key_id, blob.key_version);
    
    return decryptInternal(blob, key);
}

EncryptedBlob FieldEncryption::encryptWithKey(const std::string& plaintext,
                                               const std::string& key_id,
                                               uint32_t key_version,
                                               const std::vector<uint8_t>& key) {
    std::vector<uint8_t> plaintext_bytes(plaintext.begin(), plaintext.end());
    return encryptInternal(plaintext_bytes, key_id, key_version, key);
}

std::string FieldEncryption::decryptWithKey(const EncryptedBlob& blob,
                                             const std::vector<uint8_t>& key) {
    auto plaintext_bytes = decryptInternal(blob, key);
    return std::string(plaintext_bytes.begin(), plaintext_bytes.end());
}

std::vector<uint8_t> FieldEncryption::generateIV() const {
    std::vector<uint8_t> iv(12);  // 96 bits for GCM
    
    if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1) {
        throw EncryptionException("Failed to generate random IV");
    }
    
    return iv;
}

EncryptedBlob FieldEncryption::encryptInternal(const std::vector<uint8_t>& plaintext,
                                                const std::string& key_id,
                                                uint32_t key_version,
                                                const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        throw EncryptionException("Key must be 32 bytes (256 bits)");
    }
    
    EncryptedBlob blob;
    blob.key_id = key_id;
    blob.key_version = key_version;
    blob.iv = generateIV();
    
    // Create and initialize cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw EncryptionException("Failed to create cipher context");
    }
    
    try {
        // Initialize encryption with AES-256-GCM
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            throw EncryptionException("Failed to initialize cipher");
        }
        
        // Set IV length (12 bytes for GCM)
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) {
            throw EncryptionException("Failed to set IV length");
        }
        
        // Initialize key and IV
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), blob.iv.data()) != 1) {
            throw EncryptionException("Failed to set key and IV");
        }
        
        // Encrypt plaintext
        blob.ciphertext.resize(plaintext.size() + EVP_CIPHER_block_size(EVP_aes_256_gcm()));
        int len = 0;
        if (EVP_EncryptUpdate(ctx, blob.ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
            throw EncryptionException("Encryption failed");
        }
        int ciphertext_len = len;
        
        // Finalize encryption
        if (EVP_EncryptFinal_ex(ctx, blob.ciphertext.data() + len, &len) != 1) {
            throw EncryptionException("Failed to finalize encryption");
        }
        ciphertext_len += len;
        blob.ciphertext.resize(ciphertext_len);
        
        // Get authentication tag
        blob.tag.resize(16);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, blob.tag.data()) != 1) {
            throw EncryptionException("Failed to get authentication tag");
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
    } catch (...) {
        EVP_CIPHER_CTX_free(ctx);
        throw;
    }
    THEMIS_INFO("encryptInternal: key_id={}, key_ver={}, iv_len={}, ciphertext_len={}, tag_len={}",
                blob.key_id, blob.key_version, blob.iv.size(), blob.ciphertext.size(), blob.tag.size());
    // Write debug dump (best-effort)
    write_debug_dump("encrypt", blob, key, true);

    return blob;
}

std::vector<uint8_t> FieldEncryption::decryptInternal(const EncryptedBlob& blob,
                                                       const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        throw DecryptionException("Key must be 32 bytes (256 bits)");
    }
    
    if (blob.iv.size() != 12) {
        throw DecryptionException("IV must be 12 bytes");
    }
    
    if (blob.tag.size() != 16) {
        throw DecryptionException("Tag must be 16 bytes");
    }
    
    // Create and initialize cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw DecryptionException("Failed to create cipher context");
    }
    
    try {
        // Initialize decryption with AES-256-GCM
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            throw DecryptionException("Failed to initialize cipher");
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) {
            throw DecryptionException("Failed to set IV length");
        }
        
        // Initialize key and IV
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), blob.iv.data()) != 1) {
            throw DecryptionException("Failed to set key and IV");
        }
        
        // Decrypt ciphertext
        std::vector<uint8_t> plaintext(blob.ciphertext.size() + EVP_CIPHER_block_size(EVP_aes_256_gcm()));
        int len = 0;
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, blob.ciphertext.data(), static_cast<int>(blob.ciphertext.size())) != 1) {
            throw DecryptionException("Decryption failed");
        }
        int plaintext_len = len;
        
        // Set expected tag value
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(blob.tag.data())) != 1) {
            throw DecryptionException("Failed to set authentication tag");
        }
        
        // Finalize decryption (verifies tag)
        // Also print to stderr so test runner captures sizes even if logger is not fully initialized
        THEMIS_INFO("decryptInternal: key_id={}, key_ver={}, ciphertext_len={}, tag_len={}, iv_len={}, key_len={}",
                    blob.key_id, blob.key_version, blob.ciphertext.size(), blob.tag.size(), blob.iv.size(), key.size());
        fprintf(stderr, "decryptInternal: ciphertext_len=%zu, tag_len=%zu, iv_len=%zu, key_len=%zu\n",
            blob.ciphertext.size(), blob.tag.size(), blob.iv.size(), key.size());
        int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
        if (ret <= 0) {
            // write debug dump showing failure
            write_debug_dump("decrypt_failed", blob, key, false);
            fprintf(stderr, "decryptInternal: EVP_DecryptFinal_ex returned %d (auth failed)\n", ret);
            THEMIS_ERROR("decryptInternal: EVP_DecryptFinal_ex returned {} (auth failed)", ret);
            throw DecryptionException("Authentication failed - data may have been tampered with");
        }
        // write debug dump showing success
        write_debug_dump("decrypt_ok", blob, key, true);
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        
        EVP_CIPHER_CTX_free(ctx);
        
        return plaintext;
        
    } catch (...) {
        EVP_CIPHER_CTX_free(ctx);
        throw;
    }
}

}  // namespace themis
