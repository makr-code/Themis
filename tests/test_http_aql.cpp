#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <set>
#include <filesystem>

#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "storage/base_entity.h"
#include "query/query_engine.h"

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpAqlApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create isolated test database
        const std::string db_path = "data/themis_http_aql_test";
        
        // Clean up old test data
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

        // Start HTTP server
        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18082; // avoid clashes with other HTTP tests
        scfg.num_threads = 2;

        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        setupTestData();
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
    }

    void setupTestData() {
        // Create indexes first so puts maintain them
        auto st1 = secondary_index_->createIndex("users", "city", false);
        ASSERT_TRUE(st1.ok) << st1.message;
        auto st2 = secondary_index_->createRangeIndex("users", "age");
        ASSERT_TRUE(st2.ok) << st2.message;

        // Insert three users via SecondaryIndexManager to maintain indexes
        // Note: PK should be just the ID, table prefix is added by KeySchema::makeRelationalKey
        auto e1 = themis::BaseEntity::fromFields("alice", themis::BaseEntity::FieldMap{{"name","Alice"},{"age","25"},{"city","Berlin"}});
        auto e2 = themis::BaseEntity::fromFields("bob", themis::BaseEntity::FieldMap{{"name","Bob"},{"age","17"},{"city","Hamburg"}});
        auto e3 = themis::BaseEntity::fromFields("charlie", themis::BaseEntity::FieldMap{{"name","Charlie"},{"age","30"},{"city","Munich"}});
        ASSERT_TRUE(secondary_index_->put("users", e1).ok);
        ASSERT_TRUE(secondary_index_->put("users", e2).ok);
        ASSERT_TRUE(secondary_index_->put("users", e3).ok);
    }

    http::response<http::string_body> post(const std::string& target, const json& body) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18082");
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

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(HttpAqlApiTest, AqlEquality_FilterCityBerlin_ReturnsAlice) {
    json req = {
        {"query", "FOR user IN users FILTER user.city == \"Berlin\" RETURN user"},
        {"allow_full_scan", false}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_EQ(body["table"], "users");
    ASSERT_EQ(body["count"], 1);
    ASSERT_TRUE(body["entities"].is_array());
    ASSERT_EQ(body["entities"].size(), 1);
    // Entities are JSON strings
    auto ent = json::parse(body["entities"][0].get<std::string>());
    EXPECT_EQ(ent["name"], "Alice");
    EXPECT_EQ(ent["city"], "Berlin");
}

TEST_F(HttpAqlApiTest, AqlRange_FilterAgeGreater18_ReturnsTwo) {
    json req = {
        {"query", "FOR user IN users FILTER user.age > 18 RETURN user"},
        {"allow_full_scan", false}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_EQ(body["table"], "users");
    ASSERT_EQ(body["count"], 2);
    ASSERT_TRUE(body["entities"].is_array());
    std::set<std::string> names;
    for (const auto& s : body["entities"]) {
        auto ent = json::parse(s.get<std::string>());
        names.insert(ent["name"].get<std::string>());
    }
    EXPECT_TRUE(names.count("Alice") == 1);
    EXPECT_TRUE(names.count("Charlie") == 1);
}

TEST_F(HttpAqlApiTest, AqlEquality_ExplainIncludesPlan) {
    json req = {
        {"query", "FOR user IN users FILTER user.city == \"Berlin\" RETURN user"},
        {"allow_full_scan", false},
        {"optimize", true},
        {"explain", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_EQ(body["count"], 1);
    ASSERT_TRUE(body.contains("plan"));
    auto plan = body["plan"];
    ASSERT_TRUE(plan.contains("mode"));
    // Equality path should be optimized
    EXPECT_EQ(plan["mode"], "index_optimized");
    ASSERT_TRUE(plan.contains("order"));
    ASSERT_TRUE(plan["order"].is_array());
    ASSERT_GE(plan["order"].size(), 1u);
}
