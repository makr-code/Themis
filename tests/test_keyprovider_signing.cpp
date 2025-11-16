#include <gtest/gtest.h>
#include "security/signing.h"
#include "security/signing_provider.h"
#include "security/mock_key_provider.h"
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>
#include "security/cms_signing.h"

using namespace themis;

// TestKeyProvider: returns PEM-encoded private key and cert for a given key_id
class TestKeyProvider : public MockKeyProvider, public SigningProvider {
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

    // Implement SigningProvider::sign by performing CMS signing locally using stored PEM
    SigningResult sign(const std::string& key_id, const std::vector<uint8_t>& data) override {
        // Expect key_id to be base id
        std::string base = key_id;
        if (!priv_.count(base)) throw KeyNotFoundException(key_id, 0);
        auto& priv = priv_[base];
        auto& cert = cert_[base];

        BIO* pbio = BIO_new_mem_buf(priv.data(), static_cast<int>(priv.size()));
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(pbio, nullptr, nullptr, nullptr);
        BIO_free(pbio);

        BIO* cbio = BIO_new_mem_buf(cert.data(), static_cast<int>(cert.size()));
        X509* x = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr);
        BIO_free(cbio);

        CMSSigningService cms(std::shared_ptr<X509>(x, X509_free), std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free));
        return cms.sign(data, key_id);
    }

private:
    std::map<std::string, std::vector<uint8_t>> priv_;
    std::map<std::string, std::vector<uint8_t>> cert_;
};

static std::pair<std::string,std::string> make_key_and_cert_pem() {
    // Generate RSA key and self-signed cert in memory using OpenSSL BIO
    std::cerr << "DEBUG: OPENSSL_VERSION_TEXT=" << OPENSSL_VERSION_TEXT << std::endl;
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!pctx) throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
    if (EVP_PKEY_keygen_init(pctx) != 1) { EVP_PKEY_CTX_free(pctx); throw std::runtime_error("EVP_PKEY_keygen_init failed"); }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) != 1) { EVP_PKEY_CTX_free(pctx); throw std::runtime_error("EVP_PKEY_CTX_set_rsa_keygen_bits failed"); }
    if (EVP_PKEY_keygen(pctx, &pkey) != 1) { EVP_PKEY_CTX_free(pctx); throw std::runtime_error("EVP_PKEY_keygen failed"); }
    EVP_PKEY_CTX_free(pctx);

    X509* x = X509_new();
    if (!x) { if (pkey) EVP_PKEY_free(pkey); throw std::runtime_error("X509_new failed"); }
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_get_notBefore(x), 0);
    X509_gmtime_adj(X509_get_notAfter(x), 60*60*24*365);
    X509_set_pubkey(x, pkey);
    X509_NAME* name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"Themis Test", -1, -1, 0);
    X509_set_issuer_name(x, name);
    // Diagnostic: print key type and bits and check X509_set_pubkey result
    int pkey_type = EVP_PKEY_id(pkey);
    int pkey_bits = EVP_PKEY_bits(pkey);
    std::cerr << "DEBUG: EVP_sha256 size=" << EVP_MD_size(EVP_sha256()) << std::endl;
    std::cerr << "DEBUG: EVP_PKEY type=" << pkey_type << " bits=" << pkey_bits << std::endl;
    int setpub = X509_set_pubkey(x, pkey);
    std::cerr << "DEBUG: X509_set_pubkey returned " << setpub << std::endl;
    if (setpub != 1) { X509_free(x); EVP_PKEY_free(pkey); throw std::runtime_error("X509_set_pubkey failed"); }

    if (X509_sign(x, pkey, EVP_sha256()) != 1) {
        // Print any queued OpenSSL errors to stderr (helps when ERR_get_error() is empty)
        ERR_print_errors_fp(stderr);
        std::cerr << "DEBUG: X509_sign failed; trying X509_sign_ctx fallback" << std::endl;

        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        if (!mctx) {
            ERR_print_errors_fp(stderr);
            X509_free(x);
            EVP_PKEY_free(pkey);
            throw std::runtime_error("EVP_MD_CTX_new failed");
        }

        int init_ok = EVP_DigestSignInit(mctx, nullptr, EVP_sha256(), nullptr, pkey);
        std::cerr << "DEBUG: EVP_DigestSignInit returned " << init_ok << std::endl;
        if (init_ok != 1) {
            ERR_print_errors_fp(stderr);
            EVP_MD_CTX_free(mctx);
            X509_free(x);
            EVP_PKEY_free(pkey);
            throw std::runtime_error("EVP_DigestSignInit failed");
        }

        int signctx_ok = X509_sign_ctx(x, mctx);
        std::cerr << "DEBUG: X509_sign_ctx returned " << signctx_ok << std::endl;
        if (signctx_ok <= 0) {
            ERR_print_errors_fp(stderr);
            EVP_MD_CTX_free(mctx);
            X509_free(x);
            EVP_PKEY_free(pkey);
            throw std::runtime_error("X509_sign_ctx failed");
        }
        EVP_MD_CTX_free(mctx);
    }

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
