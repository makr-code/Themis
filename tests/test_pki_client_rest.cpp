#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <thread>
#include <sstream>
#include "utils/pki_client.h"

using namespace themis::utils;
using boost::asio::ip::tcp;

// Simple in-process HTTP server that returns a sequence of precomputed JSON responses.
class SimplePkiServer {
public:
    SimplePkiServer(std::vector<std::string> responses, unsigned short port=0)
        : responses_(std::move(responses)), ios_(), acceptor_(ios_) {
        tcp::endpoint ep(tcp::v4(), port);
        acceptor_.open(ep.protocol());
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        acceptor_.bind(ep);
        acceptor_.listen(static_cast<int>(responses_.size()));
        port_ = acceptor_.local_endpoint().port();
        thr_ = std::thread([this]{ run(); });
    }
    ~SimplePkiServer(){
        try { acceptor_.close(); } catch(...) {}
        if (thr_.joinable()) thr_.join();
    }
    unsigned short port() const { return port_; }

private:
    void run(){
        try{
            size_t idx = 0;
            while(idx < responses_.size()){
                tcp::socket sock(ios_);
                boost::system::error_code ec;
                acceptor_.accept(sock, ec);
                if(ec) break;

                std::string body_s = responses_[idx++];
                std::cerr << "SimplePkiServer: accepted connection, sending response index=" << (idx-1) << " len=" << body_s.size() << std::endl;
                std::ostringstream out;
                out << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " << body_s.size() << "\r\nConnection: close\r\n\r\n" << body_s;
                boost::asio::write(sock, boost::asio::buffer(out.str()));
                boost::system::error_code ec2; sock.shutdown(tcp::socket::shutdown_both, ec2);
            }
        } catch(...) {}
    }

    std::vector<std::string> responses_;
    boost::asio::io_context ios_;
    tcp::acceptor acceptor_;
    std::thread thr_;
    unsigned short port_{};
};

TEST(PKIClientRest, SignAndVerify_Success) {
    nlohmann::json sign_resp = {{"signature_b64","TESTSIG"},{"signature_id","srv-sig-1"},{"cert_serial","CERT-1234"}};
    nlohmann::json verify_ok = {{"ok", true}};
    std::vector<std::string> responses = { sign_resp.dump(), verify_ok.dump() };
    SimplePkiServer server(responses, 0);
    unsigned short port = server.port(); ASSERT_GT(port, 0);

    PKIConfig cfg; cfg.endpoint = std::string("http://127.0.0.1:") + std::to_string(port);
    cfg.service_id = "test-service";
    VCCPKIClient client(cfg);

    std::vector<uint8_t> hash = {1,2,3,4,5};
    auto sig = client.signHash(hash);
    EXPECT_TRUE(sig.ok);
    EXPECT_EQ(sig.signature_b64, "TESTSIG");

    bool ok = client.verifyHash(hash, sig);
    EXPECT_TRUE(ok);
}

TEST(PKIClientRest, Verify_FalseFromServer) {
    nlohmann::json sign_resp = {{"signature_b64","TESTSIG"},{"signature_id","srv-sig-1"},{"cert_serial","CERT-1234"}};
    nlohmann::json verify_false = {{"ok", false}};
    std::vector<std::string> responses = { sign_resp.dump(), verify_false.dump() };
    SimplePkiServer server(responses, 0);
    unsigned short port = server.port(); ASSERT_GT(port, 0);

    PKIConfig cfg; cfg.endpoint = std::string("http://127.0.0.1:") + std::to_string(port);
    cfg.service_id = "test-service";
    VCCPKIClient client(cfg);

    std::vector<uint8_t> hash = {9,9,9};
    auto sig = client.signHash(hash);
    EXPECT_TRUE(sig.ok);

    bool ok = client.verifyHash(hash, sig);
    EXPECT_FALSE(ok);
}

