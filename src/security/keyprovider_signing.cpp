#include "security/signing.h"
#include "security/signing_provider.h"
#include "security/cms_signing.h"
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <sstream>

namespace themis {

class KeyProviderSigningService : public SigningService {
public:
    explicit KeyProviderSigningService(std::shared_ptr<KeyProvider> kp) : kp_(std::move(kp)) {}

    SigningResult sign(const std::vector<uint8_t>& data, const std::string& key_id) override {
        // If KeyProvider also implements SigningProvider (HSM/KMS), use its sign API
        if (auto sp = dynamic_cast<SigningProvider*>(kp_.get())) {
            return sp->sign(key_id, data);
        }

        // Fallback: retrieve raw private key bytes and perform local CMS signing
        auto key_bytes = kp_->getKey(key_id);
        BIO* bio = BIO_new_mem_buf(key_bytes.data(), static_cast<int>(key_bytes.size()));
        if (!bio) throw std::runtime_error("BIO_new_mem_buf failed");

        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!pkey) throw std::runtime_error("Failed to parse private key from KeyProvider");

        // Optionally load cert (keep raw pointers and transfer ownership to CMSSigningService)
        X509* cert_ptr = nullptr;
        try {
            auto cert_bytes = kp_->getKey(key_id + ":cert");
            if (!cert_bytes.empty()) {
                BIO* cbio = BIO_new_mem_buf(cert_bytes.data(), static_cast<int>(cert_bytes.size()));
                X509* x = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr);
                BIO_free(cbio);
                if (x) cert_ptr = x; // transfer ownership to CMSSigningService below
            }
        } catch (...) {
            // missing cert is acceptable
        }

        // pkey is owned by the CMSSigningService after construction
        CMSSigningService cms(cert_ptr, pkey);
        return cms.sign(data, key_id);
    }

    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature, const std::string& key_id) override {
        // Try to obtain cert from KeyProvider
        try {
            auto cert_bytes = kp_->getKey(key_id + ":cert");
            if (!cert_bytes.empty()) {
                BIO* cbio = BIO_new_mem_buf(cert_bytes.data(), static_cast<int>(cert_bytes.size()));
                X509* x = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr);
                BIO_free(cbio);
                if (x) {
                    // transfer ownership of 'x' to CMSSigningService
                    CMSSigningService cms(x, nullptr);
                    return cms.verify(data, signature, key_id);
                }
            }
        } catch (...) {
            // fallthrough
        }
        // No cert available -> verification not possible here
        return false;
    }

private:
    std::shared_ptr<KeyProvider> kp_;
};

std::shared_ptr<SigningService> createKeyProviderSigningService(std::shared_ptr<KeyProvider> kp) {
    return std::make_shared<KeyProviderSigningService>(kp);
}

} // namespace themis
