#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include "auth/jwt_validator.h"

using namespace themis::auth;

static std::string b64url(const std::vector<uint8_t>& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64; b64.reserve(((in.size()+2)/3)*4);
    size_t i=0; while(i+3<=in.size()) { uint32_t n=(in[i]<<16)|(in[i+1]<<8)|in[i+2]; b64.push_back(tbl[(n>>18)&63]); b64.push_back(tbl[(n>>12)&63]); b64.push_back(tbl[(n>>6)&63]); b64.push_back(tbl[n&63]); i+=3; }
    if(i+1==in.size()){ uint32_t n=(in[i]<<16); b64.push_back(tbl[(n>>18)&63]); b64.push_back(tbl[(n>>12)&63]); b64.push_back('='); b64.push_back('='); }
    else if(i+2==in.size()){ uint32_t n=(in[i]<<16)|(in[i+1]<<8); b64.push_back(tbl[(n>>18)&63]); b64.push_back(tbl[(n>>12)&63]); b64.push_back(tbl[(n>>6)&63]); b64.push_back('='); }
    for(char& c: b64){ if(c=='+') c='-'; else if(c=='/') c='_'; }
    while(!b64.empty() && b64.back()=='=') b64.pop_back();
    return b64;
}

struct RSAFixture {
    RSA* rsa=nullptr; EVP_PKEY* pkey=nullptr; BIGNUM* bn=nullptr;
    RSAFixture(){
        bn=BN_new(); if(!bn) throw std::runtime_error("BN_new failed");
        if(BN_set_word(bn, RSA_F4) != 1) throw std::runtime_error("BN_set_word failed");
        rsa = RSA_new(); if(!rsa) throw std::runtime_error("RSA_new failed");
        if(RSA_generate_key_ex(rsa, 2048, bn, nullptr) != 1) throw std::runtime_error("RSA_generate_key_ex failed");
        pkey = EVP_PKEY_new(); if(!pkey) throw std::runtime_error("EVP_PKEY_new failed");
        if(EVP_PKEY_assign_RSA(pkey, rsa) != 1) throw std::runtime_error("EVP_PKEY_assign_RSA failed");
    }
    ~RSAFixture(){ if(pkey) EVP_PKEY_free(pkey); if(bn) BN_free(bn); }
};

static std::string sign_RS256(EVP_PKEY* pkey, const std::string& header_payload){
    EVP_MD_CTX* mctx = EVP_MD_CTX_new(); if(!mctx) throw std::runtime_error("EVP_MD_CTX_new failed");
    if(EVP_DigestSignInit(mctx,nullptr,EVP_sha256(),nullptr,pkey)<=0) throw std::runtime_error("DigestSignInit failed");
    if(EVP_DigestSignUpdate(mctx, header_payload.data(), header_payload.size())<=0) throw std::runtime_error("DigestSignUpdate failed");
    size_t siglen=0; if(EVP_DigestSignFinal(mctx,nullptr,&siglen)<=0) throw std::runtime_error("DigestSignFinal query failed");
    std::vector<uint8_t> sig(siglen); size_t siglen2=siglen; if(EVP_DigestSignFinal(mctx,sig.data(),&siglen2)<=0) throw std::runtime_error("DigestSignFinal failed");
    sig.resize(siglen2); EVP_MD_CTX_free(mctx); return b64url(sig);
}

static nlohmann::json make_jwks(RSA* rsa, const std::string& kid="test-int-1"){
    const BIGNUM* n; const BIGNUM* e; RSA_get0_key(rsa,&n,&e,nullptr);
    std::vector<uint8_t> n_bytes(BN_num_bytes(n)); BN_bn2bin(n,n_bytes.data());
    std::vector<uint8_t> e_bytes(BN_num_bytes(e)); BN_bn2bin(e,e_bytes.data());
    nlohmann::json jwk = {{"kty","RSA"},{"kid",kid},{"alg","RS256"},{"use","sig"},{"n", b64url(n_bytes)},{"e", b64url(e_bytes)}};
    return nlohmann::json{{"keys", nlohmann::json::array({jwk})}};
}

TEST(JWTUnit, JWKSRotation_SetJWKSForTesting) {
    RSAFixture fix;

    // JWKS that does not contain the needed kid
    nlohmann::json jwk1 = {{"kty","RSA"},{"kid","other"},{"alg","RS256"},{"use","sig"},{"n","AAA"},{"e","AQAB"}};
    nlohmann::json jwks1 = nlohmann::json{{"keys", nlohmann::json::array({jwk1})}};

    // JWKS that contains the correct kid
    auto jwks2 = make_jwks(fix.rsa, "rot-kid-1");

    // Prepare validator config
    JWTValidatorConfig cfg; cfg.expected_issuer = "rot-issuer"; cfg.expected_audience = "rot-aud"; cfg.jwks_url = "http://unused/";
    JWTValidator validator(cfg);

    // Build token signed with rot-kid-1
    nlohmann::json header = {{"alg","RS256"},{"kid","rot-kid-1"}};
    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    nlohmann::json payload = {{"sub","rot-user"},{"iss","rot-issuer"},{"aud","rot-aud"},{"exp", now + 300}};
    std::string header_s = header.dump();
    std::string payload_s = payload.dump();
    std::string header_b64 = b64url(std::vector<uint8_t>(header_s.begin(), header_s.end()));
    std::string payload_b64 = b64url(std::vector<uint8_t>(payload_s.begin(), payload_s.end()));
    std::string unsigned_token = header_b64 + "." + payload_b64;
    std::string sig = sign_RS256(fix.pkey, unsigned_token);
    std::string token = unsigned_token + "." + sig;

    // First: set JWKS to jwks1 (missing kid) -> expect failure
    validator.setJWKSForTesting(jwks1);
    EXPECT_THROW(validator.parseAndValidate(token), std::runtime_error);

    // Second: set JWKS to jwks2 (contains kid) -> expect success
    validator.setJWKSForTesting(jwks2);
    auto claims = validator.parseAndValidate(token);
    EXPECT_EQ(claims.sub, "rot-user");
}
