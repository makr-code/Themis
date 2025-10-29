/**
 * @file test_http_index_endpoints.cpp
 * @brief Integration tests for HTTP Index Maintenance Endpoints
 */

#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "storage/base_entity.h"

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpIndexEndpointsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test database
        themis::RocksDBWrapper::Config config;
    config.db_path = "data/themis_http_index_test";
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        
        storage_ = std::make_shared<themis::RocksDBWrapper>(config);
        ASSERT_TRUE(storage_->open()) << "Database could not be opened";

        // Create managers
        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_);

        // Start HTTP server on random available port
        themis::server::HttpServer::Config server_config;
        server_config.host = "127.0.0.1";
        server_config.port = 18080;
        server_config.num_threads = 2;

        server_ = std::make_unique<themis::server::HttpServer>(
            server_config, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_
        );

        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait for server to start
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        storage_->close();
    }

    // Helper: Make HTTP request
    http::response<http::string_body> makeRequest(
        http::verb method,
        const std::string& target,
        const std::string& body = ""
    ) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);

            auto const results = resolver.resolve("127.0.0.1", "18080");
            stream.connect(results);

            http::request<http::string_body> req{method, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/json");
            if (!body.empty()) {
                req.body() = body;
                req.prepare_payload();
            }

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);

            return res;
        } catch (const std::exception& e) {
            EXPECT_TRUE(false) << "Request failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }

    // Helper: Create test entities and index
    void setupTestData() {
        // Create entities
        for (int i = 1; i <= 10; ++i) {
            themis::BaseEntity::FieldMap fields;
            fields["email"] = "user" + std::to_string(i) + "@test.com";
            fields["age"] = std::to_string(20 + i);
            fields["city"] = (i % 2 == 0) ? "Berlin" : "Munich";
            fields["status"] = (i % 3 == 0) ? "premium" : "regular";
            
            auto entity = themis::BaseEntity::fromFields("customers:cust" + std::to_string(i), fields);
            storage_->put(entity.getPrimaryKey(), entity.serialize());
        }

        // Create indexes
        secondary_index_->createIndex("customers", "email", true);
        secondary_index_->createRangeIndex("customers", "age");
        secondary_index_->createIndex("customers", "city", false);
        secondary_index_->createSparseIndex("customers", "status", false);
    }

    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::unique_ptr<themis::server::HttpServer> server_;
};

TEST_F(HttpIndexEndpointsTest, GetIndexStats_SingleColumn_QueryString) {
    setupTestData();

    auto res = makeRequest(http::verb::get, "/index/stats?table=customers&column=email");
    
    EXPECT_EQ(res.result(), http::status::ok);
    
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "customers");
    EXPECT_EQ(body["column"], "email");
    EXPECT_EQ(body["type"], "regular");
    EXPECT_EQ(body["entry_count"], 10);
    EXPECT_EQ(body["unique"], true);
}

TEST_F(HttpIndexEndpointsTest, GetIndexStats_SingleColumn_JsonBody) {
    setupTestData();

    json req_body = {
        {"table", "customers"},
        {"column", "age"}
    };

    auto res = makeRequest(http::verb::get, "/index/stats", req_body.dump());
    
    EXPECT_EQ(res.result(), http::status::ok);
    
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "customers");
    EXPECT_EQ(body["column"], "age");
    EXPECT_EQ(body["type"], "range");
    EXPECT_EQ(body["entry_count"], 10);
    EXPECT_EQ(body["unique"], false);
}

TEST_F(HttpIndexEndpointsTest, GetIndexStats_AllIndexes) {
    setupTestData();

    json req_body = {
        {"table", "customers"}
    };

    auto res = makeRequest(http::verb::get, "/index/stats", req_body.dump());
    
    EXPECT_EQ(res.result(), http::status::ok);
    
    auto body = json::parse(res.body());
    EXPECT_TRUE(body.is_array());
    EXPECT_EQ(body.size(), 4); // email, age, city, status

    // Verify all indexes are present
    std::set<std::string> columns;
    for (const auto& stat : body) {
        columns.insert(stat["column"]);
        EXPECT_EQ(stat["table"], "customers");
        EXPECT_EQ(stat["entry_count"], 10);
    }
    EXPECT_TRUE(columns.count("email") > 0);
    EXPECT_TRUE(columns.count("age") > 0);
    EXPECT_TRUE(columns.count("city") > 0);
    EXPECT_TRUE(columns.count("status") > 0);
}

TEST_F(HttpIndexEndpointsTest, GetIndexStats_MissingTable) {
    auto res = makeRequest(http::verb::get, "/index/stats");
    
    EXPECT_EQ(res.result(), http::status::bad_request);
    
    auto body = json::parse(res.body());
    EXPECT_TRUE(body.contains("error"));
    EXPECT_NE(body["error"].get<std::string>().find("table"), std::string::npos);
}

TEST_F(HttpIndexEndpointsTest, RebuildIndex_Success) {
    setupTestData();

    // Verify index works before rebuild
    auto [st1, entries1] = secondary_index_->scanKeysEqual("customers", "email", "user5@test.com");
    EXPECT_TRUE(st1.ok);
    EXPECT_EQ(entries1.size(), 1);

    json req_body = {
        {"table", "customers"},
        {"column", "email"}
    };

    auto res = makeRequest(http::verb::post, "/index/rebuild", req_body.dump());
    
    EXPECT_EQ(res.result(), http::status::ok);
    
    auto body = json::parse(res.body());
    EXPECT_EQ(body["success"], true);
    EXPECT_EQ(body["table"], "customers");
    EXPECT_EQ(body["column"], "email");
    EXPECT_EQ(body["entry_count"], 10);
    EXPECT_GT(body["estimated_size_bytes"], 0);

    // Verify index still works
    auto [st2, entries2] = secondary_index_->scanKeysEqual("customers", "email", "user5@test.com");
    EXPECT_TRUE(st2.ok);
    EXPECT_EQ(entries2.size(), 1);
}

TEST_F(HttpIndexEndpointsTest, RebuildIndex_MissingParameters) {
    json req_body = {
        {"table", "customers"}
        // Missing "column"
    };

    auto res = makeRequest(http::verb::post, "/index/rebuild", req_body.dump());
    
    EXPECT_EQ(res.result(), http::status::bad_request);
    
    auto body = json::parse(res.body());
    EXPECT_TRUE(body.contains("error"));
}

TEST_F(HttpIndexEndpointsTest, ReindexTable_Success) {
    setupTestData();

    json req_body = {
        {"table", "customers"}
    };

    auto res = makeRequest(http::verb::post, "/index/reindex", req_body.dump());
    
    EXPECT_EQ(res.result(), http::status::ok);
    
    auto body = json::parse(res.body());
    EXPECT_EQ(body["success"], true);
    EXPECT_EQ(body["table"], "customers");
    EXPECT_EQ(body["indexes_rebuilt"], 4); // email, age, city, status

    // Verify indexes array
    EXPECT_TRUE(body.contains("indexes"));
    EXPECT_TRUE(body["indexes"].is_array());
    EXPECT_EQ(body["indexes"].size(), 4);

    for (const auto& idx : body["indexes"]) {
        EXPECT_TRUE(idx.contains("column"));
        EXPECT_TRUE(idx.contains("type"));
        EXPECT_TRUE(idx.contains("entry_count"));
        EXPECT_EQ(idx["entry_count"], 10);
    }

    // Verify all indexes still work
    auto [st1, entries1] = secondary_index_->scanKeysEqual("customers", "email", "user1@test.com");
    EXPECT_TRUE(st1.ok);
    EXPECT_EQ(entries1.size(), 1);
    auto [st2, entries2] = secondary_index_->scanKeysEqual("customers", "city", "Berlin");
    EXPECT_TRUE(st2.ok);
    EXPECT_EQ(entries2.size(), 5);
}

TEST_F(HttpIndexEndpointsTest, ReindexTable_MissingTable) {
    json req_body = {};

    auto res = makeRequest(http::verb::post, "/index/reindex", req_body.dump());
    
    EXPECT_EQ(res.result(), http::status::bad_request);
    
    auto body = json::parse(res.body());
    EXPECT_TRUE(body.contains("error"));
    EXPECT_NE(body["error"].get<std::string>().find("table"), std::string::npos);
}

TEST_F(HttpIndexEndpointsTest, GetIndexStats_CompositeIndex) {
    setupTestData();

    // Create composite index
    secondary_index_->createCompositeIndex("customers", {"status", "city"}, false);

    json req_body = {
        {"table", "customers"},
        {"column", "status+city"}
    };

    auto res = makeRequest(http::verb::get, "/index/stats", req_body.dump());
    
    EXPECT_EQ(res.result(), http::status::ok);
    
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "customers");
    EXPECT_EQ(body["column"], "status+city");
    EXPECT_EQ(body["type"], "composite");
    EXPECT_GT(body["entry_count"], 0);
    EXPECT_TRUE(body.contains("additional_info"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
