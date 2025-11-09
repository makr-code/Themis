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

class HttpAqlJoinTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean test DB
        db_path_ = "./data/themis_http_aql_join_test";
        std::filesystem::remove_all(db_path_);

        themis::RocksDBWrapper::Config cfg; cfg.db_path = db_path_; cfg.memtable_size_mb = 64; cfg.block_cache_size_mb = 128;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());

        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);

        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18093; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        setupData();
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        std::filesystem::remove_all(db_path_);
    }

    void setupData() {
        // Create some simple entities: users and orders
        // Users: u1, u2
        themis::BaseEntity u1("u1"); u1.setField("_key", std::string("u1")); u1.setField("name", std::string("Alice"));
        themis::BaseEntity u2("u2"); u2.setField("_key", std::string("u2")); u2.setField("name", std::string("Bob"));
        ASSERT_TRUE(secondary_index_->put("users", u1).ok);
        ASSERT_TRUE(secondary_index_->put("users", u2).ok);
        // Orders: o1(user u1), o2(user u1), o3(user u2)
        themis::BaseEntity o1("o1"); o1.setField("_key", std::string("o1")); o1.setField("user_id", std::string("u1"));
        themis::BaseEntity o2("o2"); o2.setField("_key", std::string("o2")); o2.setField("user_id", std::string("u1"));
        themis::BaseEntity o3("o3"); o3.setField("_key", std::string("o3")); o3.setField("user_id", std::string("u2"));
        ASSERT_TRUE(secondary_index_->put("orders", o1).ok);
        ASSERT_TRUE(secondary_index_->put("orders", o2).ok);
        ASSERT_TRUE(secondary_index_->put("orders", o3).ok);
    }

    http::response<http::string_body> post(const std::string& target, const json& j) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18093");
            stream.connect(results);

            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::content_type, "application/json");
            req.body() = j.dump();
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

    std::string db_path_;
    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(HttpAqlJoinTest, DoubleFor_EqualityJoin_ReturnLeftVariable) {
    // Expect three results: u1 twice (two orders) and u2 once
    json req = {
        {"query", "FOR u IN users FOR o IN orders FILTER u._key == o.user_id RETURN u"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("count"));
    EXPECT_EQ(body["count"].get<int>(), 3);
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    int aliceCount=0, bobCount=0;
    for (const auto& e : body["entities"]) {
        json ej;
        if (e.is_string()) ej = json::parse(e.get<std::string>());
        else if (e.is_object()) ej = e; else continue;
        auto nameIt = ej.find("name");
        if (nameIt != ej.end()) {
            if (nameIt->is_string() && nameIt->get<std::string>() == "Alice") aliceCount++;
            if (nameIt->is_string() && nameIt->get<std::string>() == "Bob") bobCount++;
        }
    }
    EXPECT_EQ(aliceCount, 2);
    EXPECT_EQ(bobCount, 1);
}
TEST_F(HttpAqlJoinTest, DoubleFor_EqualityJoin_WithLimit) {
    // Test JOIN with LIMIT: expect 2 results
    json req = {
        {"query", "FOR u IN users FOR o IN orders FILTER u._key == o.user_id LIMIT 2 RETURN u"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("count"));
    EXPECT_EQ(body["count"].get<int>(), 2);
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    EXPECT_EQ(body["entities"].size(), 2);
}

TEST_F(HttpAqlJoinTest, DoubleFor_EqualityJoin_NoMatch) {
    // Join condition references non-existing key -> expect 0 results
    json req = {
        {"query", "FOR u IN users FOR o IN orders FILTER u._key == o.user_id FILTER u._key == 'xxx' RETURN o"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"].get<int>(), 0);
    EXPECT_TRUE(body["entities"].is_array());
    EXPECT_EQ(body["entities"].size(), 0);
}

TEST_F(HttpAqlJoinTest, DoubleFor_EqualityJoin_WithAdditionalFilter) {
    // Additional filter on inner side reduces results; RETURN single bound variable (supported)
    json req = {
        {"query", "FOR u IN users FOR o IN orders FILTER u._key == o.user_id FILTER o._key == 'o1' RETURN o"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"].get<int>(), 1);
    auto ent = body["entities"][0];
    json ej = ent.is_string() ? json::parse(ent.get<std::string>()) : ent;
    EXPECT_EQ(ej["_key"].get<std::string>(), "o1");
}

TEST_F(HttpAqlJoinTest, DoubleFor_EqualityJoin_WithLetBinding) {
    // LET binding of a scalar field (supported in MVP) but RETURN must be a bound variable (u or o)
    // We still declare the LET variable to ensure it does not break execution.
    json req = {
        {"query", "FOR u IN users FOR o IN orders LET combined = u.name FILTER u._key == o.user_id RETURN u"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"].get<int>(), 3);
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    int aliceCount = 0, bobCount = 0;
    for (const auto& e : body["entities"]) {
        json ej = e.is_string() ? json::parse(e.get<std::string>()) : e;
        if (ej.contains("name") && ej["name"].is_string()) {
            if (ej["name"].get<std::string>() == "Alice") aliceCount++;
            if (ej["name"].get<std::string>() == "Bob") bobCount++;
        }
    }
    EXPECT_EQ(aliceCount, 2);
    EXPECT_EQ(bobCount, 1);
}

TEST_F(HttpAqlJoinTest, DoubleFor_EqualityJoin_ReturnRightVariable) {
    // Constructed object RETURN is not supported in MVP; assert right variable RETURN
    json req = {
        {"query", "FOR u IN users FOR o IN orders FILTER u._key == o.user_id RETURN o"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("count"));
    EXPECT_EQ(body["count"].get<int>(), 3);
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    
    // Verify structure: each entity should be an object with _key
    int validCount = 0;
    for (const auto& e : body["entities"]) {
        json ej;
        if (e.is_string()) ej = json::parse(e.get<std::string>());
        else if (e.is_object()) ej = e; else continue;
        if (ej.contains("_key")) {
            validCount++;
            EXPECT_TRUE(ej["_key"].is_string());
        }
    }
    EXPECT_EQ(validCount, 3);
}

