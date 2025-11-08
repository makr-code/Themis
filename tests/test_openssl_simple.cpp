#include <iostream>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bn.h>

int main() {
    std::cout << "Creating RSA key..." << std::endl;
    BIGNUM* bn = BN_new();
    if (!bn) { std::cerr << "BN_new failed" << std::endl; return 1; }
    if (BN_set_word(bn, RSA_F4) != 1) { std::cerr << "BN_set_word failed" << std::endl; return 1; }
    
    RSA* rsa = RSA_new();
    if (!rsa) { std::cerr << "RSA_new failed" << std::endl; return 1; }
    
    std::cout << "Generating 2048-bit key..." << std::endl;
    if (RSA_generate_key_ex(rsa, 2048, bn, nullptr) != 1) {
        std::cerr << "RSA_generate_key_ex failed" << std::endl;
        return 1;
    }
    
    std::cout << "Key generated successfully" << std::endl;
    
    const BIGNUM* n;
    const BIGNUM* e;
    RSA_get0_key(rsa, &n, &e, nullptr);
    
    int n_bytes = BN_num_bytes(n);
    int e_bytes = BN_num_bytes(e);
    
    std::cout << "n has " << n_bytes << " bytes" << std::endl;
    std::cout << "e has " << e_bytes << " bytes" << std::endl;
    
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) { std::cerr << "EVP_PKEY_new failed" << std::endl; return 1; }
    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
        std::cerr << "EVP_PKEY_assign_RSA failed" << std::endl;
        return 1;
    }
    
    std::cout << "Testing EVP_DigestSign..." << std::endl;
    EVP_MD_CTX* mctx = EVP_MD_CTX_new();
    if (!mctx) { std::cerr << "EVP_MD_CTX_new failed" << std::endl; return 1; }
    
    if (EVP_DigestSignInit(mctx, nullptr, EVP_sha256(), nullptr, pkey) <= 0) {
        std::cerr << "EVP_DigestSignInit failed" << std::endl;
        return 1;
    }
    
    std::string data = "test data";
    if (EVP_DigestSignUpdate(mctx, data.data(), data.size()) <= 0) {
        std::cerr << "EVP_DigestSignUpdate failed" << std::endl;
        return 1;
    }
    
    size_t siglen = 0;
    if (EVP_DigestSignFinal(mctx, nullptr, &siglen) <= 0) {
        std::cerr << "EVP_DigestSignFinal (query) failed" << std::endl;
        return 1;
    }
    
    std::cout << "Signature length: " << siglen << " bytes" << std::endl;
    
    std::vector<uint8_t> sig(siglen);
    size_t siglen2 = siglen;
    if (EVP_DigestSignFinal(mctx, sig.data(), &siglen2) <= 0) {
        std::cerr << "EVP_DigestSignFinal (actual) failed" << std::endl;
        return 1;
    }
    
    std::cout << "Actual signature length: " << siglen2 << " bytes" << std::endl;
    std::cout << "All tests passed!" << std::endl;
    
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);
    BN_free(bn);
    
    return 0;
}
