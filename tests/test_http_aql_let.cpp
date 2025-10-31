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
#include "storage/base_entity.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using json = nlohmann::json;
using tcp = net::ip::tcp;

class HttpAqlLetTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_aql_let_test";
        std::filesystem::remove_all(db_path);
        themis::RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 64; cfg.block_cache_size_mb = 128;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());
        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);
        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18094; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start(); std::this_thread::sleep_for(std::chrono::milliseconds(100));
        setupData();
    }
    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
    }
    void setupData() {
        std::vector<themis::BaseEntity> users = {
            themis::BaseEntity::fromFields("alice", themis::BaseEntity::FieldMap{{"name","Alice"},{"age","25"},{"city","Berlin"}}),
            themis::BaseEntity::fromFields("bob", themis::BaseEntity::FieldMap{{"name","Bob"},{"age","17"},{"city","Hamburg"}})
        };
        for (const auto& u : users) ASSERT_TRUE(secondary_index_->put("users", u).ok);
    }
    http::response<http::string_body> post(const std::string& target, const json& body) {
        try {
            net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18094"); stream.connect(results);
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
    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(HttpAqlLetTest, LetAndReturnObjectProjection) {
    json req = {
        {"query", "FOR u IN users LET c = u.city RETURN {name: u.name, city: c}"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    // Expect two results with object projection
    ASSERT_EQ(body["entities"].size(), 2);
    std::set<std::string> cities;
    std::set<std::string> names;
    for (const auto& e : body["entities"]) {
        json ej = e.is_string() ? json::parse(e.get<std::string>()) : e;
        ASSERT_TRUE(ej.contains("name"));
        ASSERT_TRUE(ej.contains("city"));
        names.insert(ej["name"].get<std::string>());
        cities.insert(ej["city"].get<std::string>());
    }
    EXPECT_TRUE(names.count("Alice") == 1);
    EXPECT_TRUE(names.count("Bob") == 1);
    EXPECT_TRUE(cities.count("Berlin") == 1);
    EXPECT_TRUE(cities.count("Hamburg") == 1);
}

TEST_F(HttpAqlLetTest, LetUsedInFilter_ReturnsOnlyBerlin) {
    // Should filter to only the user from Berlin (Alice)
    json req = {
        {"query", "FOR u IN users LET c = u.city FILTER c == \"Berlin\" RETURN u"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("count"));
    ASSERT_EQ(body["count"].get<int>(), 1);
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_EQ(body["entities"].size(), 1);
    json ej = body["entities"][0].is_string() ? json::parse(body["entities"][0].get<std::string>()) : body["entities"][0];
    ASSERT_TRUE(ej.contains("name"));
    EXPECT_EQ(ej["name"].get<std::string>(), "Alice");
}

TEST_F(HttpAqlLetTest, Explain_IncludesLetPreExtractedFlag) {
    // Prüft, dass EXPLAIN bei LET-in-FILTER den Plan mit let_pre_extracted = true liefert
    json req = {
        {"query", "FOR u IN users LET c = u.city FILTER c == \"Berlin\" RETURN u"},
        {"allow_full_scan", true},
        {"explain", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("plan")) << body.dump();
    auto plan = body["plan"];
    ASSERT_TRUE(plan.is_object());
    ASSERT_TRUE(plan.contains("let_pre_extracted"));
    EXPECT_TRUE(plan["let_pre_extracted"].get<bool>());
}
