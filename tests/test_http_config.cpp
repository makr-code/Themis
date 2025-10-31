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

class HttpConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_config_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
        themis::RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 64; cfg.block_cache_size_mb = 128;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());
        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);
        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18085; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
    }
    http::response<http::string_body> post(const std::string& target, const json& body) {
        try {
            net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18085"); stream.connect(results);
            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, "127.0.0.1"); req.set(http::field::content_type, "application/json");
            req.body() = body.dump(); req.prepare_payload(); http::write(stream, req);
            beast::flat_buffer buf; http::response<http::string_body> res; http::read(stream, buf, res);
            beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec); return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "POST failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }
    http::response<http::string_body> get(const std::string& target) {
        try {
            net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18085"); stream.connect(results);
            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, "127.0.0.1"); http::write(stream, req);
            beast::flat_buffer buf; http::response<http::string_body> res; http::read(stream, buf, res);
            beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec); return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "GET failed: " << e.what();
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

TEST_F(HttpConfigTest, PostConfig_UpdateLoggingAcceptsJson) {
    json body = {
        {"logging", {
            {"level", "debug"},
            {"format", "json"}
        }}
    };
    auto res = post("/config", body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto conf = json::parse(res.body());
    ASSERT_TRUE(conf.contains("server"));
}

TEST_F(HttpConfigTest, PostConfig_UpdateRequestTimeout) {
    // Update timeout to 60 seconds
    json body = {
        {"request_timeout_ms", 60000}
    };
    auto res = post("/config", body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    // Verify change in GET response
    auto get_res = get("/config");
    ASSERT_EQ(get_res.result(), http::status::ok);
    auto conf = json::parse(get_res.body());
    ASSERT_EQ(conf["server"]["request_timeout_ms"], 60000);
}

TEST_F(HttpConfigTest, PostConfig_UpdateFeatureFlags) {
    // Enable CDC feature flag
    json body = {
        {"features", {
            {"cdc", true},
            {"semantic_cache", false}
        }}
    };
    auto res = post("/config", body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    // Verify feature flags in response
    auto conf = json::parse(res.body());
    ASSERT_TRUE(conf.contains("features"));
    ASSERT_EQ(conf["features"]["cdc"], true);
    ASSERT_EQ(conf["features"]["semantic_cache"], false);
}

TEST_F(HttpConfigTest, PostConfig_RejectInvalidTimeout) {
    // Timeout too high (> 5 minutes)
    json body = {
        {"request_timeout_ms", 400000}
    };
    auto res = post("/config", body);
    ASSERT_EQ(res.result(), http::status::bad_request);
    ASSERT_NE(res.body().find("1000-300000"), std::string::npos);
}

TEST_F(HttpConfigTest, GetConfig_ReturnsFeatureFlags) {
    auto res = get("/config");
    ASSERT_EQ(res.result(), http::status::ok);
    auto conf = json::parse(res.body());
    ASSERT_TRUE(conf.contains("features"));
    ASSERT_TRUE(conf["features"].contains("cdc"));
    ASSERT_TRUE(conf["features"].contains("semantic_cache"));
    ASSERT_TRUE(conf["features"].contains("llm_store"));
    ASSERT_TRUE(conf["features"].contains("timeseries"));
}
