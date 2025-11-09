#include <gtest/gtest.h>
#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "transaction/transaction_manager.h"
#include "index/secondary_index.h"
#include <filesystem>
#include <httplib.h>

namespace fs = std::filesystem;

class HttpAqlLetTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_aql_let_test";
        fs::remove_all(db_path);

        themis::RocksDBWrapper::Config config;
        config.db_path = db_path;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";

        db_ = std::make_unique<themis::RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());

        tx_mgr_ = std::make_unique<themis::TransactionManager>(*db_);
        sec_idx_ = std::make_unique<themis::SecondaryIndexManager>(*db_);

        nlohmann::json server_config = {
            {"host", "127.0.0.1"},
            {"port", 18082},
            {"num_threads", 2}
        };

        server_ = std::make_unique<themis::HttpServer>(
            server_config, *db_, *tx_mgr_, *sec_idx_,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
        );

        server_thread_ = std::thread([this]() { server_->run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Insert test data
        httplib::Client client("127.0.0.1", 18082);
        client.set_read_timeout(5, 0);

        for (int i = 1; i <= 5; ++i) {
            nlohmann::json doc = {
                {"id", "user" + std::to_string(i)},
                {"name", "User " + std::to_string(i)},
                {"age", 20 + i * 5},
                {"salary", 30000 + i * 10000}
            };
            auto res = client.Post("/documents/users", doc.dump(), "application/json");
            ASSERT_TRUE(res) << "Failed to insert user" << i;
            ASSERT_EQ(res->status, 201) << res->body;
        }
    }

    void TearDown() override {
        if (server_) server_->stop();
        if (server_thread_.joinable()) server_thread_.join();
        server_.reset();
        sec_idx_.reset();
        tx_mgr_.reset();
        db_.reset();
        fs::remove_all("data/themis_http_aql_let_test");
    }

    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::TransactionManager> tx_mgr_;
    std::unique_ptr<themis::SecondaryIndexManager> sec_idx_;
    std::unique_ptr<themis::HttpServer> server_;
    std::thread server_thread_;
};

TEST_F(HttpAqlLetTest, Let_SimpleArithmetic) {
    httplib::Client client("127.0.0.1", 18082);
    client.set_read_timeout(5, 0);

    nlohmann::json req = {
        {"query", "FOR u IN users LET bonus = u.salary * 0.1 RETURN {name: u.name, salary: u.salary, bonus: bonus}"}
    };

    auto res = client.Post("/query/aql", req.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 200) << res->body;

    auto body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body.contains("results"));
    ASSERT_TRUE(body["results"].is_array());
    ASSERT_EQ(body["results"].size(), 5);

    // Check first user: salary=40000, bonus=4000
    auto first = body["results"][0];
    EXPECT_EQ(first["name"], "User 1");
    EXPECT_EQ(first["salary"], 40000);
    EXPECT_EQ(first["bonus"], 4000);
}

TEST_F(HttpAqlLetTest, Let_MultipleLets) {
    httplib::Client client("127.0.0.1", 18082);
    client.set_read_timeout(5, 0);

    nlohmann::json req = {
        {"query", "FOR u IN users LET bonus = u.salary * 0.1 LET total = u.salary + bonus RETURN {name: u.name, total: total}"}
    };

    auto res = client.Post("/query/aql", req.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 200) << res->body;

    auto body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["results"].size(), 5);

    // User 1: salary=40000, bonus=4000, total=44000
    EXPECT_EQ(body["results"][0]["total"], 44000);
    // User 2: salary=50000, bonus=5000, total=55000
    EXPECT_EQ(body["results"][1]["total"], 55000);
}

TEST_F(HttpAqlLetTest, Let_InFilter) {
    httplib::Client client("127.0.0.1", 18082);
    client.set_read_timeout(5, 0);

    nlohmann::json req = {
        {"query", "FOR u IN users LET bonus = u.salary * 0.1 FILTER bonus > 5000 RETURN {name: u.name, bonus: bonus}"}
    };

    auto res = client.Post("/query/aql", req.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 200) << res->body;

    auto body = nlohmann::json::parse(res->body);
    // bonus > 5000: User 2 (5000 not included), User 3 (6000), User 4 (7000), User 5 (8000)
    ASSERT_EQ(body["results"].size(), 3); // Users 3, 4, 5
}

TEST_F(HttpAqlLetTest, Let_WithSort) {
    httplib::Client client("127.0.0.1", 18082);
    client.set_read_timeout(5, 0);

    nlohmann::json req = {
        {"query", "FOR u IN users LET bonus = u.salary * 0.1 SORT bonus DESC RETURN {name: u.name, bonus: bonus}"}
    };

    auto res = client.Post("/query/aql", req.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 200) << res->body;

    auto body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["results"].size(), 5);

    // Sorted DESC by bonus: User 5 (8000), User 4 (7000), User 3 (6000), User 2 (5000), User 1 (4000)
    EXPECT_EQ(body["results"][0]["name"], "User 5");
    EXPECT_EQ(body["results"][0]["bonus"], 8000);
    EXPECT_EQ(body["results"][4]["name"], "User 1");
    EXPECT_EQ(body["results"][4]["bonus"], 4000);
}

TEST_F(HttpAqlLetTest, Let_StringConcatenation) {
    httplib::Client client("127.0.0.1", 18082);
    client.set_read_timeout(5, 0);

    nlohmann::json req = {
        {"query", "FOR u IN users LET fullInfo = CONCAT(u.name, ' (age: ', u.age, ')') RETURN fullInfo"}
    };

    auto res = client.Post("/query/aql", req.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 200) << res->body;

    auto body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["results"].size(), 5);

    // User 1: age=25 -> "User 1 (age: 25)"
    EXPECT_EQ(body["results"][0], "User 1 (age: 25)");
}

TEST_F(HttpAqlLetTest, Let_NestedFieldAccess) {
    httplib::Client client("127.0.0.1", 18082);
    client.set_read_timeout(5, 0);

    // Add user with nested data
    nlohmann::json doc = {
        {"id", "user_nested"},
        {"name", "Nested User"},
        {"details", {{"age", 30}, {"city", "Berlin"}}}
    };
    auto insert_res = client.Post("/documents/users", doc.dump(), "application/json");
    ASSERT_EQ(insert_res->status, 201);

    nlohmann::json req = {
        {"query", "FOR u IN users FILTER u.id == 'user_nested' LET cityInfo = u.details.city RETURN {name: u.name, city: cityInfo}"}
    };

    auto res = client.Post("/query/aql", req.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 200) << res->body;

    auto body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["results"].size(), 1);
    EXPECT_EQ(body["results"][0]["city"], "Berlin");
}

TEST_F(HttpAqlLetTest, Let_ReferenceInReturn) {
    httplib::Client client("127.0.0.1", 18082);
    client.set_read_timeout(5, 0);

    nlohmann::json req = {
        {"query", "FOR u IN users LET x = u.age RETURN x"}
    };

    auto res = client.Post("/query/aql", req.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 200) << res->body;

    auto body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["results"].size(), 5);
    
    // Ages: 25, 30, 35, 40, 45
    EXPECT_EQ(body["results"][0], 25);
    EXPECT_EQ(body["results"][1], 30);
}
