#pragma once

#include "security/signing.h"
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <memory>

namespace themis {

class CMSSigningService : public SigningService {
public:
    // Construct from existing X509 cert and private key (shared ownership)
    CMSSigningService(std::shared_ptr<X509> cert, std::shared_ptr<EVP_PKEY> pkey);
    ~CMSSigningService() override;

    SigningResult sign(const std::vector<uint8_t>& data, const std::string& key_id) override;
    bool verify(const std::vector<uint8_t>& data,
                const std::vector<uint8_t>& signature,
                const std::string& key_id) override;

private:
    std::shared_ptr<X509> cert_;
    std::shared_ptr<EVP_PKEY> pkey_;
};

} // namespace themis
