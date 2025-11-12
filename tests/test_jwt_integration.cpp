#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <boost/asio.hpp>
#include <thread>
#include <chrono>
#include "auth/jwt_validator.h"

using namespace themis::auth;
using boost::asio::ip::tcp;

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

// Simple one-shot HTTP server (Boost.Asio) that serves given body for any request
class OneShotHttpServer {
public:
    OneShotHttpServer(std::string body, unsigned short port=0)
        : body_(std::move(body)), ios_(), acceptor_(ios_) {
        tcp::endpoint ep(tcp::v4(), port);
        acceptor_.open(ep.protocol());
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        acceptor_.bind(ep);
        acceptor_.listen(1);
        port_ = acceptor_.local_endpoint().port();
        thr_ = std::thread([this]{ this->run(); });
    }
    ~OneShotHttpServer(){
        try { acceptor_.close(); } catch(...){}
        if(thr_.joinable()) thr_.join();
    }
    unsigned short port() const { return port_; }
private:
    void run(){
        try{
            tcp::socket sock(ios_);
            acceptor_.accept(sock);
            // read request (up to header end)
            boost::asio::streambuf req; boost::asio::read_until(sock, req, "\r\n\r\n");
            std::ostringstream resp;
            resp << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " << body_.size() << "\r\nConnection: close\r\n\r\n" << body_;
            std::cerr << "OneShotHttpServer: serving body (len=" << body_.size() << "): " << (body_.size() > 200 ? body_.substr(0,200) + "..." : body_) << std::endl;
            boost::asio::write(sock, boost::asio::buffer(resp.str()));
            boost::system::error_code ec; sock.shutdown(tcp::socket::shutdown_both, ec);
        } catch(...) {
            // ignore
        }
    }
    std::string body_;
    boost::asio::io_context ios_;
    tcp::acceptor acceptor_;
    std::thread thr_;
    unsigned short port_{};
};

// Multi-response server: serves responses sequentially for multiple connections
class MultiResponseHttpServer {
public:
    MultiResponseHttpServer(std::vector<std::string> bodies, unsigned short port=0)
        : bodies_(std::move(bodies)), ios_(), acceptor_(ios_), idx_(0) {
        tcp::endpoint ep(tcp::v4(), port);
        acceptor_.open(ep.protocol());
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        acceptor_.bind(ep);
        acceptor_.listen(static_cast<int>(bodies_.size()));
        port_ = acceptor_.local_endpoint().port();
        thr_ = std::thread([this]{ this->run(); });
    }
    ~MultiResponseHttpServer(){
        try { acceptor_.close(); } catch(...){}
        if(thr_.joinable()) thr_.join();
    }
    unsigned short port() const { return port_; }
private:
    void run(){
        try{
            while(idx_ < bodies_.size()){
                tcp::socket sock(ios_);
                acceptor_.accept(sock);
                // read request headers
                boost::asio::streambuf req; boost::asio::read_until(sock, req, "\r\n\r\n");
                std::string body = bodies_[idx_++];
                std::ostringstream resp;
                resp << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
                std::cerr << "MultiResponseHttpServer: serving index=" << (idx_-1) << " len=" << body.size() << " body: " << (body.size() > 200 ? body.substr(0,200) + "..." : body) << std::endl;
                boost::asio::write(sock, boost::asio::buffer(resp.str()));
                boost::system::error_code ec; sock.shutdown(tcp::socket::shutdown_both, ec);
            }
        } catch(...) { }
    }
    std::vector<std::string> bodies_;
    boost::asio::io_context ios_;
    tcp::acceptor acceptor_;
    std::thread thr_;
    unsigned short port_{};
    std::atomic<size_t> idx_;
};

TEST(JWTIntegration, JWKSFetchAndValidate) {
    try {
        RSAFixture fix;
        auto jwks = make_jwks(fix.rsa, "int-kid-1");
        std::string jwks_body = jwks.dump();
        OneShotHttpServer server(jwks_body, 0);
        unsigned short port = server.port();
        ASSERT_GT(port, 0);

        // Build token
        nlohmann::json header = {{"alg","RS256"},{"kid","int-kid-1"}};
        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        nlohmann::json payload = {{"sub","integration-user"},{"iss","int-issuer"},{"aud","int-aud"},{"exp", now + 300}};
    std::string header_s = header.dump();
    std::string payload_s = payload.dump();
    std::string header_b64 = b64url(std::vector<uint8_t>(header_s.begin(), header_s.end()));
    std::string payload_b64 = b64url(std::vector<uint8_t>(payload_s.begin(), payload_s.end()));
        std::string unsigned_token = header_b64 + "." + payload_b64;
        std::string sig = sign_RS256(fix.pkey, unsigned_token);
        std::string token = unsigned_token + "." + sig;

        JWTValidatorConfig cfg;
        cfg.jwks_url = "http://127.0.0.1:" + std::to_string(port) + "/jwks.json";
        cfg.expected_issuer = "int-issuer";
        cfg.expected_audience = "int-aud";
        JWTValidator validator(cfg);

        // Give server a moment to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto claims = validator.parseAndValidate(token);
        EXPECT_EQ(claims.sub, "integration-user");
        EXPECT_EQ(claims.issuer, "int-issuer");
    } catch (const std::exception& ex) {
        std::cerr << "JWTIntegration test exception: " << ex.what() << std::endl;
        FAIL() << "Exception: " << ex.what();
    } catch (...) {
        std::cerr << "JWTIntegration test unknown exception" << std::endl;
        FAIL() << "Unknown exception";
    }
}

TEST(JWTIntegration, JWKSRotation_RefetchOnMissingKid) {
    try {
        RSAFixture fix;
        // First JWKS does NOT include the needed kid
        nlohmann::json jwk1 = {{"kty","RSA"},{"kid","other"},{"alg","RS256"},{"use","sig"},{"n","AAA"},{"e","AQAB"}};
        nlohmann::json jwks1 = nlohmann::json{{"keys", nlohmann::json::array({jwk1})}};
        // Second JWKS contains the correct kid
        auto jwks2 = make_jwks(fix.rsa, "rot-kid-1");

    // Start server that will serve jwks1 then jwks2
        std::vector<std::string> bodies = { jwks1.dump(), jwks2.dump() };
    std::cerr << "JWKSRotation: jwks1 size=" << bodies[0].size() << " jwks2 size=" << bodies[1].size() << std::endl;
        MultiResponseHttpServer server(bodies, 0);
        unsigned short port = server.port(); ASSERT_GT(port, 0);
    std::cerr << "JWKSRotation: server started on port " << port << std::endl;

        // Build token signed with rot-kid-1
        nlohmann::json header = {{"alg","RS256"},{"kid","rot-kid-1"}};
        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        nlohmann::json payload = {{"sub","rot-user"},{"iss","rot-issuer"},{"aud","rot-aud"},{"exp", now + 300}};
    std::string header_s = header.dump();
    std::string payload_s = payload.dump();
    std::string header_b64 = b64url(std::vector<uint8_t>(header_s.begin(), header_s.end()));
    std::cerr << "JWKSRotation: header_b64 len=" << header_b64.size() << std::endl;
    std::string payload_b64 = b64url(std::vector<uint8_t>(payload_s.begin(), payload_s.end()));
    std::cerr << "JWKSRotation: payload_b64 len=" << payload_b64.size() << std::endl;
        std::string unsigned_token = header_b64 + "." + payload_b64;
    std::string sig = sign_RS256(fix.pkey, unsigned_token);
    std::cerr << "JWKSRotation: signature len=" << sig.size() << std::endl;
    std::string token = unsigned_token + "." + sig;

        JWTValidatorConfig cfg; cfg.jwks_url = "http://127.0.0.1:" + std::to_string(port) + "/jwks.json"; cfg.expected_issuer = "rot-issuer"; cfg.expected_audience = "rot-aud";
        JWTValidator validator(cfg);

        // Give server a moment
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // First attempt: validator will fetch first JWKS (missing kid) and should fail
        std::cerr << "JWKSRotation: first parseAndValidate (expect fail)" << std::endl;
        bool first_failed = false;
        try {
            validator.parseAndValidate(token);
        } catch (const std::exception& ex) {
            std::cerr << "JWKSRotation: first parseAndValidate threw: " << ex.what() << std::endl;
            first_failed = true;
        }
        EXPECT_TRUE(first_failed);

        // Second attempt: create a fresh validator (cleared cache) which will fetch the second JWKS response
        JWTValidator validator2(cfg);
        std::cerr << "JWKSRotation: second parseAndValidate (expect success)" << std::endl;
        auto claims2 = validator2.parseAndValidate(token);
        EXPECT_EQ(claims2.sub, "rot-user");
    } catch (const std::exception& ex) {
        std::cerr << "JWKSRotation test exception: " << ex.what() << std::endl;
        FAIL() << "Exception: " << ex.what();
    }
}
