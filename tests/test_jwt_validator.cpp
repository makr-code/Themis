#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/bn.h>
#include "auth/jwt_validator.h"

using namespace themis::auth;

// Helper: base64url encode
static std::string b64url(const std::vector<uint8_t>& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64; b64.reserve(((in.size()+2)/3)*4);
    size_t i=0; while(i+3<=in.size()) { uint32_t n=(in[i]<<16)|(in[i+1]<<8)|in[i+2]; b64.push_back(tbl[(n>>18)&63]); b64.push_back(tbl[(n>>12)&63]); b64.push_back(tbl[(n>>6)&63]); b64.push_back(tbl[n&63]); i+=3; }
    if(i+1==in.size()){ uint32_t n=(in[i]<<16); b64.push_back(tbl[(n>>18)&63]); b64.push_back(tbl[(n>>12)&63]); b64.push_back('='); b64.push_back('='); }
    else if(i+2==in.size()){ uint32_t n=(in[i]<<16)|(in[i+1]<<8); b64.push_back(tbl[(n>>18)&63]); b64.push_back(tbl[(n>>12)&63]); b64.push_back(tbl[(n>>6)&63]); b64.push_back('='); }
    // convert to url form
    for(char& c: b64){ if(c=='+') c='-'; else if(c=='/') c='_'; }
    while(!b64.empty() && b64.back()=='=') b64.pop_back();
    return b64;
}

static std::vector<uint8_t> sha256(const std::string& s){ std::vector<uint8_t> h(SHA256_DIGEST_LENGTH); SHA256((const uint8_t*)s.data(), s.size(), h.data()); return h; }

struct RSAFixture {
    RSA* rsa=nullptr; EVP_PKEY* pkey=nullptr; BIGNUM* bn=nullptr; 
    RSAFixture(){ 
        bn=BN_new(); 
        if (!bn) throw std::runtime_error("BN_new failed");
        if (BN_set_word(bn, RSA_F4) != 1) throw std::runtime_error("BN_set_word failed");
        rsa=RSA_new(); 
        if (!rsa) throw std::runtime_error("RSA_new failed");
        if (RSA_generate_key_ex(rsa, 2048, bn, nullptr) != 1) {
            throw std::runtime_error("RSA_generate_key_ex failed");
        }
        pkey=EVP_PKEY_new(); 
        if (!pkey) throw std::runtime_error("EVP_PKEY_new failed");
        if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
            throw std::runtime_error("EVP_PKEY_assign_RSA failed");
        }
    }
    ~RSAFixture(){ if(pkey) EVP_PKEY_free(pkey); if(bn) BN_free(bn); /* rsa freed by pkey */ }
};

static std::string sign_RS256(EVP_PKEY* pkey, const std::string& header_payload){ 
    EVP_MD_CTX* mctx = EVP_MD_CTX_new(); 
    if(!mctx) throw std::runtime_error("EVP_MD_CTX_new failed"); 
    size_t siglen=0; 
    if(EVP_DigestSignInit(mctx,nullptr,EVP_sha256(),nullptr,pkey)<=0) throw std::runtime_error("EVP_DigestSignInit failed"); 
    if(EVP_DigestSignUpdate(mctx, header_payload.data(), header_payload.size())<=0) throw std::runtime_error("EVP_DigestSignUpdate failed"); 
    if(EVP_DigestSignFinal(mctx,nullptr,&siglen)<=0) throw std::runtime_error("EVP_DigestSignFinal (query) failed"); 
    std::vector<uint8_t> sig(siglen); 
    size_t siglen2 = siglen;
    if(EVP_DigestSignFinal(mctx,sig.data(),&siglen2)<=0) throw std::runtime_error("EVP_DigestSignFinal failed"); 
    sig.resize(siglen2); 
    EVP_MD_CTX_free(mctx); 
    return b64url(sig); 
}

static nlohmann::json make_jwks(RSA* rsa){ const BIGNUM* n; const BIGNUM* e; RSA_get0_key(rsa,&n,&e,nullptr); std::vector<uint8_t> n_bytes(BN_num_bytes(n)); BN_bn2bin(n,n_bytes.data()); std::vector<uint8_t> e_bytes(BN_num_bytes(e)); BN_bn2bin(e,e_bytes.data()); nlohmann::json jwk={ {"kty","RSA"},{"kid","test-key-1"},{"alg","RS256"},{"use","sig"},{"n", b64url(n_bytes)},{"e", b64url(e_bytes)} }; return nlohmann::json{{"keys", nlohmann::json::array({jwk})}}; }

static std::string build_token(const std::string& kid, const nlohmann::json& payload){ 
    nlohmann::json header={{"alg","RS256"},{"typ","JWT"},{"kid",kid}}; 
    std::string header_str = header.dump();
    std::string payload_str = payload.dump();
    std::vector<uint8_t> h_vec(header_str.begin(), header_str.end());
    std::vector<uint8_t> p_vec(payload_str.begin(), payload_str.end());
    std::string h=b64url(h_vec); 
    std::string p=b64url(p_vec); 
    return h+"."+p; 
}

TEST(JWTValidatorTest, ValidToken) {
    RSAFixture fix; auto jwks = make_jwks(fix.rsa);
    JWTValidator validator(JWTValidatorConfig{"", "issuerX", "audX", std::chrono::seconds(600), std::chrono::seconds(60)});
    validator.setJWKSForTesting(jwks);
    auto now = std::chrono::system_clock::now(); auto exp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()+300;
    nlohmann::json payload={{"sub","u1"},{"email","u1@x"},{"iss","issuerX"},{"aud","audX"},{"exp",exp}};
    std::string unsigned_token = build_token("test-key-1", payload);
    std::string sig = sign_RS256(fix.pkey, unsigned_token);
    std::string token = unsigned_token+"."+sig;
    auto claims = validator.parseAndValidate(token);
    EXPECT_EQ(claims.sub, "u1");
    EXPECT_EQ(claims.issuer, "issuerX");
}

TEST(JWTValidatorTest, ExpiredToken) {
    RSAFixture fix; auto jwks = make_jwks(fix.rsa); JWTValidator validator(JWTValidatorConfig{"", "issuerX", "audX", std::chrono::seconds(600), std::chrono::seconds(0)}); validator.setJWKSForTesting(jwks); auto now = std::chrono::system_clock::now(); auto exp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()-10; nlohmann::json payload={{"sub","u1"},{"email","e"},{"iss","issuerX"},{"aud","audX"},{"exp",exp}}; std::string up=build_token("test-key-1",payload); std::string token=up+"."+sign_RS256(fix.pkey, up); EXPECT_THROW(validator.parseAndValidate(token), std::runtime_error); }

TEST(JWTValidatorTest, IssuerMismatch) {
    RSAFixture fix; auto jwks=make_jwks(fix.rsa); JWTValidator validator(JWTValidatorConfig{"", "issuerX", "audX", std::chrono::seconds(600), std::chrono::seconds(60)}); validator.setJWKSForTesting(jwks); auto now=std::chrono::system_clock::now(); auto exp=std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()+100; nlohmann::json payload={{"sub","u1"},{"email","e"},{"iss","wrong"},{"aud","audX"},{"exp",exp}}; std::string up=build_token("test-key-1",payload); std::string token=up+"."+sign_RS256(fix.pkey, up); EXPECT_THROW(validator.parseAndValidate(token), std::runtime_error); }

TEST(JWTValidatorTest, AudienceMismatch) {
    RSAFixture fix; auto jwks=make_jwks(fix.rsa); JWTValidator validator(JWTValidatorConfig{"", "issuerX", "audExpected", std::chrono::seconds(600), std::chrono::seconds(60)}); validator.setJWKSForTesting(jwks); auto now=std::chrono::system_clock::now(); auto exp=std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()+100; nlohmann::json payload={{"sub","u1"},{"email","e"},{"iss","issuerX"},{"aud","otherAud"},{"exp",exp}}; std::string up=build_token("test-key-1",payload); std::string token=up+"."+sign_RS256(fix.pkey, up); EXPECT_THROW(validator.parseAndValidate(token), std::runtime_error); }

TEST(JWTValidatorTest, NotYetValidNbf) {
    RSAFixture fix; auto jwks=make_jwks(fix.rsa); JWTValidator validator(JWTValidatorConfig{"", "issuerX", "audX", std::chrono::seconds(600), std::chrono::seconds(0)}); validator.setJWKSForTesting(jwks); auto now=std::chrono::system_clock::now(); auto exp=std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()+300; auto nbf=exp; nlohmann::json payload={{"sub","u1"},{"email","e"},{"iss","issuerX"},{"aud","audX"},{"exp",exp},{"nbf",nbf}}; std::string up=build_token("test-key-1",payload); std::string token=up+"."+sign_RS256(fix.pkey, up); EXPECT_THROW(validator.parseAndValidate(token), std::runtime_error); }

TEST(JWTValidatorTest, TamperedPayloadSignatureFails) {
    RSAFixture fix; auto jwks=make_jwks(fix.rsa); JWTValidator validator(JWTValidatorConfig{"", "issuerX", "audX", std::chrono::seconds(600), std::chrono::seconds(60)}); validator.setJWKSForTesting(jwks); auto now=std::chrono::system_clock::now(); auto exp=std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()+100; nlohmann::json payload={{"sub","u1"},{"email","e"},{"iss","issuerX"},{"aud","audX"},{"exp",exp}}; std::string up=build_token("test-key-1",payload); std::string sig=sign_RS256(fix.pkey, up); // tamper payload
    std::string tampered = up; tampered[up.find('.')+5] = (tampered[up.find('.')+5] == 'A' ? 'B' : 'A'); std::string token = tampered + "." + sig; 
    // Tampering should cause either parse error or signature validation failure
    EXPECT_THROW(validator.parseAndValidate(token), std::exception); 
}
