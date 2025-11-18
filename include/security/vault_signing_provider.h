#pragma once

#include "security/signing_provider.h"
#include <string>
#include <vector>
#include <memory>

namespace themis {

class VaultSigningProvider : public SigningProvider {
public:
    struct Config {
        std::string vault_addr;    // e.g. "http://localhost:8200"
        std::string vault_token;   // Vault token for API calls
        std::string transit_mount; // transit mount path (default: "transit")
        int request_timeout_ms = 5000;
        bool verify_ssl = true;

        Config() : transit_mount("transit") {}
    };

    explicit VaultSigningProvider(const Config& cfg);
    ~VaultSigningProvider() override;

    SigningResult sign(const std::string& key_id, const std::vector<uint8_t>& data) override;

    // KeyProvider interface - this signing-only provider does not manage
    // encryption keys. Implementations of these are left as runtime
    // errors for the prototype to allow tests to instantiate the provider
    // when only signing is required.
    std::vector<uint8_t> getKey(const std::string& key_id) override {
        (void)key_id;
        throw KeyOperationException("VaultSigningProvider: getKey not implemented");
    }
    std::vector<uint8_t> getKey(const std::string& key_id, uint32_t version) override {
        (void)key_id; (void)version;
        throw KeyOperationException("VaultSigningProvider: getKey(version) not implemented");
    }
    uint32_t rotateKey(const std::string& key_id) override {
        (void)key_id;
        throw KeyOperationException("VaultSigningProvider: rotateKey not implemented");
    }
    std::vector<KeyMetadata> listKeys() override {
        throw KeyOperationException("VaultSigningProvider: listKeys not implemented");
    }
    KeyMetadata getKeyMetadata(const std::string& key_id, uint32_t version = 0) override {
        (void)key_id; (void)version;
        throw KeyOperationException("VaultSigningProvider: getKeyMetadata not implemented");
    }
    void deleteKey(const std::string& key_id, uint32_t version) override {
        (void)key_id; (void)version;
        throw KeyOperationException("VaultSigningProvider: deleteKey not implemented");
    }
    bool hasKey(const std::string& key_id, uint32_t version = 0) override {
        (void)key_id; (void)version;
        throw KeyOperationException("VaultSigningProvider: hasKey not implemented");
    }
    uint32_t createKeyFromBytes(
        const std::string& key_id,
        const std::vector<uint8_t>& key_bytes,
        const KeyMetadata& metadata = KeyMetadata()) override {
        (void)key_id; (void)key_bytes; (void)metadata;
        throw KeyOperationException("VaultSigningProvider: createKeyFromBytes not implemented");
    }
};

} // namespace themis
