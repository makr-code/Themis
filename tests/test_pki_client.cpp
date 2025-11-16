#include <gtest/gtest.h>
#include "utils/pki_client.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#ifdef _WIN32
#include <openssl/applink.c>
#endif

#include <filesystem>
#include <fstream>
#include <vector>
#include <random>

using namespace themis::utils;

namespace {

std::vector<uint8_t> random_bytes(size_t n) {
    std::vector<uint8_t> v(n);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(dist(rng));
    return v;
}

bool generate_rsa_key_and_self_signed_cert(const std::string& key_path,
                                           const std::string& cert_path,
                                           int bits = 2048,
                                           int days_valid = 365) {
    bool ok = false;
    EVP_PKEY* pkey = nullptr;
    X509* x509 = nullptr;
    X509_NAME* name = nullptr;

    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new();
    if (!rsa || !e) goto cleanup;
    if (BN_set_word(e, RSA_F4) != 1) goto cleanup;
    if (RSA_generate_key_ex(rsa, bits, e, nullptr) != 1) goto cleanup;

    pkey = EVP_PKEY_new();
    if (!pkey) goto cleanup;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) goto cleanup;
    // rsa is now owned by pkey
    rsa = nullptr;
#else
    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) goto cleanup;
    rsa = nullptr;
#endif

    x509 = X509_new();
    if (!x509) goto cleanup;

    // Version 3 certificate
    X509_set_version(x509, 2);

    // Serial number
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

    // Validity
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), days_valid * 24 * 3600);

    // Subject/Issuer (self-signed)
    name = X509_get_subject_name(x509);
    if (!name) goto cleanup;
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"DE", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"ThemisDB", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"themis-test", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Public key
    if (X509_set_pubkey(x509, pkey) != 1) goto cleanup;

    // Sign certificate
    if (X509_sign(x509, pkey, EVP_sha256()) == 0) goto cleanup;

    // Write key
    {
        FILE* f = fopen(key_path.c_str(), "wb");
        if (!f) goto cleanup;
        if (PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) { fclose(f); goto cleanup; }
        fclose(f);
    }

    // Write cert
    {
        FILE* f = fopen(cert_path.c_str(), "wb");
        if (!f) goto cleanup;
        if (PEM_write_X509(f, x509) != 1) { fclose(f); goto cleanup; }
        fclose(f);
    }

    ok = true;

cleanup:
    if (x509) X509_free(x509);
    if (pkey) EVP_PKEY_free(pkey);
    if (rsa) RSA_free(rsa);
    if (e) BN_free(e);
    return ok;
}

} // namespace

TEST(PKIClientTest, SignVerify_StubMode_Base64Echo) {
    PKIConfig cfg; // no key/cert -> stub mode
    cfg.signature_algorithm = "RSA-SHA256";
    VCCPKIClient client(cfg);

    auto hash = random_bytes(32);
    auto sig = client.signHash(hash);
    ASSERT_TRUE(sig.ok);
    // In stub mode signature is base64(hash); verify should succeed
    EXPECT_TRUE(client.verifyHash(hash, sig));
}

TEST(PKIClientTest, SignVerify_RSA_SHA256_Succeeds) {
    // Prepare temp files
    std::filesystem::create_directories("data/test_pki");
    const std::string key_path = "data/test_pki/test_key.pem";
    const std::string cert_path = "data/test_pki/test_cert.pem";

    ASSERT_TRUE(generate_rsa_key_and_self_signed_cert(key_path, cert_path));

    PKIConfig cfg;
    cfg.key_path = key_path;
    cfg.cert_path = cert_path;
    cfg.signature_algorithm = "RSA-SHA256";

    VCCPKIClient client(cfg);

    auto hash = random_bytes(32); // SHA-256 length
    auto sig = client.signHash(hash);

    ASSERT_TRUE(sig.ok);
    EXPECT_EQ(sig.algorithm, "RSA-SHA256");
    EXPECT_FALSE(sig.signature_b64.empty());
    EXPECT_FALSE(sig.cert_serial.empty());

    EXPECT_TRUE(client.verifyHash(hash, sig));

    // Negative: modify hash
    hash[0] ^= 0xFF;
    EXPECT_FALSE(client.verifyHash(hash, sig));
}

TEST(PKIClientTest, SignVerify_AlgoMismatch_FallsBackStub) {
    PKIConfig cfg; // no key/cert
    cfg.signature_algorithm = "RSA-SHA512"; // expects 64-byte hash
    VCCPKIClient client(cfg);

    auto hash = random_bytes(32); // wrong length for RSA-SHA512 -> stub
    auto sig = client.signHash(hash);
    ASSERT_TRUE(sig.ok);
    // Should verify via stub comparison
    EXPECT_TRUE(client.verifyHash(hash, sig));
}
