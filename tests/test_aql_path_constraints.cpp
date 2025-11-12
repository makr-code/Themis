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

class HttpAqlPathConstraintsTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = "./data/themis_http_aql_path_constraints_test";
        std::filesystem::remove_all(db_path_);

        themis::RocksDBWrapper::Config cfg;
        cfg.db_path = db_path_;
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
        scfg.port = 18112; // separate port
        scfg.num_threads = 1;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        setupGraph();
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        std::filesystem::remove_all(db_path_);
    }

    void setupGraph() {
        themis::BaseEntity e1("edge1");
        e1.setField("id", std::string("edge1"));
        e1.setField("_from", std::string("user1"));
        e1.setField("_to", std::string("user2"));
        e1.setField("_weight", 1.0);
        e1.setField("type", std::string("follows"));
        ASSERT_TRUE(graph_index_->addEdge(e1).ok);

        themis::BaseEntity e2("edge2");
        e2.setField("id", std::string("edge2"));
        e2.setField("_from", std::string("user2"));
        e2.setField("_to", std::string("user3"));
        e2.setField("_weight", 2.0);
        e2.setField("type", std::string("likes"));
        ASSERT_TRUE(graph_index_->addEdge(e2).ok);
    }

    http::response<http::string_body> post(const std::string& target, const json& j) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18112");
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

TEST_F(HttpAqlPathConstraintsTest, PathAll_EdgeTypeFilter) {
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER PATH.ALL(e, e.type == 'follows') RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlPathConstraintsTest, PathAny_EdgeWeight) {
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER PATH.ANY(e, e._weight > 1) RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlPathConstraintsTest, PathNone_VertexBlocked) {
    // Mark user2 as blocked and test PATH.NONE
    // Create user2 entity
    themis::BaseEntity u2("user2");
    u2.setField("_key", std::string("user2"));
    u2.setField("blocked", true);
    auto blob = u2.toBinary();
    storage_->put(std::string("users:") + "user2", blob);

    json req = {
        {"query", "FOR v IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER PATH.NONE(v, v.blocked == true) RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    // PATH.NONE should exclude any path that contains blocked vertex; since user2 is blocked, only user3 paths might be excluded too
    // Expect result count 0 because first hop contains blocked vertex (user2) and PATH.NONE requires no blocked vertices in path
    EXPECT_EQ(body["count"], 0);
}
