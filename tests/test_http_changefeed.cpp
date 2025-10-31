#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <filesystem>

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

class HttpChangefeedTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_aql_test"; // reuse isolated path
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
        scfg.port = 18090; // dedicated port for this suite
        scfg.num_threads = 2;
        scfg.feature_cdc = true; // enable CDC

        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
    }

    http::response<http::string_body> httpGet(const std::string& target) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18090");
            stream.connect(results);

            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, "127.0.0.1");
            http::write(stream, req);

            beast::flat_buffer buf;
            http::response<http::string_body> res;
            http::read(stream, buf, res);
            beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "GET failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }

    http::response<http::string_body> httpPost(const std::string& target, const json& body) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18090");
            stream.connect(results);

            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::content_type, "application/json");
            req.body() = body.dump();
            req.prepare_payload();

            http::write(stream, req);
            beast::flat_buffer buf;
            http::response<http::string_body> res;
            http::read(stream, buf, res);
            beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "POST failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }

    http::response<http::string_body> httpPut(const std::string& target, const json& body) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18090");
            stream.connect(results);

            http::request<http::string_body> req{http::verb::put, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::content_type, "application/json");
            req.body() = body.dump();
            req.prepare_payload();

            http::write(stream, req);
            beast::flat_buffer buf;
            http::response<http::string_body> res;
            http::read(stream, buf, res);
            beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "PUT failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }

    http::response<http::string_body> httpDelete(const std::string& target) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18090");
            stream.connect(results);

            http::request<http::string_body> req{http::verb::delete_, target, 11};
            req.set(http::field::host, "127.0.0.1");

            http::write(stream, req);
            beast::flat_buffer buf;
            http::response<http::string_body> res;
            http::read(stream, buf, res);
            beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "DELETE failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(HttpChangefeedTest, Changefeed_EmptyInitially) {
    auto res = httpGet("/changefeed?from_seq=0&limit=10");
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("events"));
    EXPECT_TRUE(body["events"].is_array());
    EXPECT_EQ(body["events"].size(), 0);
    ASSERT_TRUE(body.contains("latest_sequence"));
    EXPECT_EQ(body["latest_sequence"].get<uint64_t>(), 0ULL);
}

TEST_F(HttpChangefeedTest, Changefeed_PutAndDelete_ProducesEvents) {
    // PUT entity
        json putEntity = {{"name","Alice"},{"age",25}};
        json putBody = {{"blob", putEntity.dump()}};
    auto putRes = httpPut("/entities/users:alice", putBody);
    ASSERT_TRUE(putRes.result() == http::status::created || putRes.result() == http::status::ok) << putRes.body();

    // Read changefeed from 0
    auto cf1 = httpGet("/changefeed?from_seq=0&limit=10");
    ASSERT_EQ(cf1.result(), http::status::ok) << cf1.body();
    auto b1 = json::parse(cf1.body());
    ASSERT_TRUE(b1.contains("events"));
    ASSERT_GE(b1["events"].size(), 1u);
    auto ev = b1["events"][0];
    ASSERT_TRUE(ev.contains("type"));
    EXPECT_EQ(ev["type"].get<std::string>(), "PUT");
    EXPECT_EQ(ev["key"].get<std::string>(), "users:alice");

    uint64_t next = b1.value("latest_sequence", 0ULL);
    ASSERT_GT(next, 0ULL);

    // DELETE entity
    auto delRes = httpDelete("/entities/users:alice");
    ASSERT_EQ(delRes.result(), http::status::ok) << delRes.body();

    // Read from previous latest sequence
    auto cf2 = httpGet(std::string("/changefeed?from_seq=") + std::to_string(next) + "&limit=10");
    ASSERT_EQ(cf2.result(), http::status::ok) << cf2.body();
    auto b2 = json::parse(cf2.body());
    ASSERT_TRUE(b2.contains("events"));
    ASSERT_GE(b2["events"].size(), 1u);
    auto ev2 = b2["events"][0];
    ASSERT_TRUE(ev2.contains("type"));
    EXPECT_EQ(ev2["type"].get<std::string>(), "DELETE");
    EXPECT_EQ(ev2["key"].get<std::string>(), "users:alice");
}

TEST_F(HttpChangefeedTest, Changefeed_LongPoll_ReturnsOnNewEvent) {
    // Get current latest seq
    auto cf0 = httpGet("/changefeed?from_seq=0&limit=1");
    ASSERT_EQ(cf0.result(), http::status::ok);
    uint64_t latest = json::parse(cf0.body()).value("latest_sequence", 0ULL);

    std::atomic<bool> got{false};
    json responseJson;

    std::thread waiter([&]{
        auto res = httpGet(std::string("/changefeed?from_seq=") + std::to_string(latest) + "&limit=10&long_poll_ms=800");
        ASSERT_EQ(res.result(), http::status::ok) << res.body();
        responseJson = json::parse(res.body());
        got = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
        json putEntity2 = {{"name","Bob"},{"age",30}};
        json putBody = {{"blob", putEntity2.dump()}};
    auto putRes = httpPut("/entities/users:bob", putBody);
    ASSERT_TRUE(putRes.result() == http::status::created || putRes.result() == http::status::ok) << putRes.body();

    waiter.join();
    ASSERT_TRUE(got.load());
    ASSERT_TRUE(responseJson.contains("events"));
    ASSERT_GE(responseJson["events"].size(), 1u);
    auto ev = responseJson["events"][0];
    EXPECT_EQ(ev.value("type", std::string("")), std::string("PUT"));
    EXPECT_EQ(ev.value("key", std::string("")), std::string("users:bob"));
}

TEST_F(HttpChangefeedTest, Changefeed_KeyPrefix_Filter_And_Retention) {
    // Create two different keys
    json e1 = {{"name","X"}}; json b1 = {{"blob", e1.dump()}};
    json e2 = {{"name","Y"}}; json b2 = {{"blob", e2.dump()}};
    auto r1 = httpPut("/entities/orders:001", b1);
    auto r2 = httpPut("/entities/users:002", b2);
    ASSERT_TRUE((r1.result()==http::status::created || r1.result()==http::status::ok) && (r2.result()==http::status::created || r2.result()==http::status::ok));

    // Fetch only orders:* via key_prefix
    auto cf = httpGet("/changefeed?from_seq=0&limit=100&key_prefix=orders:");
    ASSERT_EQ(cf.result(), http::status::ok) << cf.body();
    auto jb = json::parse(cf.body());
    ASSERT_TRUE(jb.contains("events"));
    bool onlyOrders = true;
    for (auto& ev : jb["events"]) {
        if (ev.value("key", std::string()).rfind("orders:", 0) != 0) { onlyOrders = false; break; }
    }
    EXPECT_TRUE(onlyOrders);

    // Get latest seq
    uint64_t latest = jb.value("latest_sequence", 0ULL);
    ASSERT_GT(latest, 0ULL);

    // Retain only after latest (delete everything strictly before latest)
    auto ret = httpPost("/changefeed/retention", json{{"before_sequence", latest}});
    ASSERT_EQ(ret.result(), http::status::ok) << ret.body();
    auto jr = json::parse(ret.body());
    ASSERT_TRUE(jr.contains("deleted"));

    // Stats should still report latest_sequence >= previous latest
    auto st = httpGet("/changefeed/stats");
    ASSERT_EQ(st.result(), http::status::ok) << st.body();
    auto js = json::parse(st.body());
    ASSERT_TRUE(js.contains("latest_sequence"));
}
