#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <filesystem>
#include <set>
#include <optional>

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

class HttpAqlCollectTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_aql_collect_test";
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
        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18084; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        setupData();
    }
    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
    }
    void setupData() {
        ASSERT_TRUE(secondary_index_->createIndex("users", "city", false).ok);
        ASSERT_TRUE(secondary_index_->createRangeIndex("users", "age").ok);
        std::vector<themis::BaseEntity> users = {
            themis::BaseEntity::fromFields("alice", themis::BaseEntity::FieldMap{{"name","Alice"},{"age", "25"},{"city","Berlin"}}),
            themis::BaseEntity::fromFields("bob", themis::BaseEntity::FieldMap{{"name","Bob"},{"age", "17"},{"city","Hamburg"}}),
            themis::BaseEntity::fromFields("diana", themis::BaseEntity::FieldMap{{"name","Diana"},{"age", "28"},{"city","Berlin"}})
        };
        for (const auto& u : users) ASSERT_TRUE(secondary_index_->put("users", u).ok);
    }
    http::response<http::string_body> post(const std::string& target, const json& body) {
        try {
            net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18084"); stream.connect(results);
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

TEST_F(HttpAqlCollectTest, Collect_GroupByCity_Count) {
    json req = {
        {"query", "FOR u IN users COLLECT g = u.city AGGREGATE c = COUNT()"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("groups"));
    auto groups = body["groups"].get<json::array_t>();
    // Expect two groups
    ASSERT_EQ(groups.size(), 2);
    // Verify Berlin has count 2
    int berlinCnt = -1; int hamburgCnt = -1;
    for (const auto& g : groups) {
        std::string city = g["g"].get<std::string>();
        int c = g["c"].get<int>();
        if (city == "Berlin") berlinCnt = c;
        if (city == "Hamburg") hamburgCnt = c;
    }
    EXPECT_EQ(berlinCnt, 2);
    EXPECT_EQ(hamburgCnt, 1);
}

TEST_F(HttpAqlCollectTest, Collect_Global_AvgAge_Berlin) {
    json req = {
        {"query", "FOR u IN users FILTER u.city == 'Berlin' COLLECT AGGREGATE avgAge = AVG(u.age)"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("groups"));
    auto groups = body["groups"].get<json::array_t>();
    ASSERT_EQ(groups.size(), 1);
    double avg = groups[0]["avgAge"].get<double>();
    EXPECT_NEAR(avg, (25.0 + 28.0) / 2.0, 1e-9);
}

TEST_F(HttpAqlCollectTest, Collect_MultiGroup_BooleanKey) {
    json req = {
        {"query", "FOR u IN users COLLECT city = u.city, isAdult = u.age >= 18 AGGREGATE cnt = COUNT()"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("groups"));
    auto groups = body["groups"].get<json::array_t>();
    ASSERT_EQ(groups.size(), 2);

    std::optional<int> berlinAdult;
    std::optional<int> hamburgMinor;
    for (const auto& g : groups) {
        ASSERT_TRUE(g.contains("city"));
        ASSERT_TRUE(g.contains("isAdult"));
        ASSERT_TRUE(g.contains("cnt"));
        std::string city = g["city"].get<std::string>();
        bool isAdult = g["isAdult"].get<bool>();
        int cnt = g["cnt"].get<int>();
        if (city == "Berlin" && isAdult) {
            berlinAdult = cnt;
        } else if (city == "Hamburg" && !isAdult) {
            hamburgMinor = cnt;
        } else {
            FAIL() << "Unexpected group: " << g.dump();
        }
    }
    ASSERT_TRUE(berlinAdult.has_value());
    ASSERT_TRUE(hamburgMinor.has_value());
    EXPECT_EQ(*berlinAdult, 2);
    EXPECT_EQ(*hamburgMinor, 1);
}

TEST_F(HttpAqlCollectTest, Collect_HavingFiltersGroups) {
    json req = {
        {"query", "FOR u IN users COLLECT city = u.city AGGREGATE total = COUNT() HAVING total >= 2"},
        {"allow_full_scan", true}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("groups"));
    auto groups = body["groups"].get<json::array_t>();
    ASSERT_EQ(groups.size(), 1);
    const auto& group = groups.front();
    ASSERT_TRUE(group.contains("city"));
    ASSERT_TRUE(group.contains("total"));
    EXPECT_EQ(group["city"].get<std::string>(), "Berlin");
    EXPECT_EQ(group["total"].get<int>(), 2);
}
