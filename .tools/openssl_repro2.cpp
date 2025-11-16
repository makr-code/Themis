#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <iostream>

int main(){
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!pctx) { std::cerr<<"EVP_PKEY_CTX_new_id failed\n"; return 2; }
    if (EVP_PKEY_keygen_init(pctx) != 1) { std::cerr<<"EVP_PKEY_keygen_init failed\n"; EVP_PKEY_CTX_free(pctx); return 2; }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) != 1) { std::cerr<<"EVP_PKEY_CTX_set_rsa_keygen_bits failed\n"; EVP_PKEY_CTX_free(pctx); return 2; }
    if (EVP_PKEY_keygen(pctx, &pkey) != 1) { std::cerr<<"EVP_PKEY_keygen failed\n"; EVP_PKEY_CTX_free(pctx); return 2; }
    EVP_PKEY_CTX_free(pctx);

    X509* x = X509_new();
    if (!x) { std::cerr<<"X509_new failed\n"; EVP_PKEY_free(pkey); return 2; }
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_get_notBefore(x), 0);
    X509_gmtime_adj(X509_get_notAfter(x), 60*60*24);
    if (X509_set_pubkey(x, pkey) != 1) { std::cerr<<"X509_set_pubkey failed\n"; X509_free(x); EVP_PKEY_free(pkey); return 2; }

    // Try EVP_DigestSign as a lower-level check
    {
        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        if (!mctx) { std::cerr<<"EVP_MD_CTX_new failed\n"; }
        else {
            if (EVP_DigestSignInit(mctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
                std::cerr<<"EVP_DigestSignInit failed\n";
                ERR_print_errors_fp(stderr);
            } else {
                unsigned char sig[512]; size_t siglen = sizeof(sig);
                const unsigned char msg[] = "abc";
                if (EVP_DigestSign(mctx, sig, &siglen, msg, sizeof(msg)-1) != 1) {
                    std::cerr<<"EVP_DigestSign failed\n";
                    ERR_print_errors_fp(stderr);
                } else {
                    std::cerr<<"EVP_DigestSign OK siglen="<<siglen<<"\n";
                }
            }
            EVP_MD_CTX_free(mctx);
        }
    }

    if (X509_sign(x, pkey, EVP_sha256()) != 1) {
        std::cerr<<"X509_sign returned != 1\n";
        ERR_print_errors_fp(stderr);
        unsigned long e = ERR_get_error();
        if (e) {
            char buf[256]; ERR_error_string_n(e, buf, sizeof(buf)); std::cerr<<"ERR: "<<buf<<"\n";
        }

        std::cerr<<"Attempting X509_sign_ctx fallback...\n";
        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        if (!mctx) {
            std::cerr<<"EVP_MD_CTX_new failed\n";
        } else {
            int r = EVP_DigestSignInit(mctx, nullptr, EVP_sha256(), nullptr, pkey);
            std::cerr<<"EVP_DigestSignInit returned "<<r<<"\n";
            if (r != 1) ERR_print_errors_fp(stderr);
            int s = X509_sign_ctx(x, mctx);
            std::cerr<<"X509_sign_ctx returned "<<s<<"\n";
            if (s != 1) ERR_print_errors_fp(stderr);
            EVP_MD_CTX_free(mctx);
        }

        const char* paths[] = {"/etc/ssl/openssl.cnf", "/usr/lib/ssl/openssl.cnf", "/usr/local/ssl/openssl.cnf"};
        for (auto &p : paths) {
            FILE* f = fopen(p, "r");
            if (f) { std::cerr<<"Found openssl config: "<<p<<"\n"; fclose(f); }
            else { std::cerr<<"No openssl config at: "<<p<<"\n"; }
        }

        X509_free(x); EVP_PKEY_free(pkey); return 3;
    }

    std::cout<<"X509_sign succeeded\n";
    X509_free(x); EVP_PKEY_free(pkey);
    EVP_cleanup(); ERR_free_strings();
    return 0;
}
