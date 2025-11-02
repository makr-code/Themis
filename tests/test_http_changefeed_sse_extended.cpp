#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <filesystem>
#include <future>
#include <regex>
#include <sstream>

#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpChangefeedSseExtendedTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_changefeed_sse_ext_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }

        themis::RocksDBWrapper::Config cfg;
        cfg.db_path = db_path;
        cfg.memtable_size_mb = 64;
        cfg.block_cache_size_mb = 128;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());

        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);

        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18088; // isolated port
        scfg.num_threads = 2;
        scfg.feature_cdc = true;

        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        std::filesystem::remove_all("data/themis_http_changefeed_sse_ext_test");
    }

    // Basic HTTP helpers
    std::string getRaw(const std::string& target) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("127.0.0.1", "18088");
        stream.connect(results);
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "127.0.0.1");
        http::write(stream, req);
        beast::flat_buffer buf; http::response<http::string_body> res;
        http::read(stream, buf, res);
        beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res.body();
    }

    std::string getRawWithHeader(const std::string& target, const std::string& name, const std::string& value) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("127.0.0.1", "18088");
        stream.connect(results);
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(name, value);
        http::write(stream, req);
        beast::flat_buffer buf; http::response<http::string_body> res;
        http::read(stream, buf, res);
        beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res.body();
    }

    void postEntity(const std::string& key, const json& obj) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("127.0.0.1", "18088");
        stream.connect(results);
        http::request<http::string_body> req{http::verb::post, "/entities", 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::content_type, "application/json");
        json body = {{"key", key}, {"blob", obj.dump()}};
        req.body() = body.dump(); req.prepare_payload();
        http::write(stream, req);
        beast::flat_buffer buf; http::response<http::string_body> res;
        http::read(stream, buf, res);
        beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        ASSERT_TRUE(res.result_int() == 201 || res.result() == http::status::created);
    }

    static std::vector<uint64_t> parseIds(const std::string& body) {
        std::vector<uint64_t> ids; std::regex idre("^id: ([0-9]+)\r?$");
        std::istringstream iss(body); std::string line; std::smatch m;
        while (std::getline(iss, line)) { if (std::regex_match(line, m, idre)) ids.push_back(std::stoull(m[1].str())); }
        return ids;
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(HttpChangefeedSseExtendedTest, LastEventId_Resume) {
    for (int i = 0; i < 5; ++i) postEntity("resume:" + std::to_string(i), json{{"v", i}});
    auto b1 = getRaw("/changefeed/stream?keep_alive=false");
    auto ids1 = parseIds(b1); ASSERT_GE(ids1.size(), 5u); auto last = ids1.back();
    for (int i = 5; i < 8; ++i) postEntity("resume:" + std::to_string(i), json{{"v", i}});
    auto b2 = getRawWithHeader("/changefeed/stream?keep_alive=false", "Last-Event-ID", std::to_string(last));
    auto ids2 = parseIds(b2); ASSERT_FALSE(ids2.empty()); EXPECT_GT(ids2.front(), last);
}

TEST_F(HttpChangefeedSseExtendedTest, Backpressure_DropsVisibleInMetrics) {
    std::thread producer([&]{ for (int i = 0; i < 2000; ++i) postEntity("bp:" + std::to_string(i), json{{"v", i}}); });
    (void)getRaw("/changefeed/stream?keep_alive=true&max_seconds=2&max_events=5&retry_ms=100");
    if (producer.joinable()) producer.join();
    auto metrics = getRaw("/metrics");
    bool found=false; uint64_t drops=0; std::istringstream iss(metrics); std::string line;
    while (std::getline(iss, line)) {
        const std::string prefix = "vccdb_sse_dropped_events_total ";
        if (line.rfind(prefix, 0) == 0) { found=true; try { drops = std::stoull(line.substr(prefix.size())); } catch(...) { drops=0; } break; }
    }
    ASSERT_TRUE(found); EXPECT_GE(drops, 1u);
}

TEST_F(HttpChangefeedSseExtendedTest, PerPoll_MaxEvents_CapsTotalWithinWindow) {
    for (int i = 0; i < 200; ++i) postEntity("cap:" + std::to_string(i), json{{"v", i}});
    auto body = getRaw("/changefeed/stream?keep_alive=true&max_seconds=1&max_events=3&retry_ms=100");
    auto ids = parseIds(body); ASSERT_GE(ids.size(), 1u); EXPECT_LE(ids.size(), 50u);
}
