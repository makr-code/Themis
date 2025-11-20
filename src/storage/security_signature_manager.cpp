#include "storage/security_signature_manager.h"
#include <openssl/sha.h>
#include <fstream>
#include <filesystem>
#include <vector>

namespace themis {
namespace storage {

namespace fs = std::filesystem;

SecuritySignatureManager::SecuritySignatureManager(std::shared_ptr<RocksDBWrapper> db)
    : db_(db) {
    if (!db_) {
        throw std::invalid_argument("RocksDBWrapper cannot be null");
    }
}

std::string SecuritySignatureManager::makeKey(const std::string& resource_id) const {
    return std::string(KEY_PREFIX) + resource_id;
}

bool SecuritySignatureManager::storeSignature(const SecuritySignature& sig) {
    try {
        std::string key = makeKey(sig.resource_id);
        std::string value = sig.serialize();
        
        return db_->put(key, value);
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<SecuritySignature> SecuritySignatureManager::getSignature(const std::string& resource_id) {
    try {
        std::string key = makeKey(resource_id);
        std::string value;
        
        if (!db_->get(key, value)) {
            return std::nullopt;
        }
        
        return SecuritySignature::deserialize(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool SecuritySignatureManager::deleteSignature(const std::string& resource_id) {
    try {
        std::string key = makeKey(resource_id);
        return db_->del(key);
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<SecuritySignature> SecuritySignatureManager::listAllSignatures() {
    std::vector<SecuritySignature> signatures;
    
    // TODO: Implement proper iteration when RocksDBWrapper supports it
    // For now, return empty list
    
    return signatures;
}

std::string SecuritySignatureManager::computeFileHash(const std::string& file_path) {
    try {
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            return "";
        }
        
        // Read entire file into memory
        std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        
        // Compute SHA256
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(buffer.data()), 
               buffer.size(), 
               digest);
        
        // Convert to hex string
        char hex_output[SHA256_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            snprintf(&hex_output[i * 2], 3, "%02x", static_cast<unsigned int>(digest[i]));
        }
        hex_output[SHA256_DIGEST_LENGTH * 2] = '\0';
        
        return std::string(hex_output);
    } catch (const std::exception&) {
        return "";
    }
}

std::string SecuritySignatureManager::normalizeResourceId(const std::string& path) {
    try {
        fs::path p(path);
        
        // Resolve relative paths and symlinks
        if (fs::exists(p)) {
            p = fs::weakly_canonical(p);
        }
        
        // Convert to generic format (forward slashes)
        std::string normalized = p.generic_string();
        
        // Remove leading "./" if present
        if (normalized.substr(0, 2) == "./") {
            normalized = normalized.substr(2);
        }
        
        return normalized;
    } catch (const std::exception&) {
        return path; // Return original if normalization fails
    }
}

bool SecuritySignatureManager::verifyFile(const std::string& file_path, 
                                          const std::string& resource_id) {
    try {
        // Compute current file hash
        std::string current_hash = computeFileHash(file_path);
        if (current_hash.empty()) {
            return false;
        }
        
        // Retrieve stored signature
        auto sig = getSignature(resource_id);
        if (!sig.has_value()) {
            return false;
        }
        
        // Verify algorithm
        if (sig->algorithm != "sha256") {
            return false;
        }
        
        // Compare hashes
        return (current_hash == sig->hash);
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace storage
} // namespace themis
