#include "security/signing.h"
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <vector>
#include <memory>
#include <mutex>

namespace themis {

class MockSigningService : public SigningService {
public:
    MockSigningService() {
        // Generate an ephemeral RSA key for testing
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY* pkey = nullptr;
        if (EVP_PKEY_keygen_init(ctx) <= 0) return;
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            return;
        }
        EVP_PKEY_CTX_free(ctx);
        pkey_.reset(pkey, EVP_PKEY_free);
    }

    SigningResult sign(const std::vector<uint8_t>& data, const std::string& key_id) override {
        SigningResult res;
        res.algorithm = "RSASSA-PSS/SHA256";
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) throw std::runtime_error("EVP_MD_CTX_new failed");

        if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey_.get()) != 1) {
            EVP_MD_CTX_free(mdctx);
            throw std::runtime_error("DigestSignInit failed");
        }

        size_t siglen = 0;
        if (EVP_DigestSign(mdctx, nullptr, &siglen, data.data(), data.size()) != 1) {
            EVP_MD_CTX_free(mdctx);
            throw std::runtime_error("DigestSign (len) failed");
        }
        res.signature.resize(siglen);
        if (EVP_DigestSign(mdctx, res.signature.data(), &siglen, data.data(), data.size()) != 1) {
            EVP_MD_CTX_free(mdctx);
            throw std::runtime_error("DigestSign failed");
        }
        res.signature.resize(siglen);
        EVP_MD_CTX_free(mdctx);
        return res;
    }

    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature, const std::string& key_id) override {
        EVP_PKEY* pub = pkey_.get();
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx) return false;
        if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pub) != 1) {
            EVP_MD_CTX_free(mdctx);
            return false;
        }
        int ok = EVP_DigestVerify(mdctx, signature.data(), signature.size(), data.data(), data.size());
        EVP_MD_CTX_free(mdctx);
        return ok == 1;
    }

private:
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey_{nullptr, EVP_PKEY_free};
};

std::shared_ptr<SigningService> createMockSigningService() {
    return std::make_shared<MockSigningService>();
}

} // namespace themis
