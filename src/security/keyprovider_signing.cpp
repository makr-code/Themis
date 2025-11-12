#include "security/signing.h"
#include "security/cms_signing.h"
#include <openssl/pem.h>
#include <openssl/err.h>
#include <sstream>

namespace themis {

class KeyProviderSigningService : public SigningService {
public:
    explicit KeyProviderSigningService(std::shared_ptr<KeyProvider> kp) : kp_(std::move(kp)) {}

    SigningResult sign(const std::vector<uint8_t>& data, const std::string& key_id) override {
        // Obtain private key bytes from KeyProvider
        auto key_bytes = kp_->getKey(key_id);
        BIO* bio = BIO_new_mem_buf(key_bytes.data(), static_cast<int>(key_bytes.size()));
        if (!bio) throw std::runtime_error("BIO_new_mem_buf failed");

        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!pkey) throw std::runtime_error("Failed to parse private key from KeyProvider");

        // Optionally load cert
        std::shared_ptr<X509> cert(nullptr, X509_free);
        try {
            auto cert_bytes = kp_->getKey(key_id + ":cert");
            if (!cert_bytes.empty()) {
                BIO* cbio = BIO_new_mem_buf(cert_bytes.data(), static_cast<int>(cert_bytes.size()));
                X509* x = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr);
                BIO_free(cbio);
                if (x) cert.reset(x);
            }
        } catch (...) {
            // missing cert is acceptable
        }

        CMSSigningService cms(cert, std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free));
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
                    CMSSigningService cms(std::shared_ptr<X509>(x, X509_free), nullptr);
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
