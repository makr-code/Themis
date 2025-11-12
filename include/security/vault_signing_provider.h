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
};

} // namespace themis
