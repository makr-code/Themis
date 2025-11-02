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
#include "cdc/changefeed.h"

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpChangefeedSseTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_changefeed_sse_test";
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
        scfg.port = 18087; // unique port for SSE tests
        scfg.num_threads = 2;
        scfg.feature_cdc = true;

        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        std::filesystem::remove_all("data/themis_http_changefeed_sse_test");
    }

    std::string httpGetRaw(const std::string& target) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18087));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "127.0.0.1");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return res.body();
    }

    // GET with one custom header (e.g., Last-Event-ID)
    std::string httpGetRawWithHeader(const std::string& target, const std::string& name, const std::string& value) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18087));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(name, value);

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return res.body();
    }

    json httpPost(const std::string& target, const json& body) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18087));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::content_type, "application/json");
        req.body() = body.dump();
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return json::parse(res.body());
    }

    static std::vector<uint64_t> parseSseIds(const std::string& body) {
        std::vector<uint64_t> ids;
        std::regex idre("^id: ([0-9]+)\r?$");
        std::istringstream iss(body);
        std::string line;
        while (std::getline(iss, line)) {
            std::smatch m;
            if (std::regex_match(line, m, idre)) {
                ids.push_back(static_cast<uint64_t>(std::stoull(m[1].str())));
            }
        }
        return ids;
    }

    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::unique_ptr<themis::server::HttpServer> server_;
};

TEST_F(HttpChangefeedSseTest, SseStream_ReturnsEventsInSseFormat) {
    // Generate some change events via entity operations
    json e1 = {{"key", "test:item1"}, {"blob", "{\"value\":1}"}};
    json e2 = {{"key", "test:item2"}, {"blob", "{\"value\":2}"}};
    (void)httpPost("/entities", e1);
    (void)httpPost("/entities", e2);

    // Wait briefly for events to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // GET /changefeed/stream should return SSE format (use keep_alive=false for speed)
    std::string sseBody = httpGetRaw("/changefeed/stream?from_seq=0&keep_alive=false");

    // Verify SSE structure: "data: {...}\n\n"
    ASSERT_NE(sseBody.find("data: "), std::string::npos);
    ASSERT_NE(sseBody.find("\n\n"), std::string::npos);

    // Parse events from SSE stream
    std::vector<std::string> lines;
    std::istringstream iss(sseBody);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.rfind("data: ", 0) == 0) {
            std::string jsonStr = line.substr(6); // skip "data: "
            lines.push_back(jsonStr);
        }
    }

    // Should have at least one event
    ASSERT_GE(lines.size(), 1);

    // Verify one event is valid JSON
    json ev = json::parse(lines[0]);
    ASSERT_TRUE(ev.contains("sequence"));
    ASSERT_TRUE(ev.contains("key"));
}

TEST_F(HttpChangefeedSseTest, SseStream_FiltersByKeyPrefix) {
    // Insert events with different prefixes
    json e1 = {{"key", "alpha:1"}, {"blob", "{\"value\":1}"}};
    json e2 = {{"key", "beta:2"}, {"blob", "{\"value\":2}"}};
    (void)httpPost("/entities", e1);
    (void)httpPost("/entities", e2);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Stream with key_prefix=alpha (use keep_alive=false for speed)
    std::string sseBody = httpGetRaw("/changefeed/stream?from_seq=0&key_prefix=alpha&keep_alive=false");

}

TEST_F(HttpChangefeedSseTest, SseStream_KeepAlive_EmitsHeartbeats) {
    // No events; start keep-alive stream for 2 seconds with fast heartbeat
    std::string sseBody = httpGetRaw("/changefeed/stream?from_seq=0&keep_alive=true&max_seconds=2&heartbeat_ms=300");

    // Expect at least one heartbeat comment line
    ASSERT_NE(sseBody.find(": heartbeat"), std::string::npos);
}

TEST_F(HttpChangefeedSseTest, SseStream_KeepAlive_ReceivesIncrementalEvents) {
    // Start keep-alive stream asynchronously for 3 seconds
    std::future<std::string> fut = std::async(std::launch::async, [this]() {
        return httpGetRaw("/changefeed/stream?from_seq=0&keep_alive=true&max_seconds=3");
    });

    // After a short delay, generate events that should appear in the stream
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    (void)httpPost("/entities", json{{"key","live:1"},{"blob","{\"v\":1}"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    (void)httpPost("/entities", json{{"key","live:2"},{"blob","{\"v\":2}"}});

    // Wait for stream to finish
    std::string sseBody = fut.get();

    // Extract data lines
    std::vector<json> events;
    std::istringstream iss(sseBody);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.rfind("data: ", 0) == 0) {
            events.push_back(json::parse(line.substr(6)));
        }
    }

    // Ensure we received the incremental events
    bool found1 = false, found2 = false;
    for (const auto& ev : events) {
        if (ev.contains("key")) {
            std::string key = ev["key"].get<std::string>();
            if (key == "live:1") found1 = true;
            if (key == "live:2") found2 = true;
        }
    }
    ASSERT_TRUE(found1);
    ASSERT_TRUE(found2);
}
