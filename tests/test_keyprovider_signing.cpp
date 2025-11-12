#include <gtest/gtest.h>
#include "security/signing.h"
#include "security/mock_key_provider.h"
#include <openssl/pem.h>
#include <openssl/x509.h>

using namespace themis;

// TestKeyProvider: returns PEM-encoded private key and cert for a given key_id
class TestKeyProvider : public MockKeyProvider {
public:
    TestKeyProvider() = default;

    void setKeyPem(const std::string& id, const std::string& priv_pem, const std::string& cert_pem) {
        priv_[id] = std::vector<uint8_t>(priv_pem.begin(), priv_pem.end());
        cert_[id] = std::vector<uint8_t>(cert_pem.begin(), cert_pem.end());
    }

    std::vector<uint8_t> getKey(const std::string& key_id) override {
        // If key ends with :cert return cert
        if (key_id.size() > 5 && key_id.substr(key_id.size()-5) == ":cert") {
            auto id = key_id.substr(0, key_id.size()-5);
            if (cert_.count(id)) return cert_[id];
            throw KeyNotFoundException(key_id, 0);
        }
        if (priv_.count(key_id)) return priv_[key_id];
        return MockKeyProvider::getKey(key_id); // fallback
    }

private:
    std::map<std::string, std::vector<uint8_t>> priv_;
    std::map<std::string, std::vector<uint8_t>> cert_;
};

static std::pair<std::string,std::string> make_key_and_cert_pem() {
    // Generate RSA key and self-signed cert in memory using OpenSSL BIO
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    ASSERT_NE(pctx, nullptr);
    ASSERT_EQ(EVP_PKEY_keygen_init(pctx), 1);
    ASSERT_EQ(EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048), 1);
    ASSERT_EQ(EVP_PKEY_keygen(pctx, &pkey), 1);
    EVP_PKEY_CTX_free(pctx);

    X509* x = X509_new();
    ASSERT_NE(x, nullptr);
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_get_notBefore(x), 0);
    X509_gmtime_adj(X509_get_notAfter(x), 60*60*24*365);
    X509_set_pubkey(x, pkey);
    X509_NAME* name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"Themis Test", -1, -1, 0);
    X509_set_issuer_name(x, name);
    ASSERT_EQ(X509_sign(x, pkey, EVP_sha256()), 1);

    BIO* pbio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(pbio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BUF_MEM* pptr; BIO_get_mem_ptr(pbio, &pptr);
    std::string priv_pem(pptr->data, pptr->length);
    BIO_free(pbio);

    BIO* cbio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(cbio, x);
    BUF_MEM* cptr; BIO_get_mem_ptr(cbio, &cptr);
    std::string cert_pem(cptr->data, cptr->length);
    BIO_free(cbio);

    X509_free(x);
    EVP_PKEY_free(pkey);
    return {priv_pem, cert_pem};
}

TEST(KeyProviderSigning, SignVerifyUsingKeyProvider) {
    auto [priv_pem, cert_pem] = make_key_and_cert_pem();

    auto kp = std::make_shared<TestKeyProvider>();
    // store a fallback AES key to satisfy MockKeyProvider base behavior
    kp->createKeyFromBytes("fallback_aes", std::vector<uint8_t>(32, 0x11));
    kp->setKeyPem("sign-key", priv_pem, cert_pem);

    auto svc = createKeyProviderSigningService(kp);

    std::string msg = "Data to sign via KeyProvider";
    std::vector<uint8_t> data(msg.begin(), msg.end());

    auto res = svc->sign(data, "sign-key");
    ASSERT_FALSE(res.signature.empty());

    bool ok = svc->verify(data, res.signature, "sign-key");
    EXPECT_TRUE(ok);
}
