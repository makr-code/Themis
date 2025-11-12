#include <gtest/gtest.h>
#include "security/cms_signing.h"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <memory>

using namespace themis;

static std::shared_ptr<EVP_PKEY> generate_rsa_key() {
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* pkey = nullptr;
    if (!pctx) return nullptr;
    if (EVP_PKEY_keygen_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); return nullptr; }
    EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048);
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) { EVP_PKEY_CTX_free(pctx); return nullptr; }
    EVP_PKEY_CTX_free(pctx);
    return std::shared_ptr<EVP_PKEY>(pkey, EVP_PKEY_free);
}

static std::shared_ptr<X509> make_self_signed_cert(EVP_PKEY* pkey) {
    X509* x = X509_new();
    if (!x) return nullptr;
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_get_notBefore(x), 0);
    X509_gmtime_adj(X509_get_notAfter(x), 60*60*24*365);
    X509_set_pubkey(x, pkey);

    X509_NAME* name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"Themis Test", -1, -1, 0);
    // issuer = subject (self-signed)
    X509_set_issuer_name(x, name);

    if (!X509_sign(x, pkey, EVP_sha256())) {
        X509_free(x);
        return nullptr;
    }
    return std::shared_ptr<X509>(x, X509_free);
}

TEST(CMSSigning, SignAndVerify) {
    auto pkey = generate_rsa_key();
    ASSERT_NE(pkey, nullptr);
    auto cert = make_self_signed_cert(pkey.get());
    ASSERT_NE(cert, nullptr);

    CMSSigningService svc(cert, pkey);

    std::string msg = "Test CMS signing payload";
    std::vector<uint8_t> data(msg.begin(), msg.end());

    auto res = svc.sign(data, "test-key");
    ASSERT_FALSE(res.signature.empty());

    bool ok = svc.verify(data, res.signature, "test-key");
    EXPECT_TRUE(ok);
}
