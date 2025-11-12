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

class HttpAqlShortestPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = "./data/themis_http_aql_shortestpath_test";
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
        scfg.port = 18102; // separate port
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
        ASSERT_TRUE(graph_index_->addEdge(e1).ok);

        themis::BaseEntity e2("edge2");
        e2.setField("id", std::string("edge2"));
        e2.setField("_from", std::string("user2"));
        e2.setField("_to", std::string("user3"));
        e2.setField("_weight", 2.0);
        ASSERT_TRUE(graph_index_->addEdge(e2).ok);
    }

    http::response<http::string_body> post(const std::string& target, const json& j) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18102");
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

TEST_F(HttpAqlShortestPathTest, ShortestPath_ReturnsVerticesAndCost) {
    json req = {
        {"query", "RETURN shortestPath('user1','user3')"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    // Expect a single result row
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    ASSERT_EQ(body["entities"].size(), 1);
    auto ent = body["entities"][0];
    // The server may wrap the returned value; accept object with vertices/totalCost or stringified JSON
    json pj;
    if (ent.is_string()) {
        pj = json::parse(ent.get<std::string>());
    } else {
        pj = ent;
    }
    ASSERT_TRUE(pj.is_object());
    ASSERT_TRUE(pj.contains("vertices"));
    ASSERT_TRUE(pj.contains("totalCost"));
    ASSERT_TRUE(pj.contains("edges"));
    auto verts = pj["vertices"];
    double cost = pj["totalCost"].get<double>();
    auto edges = pj["edges"];
    ASSERT_TRUE(verts.is_array());
    // Path should be user1,user2,user3
    ASSERT_EQ(verts.size(), 3);
    EXPECT_EQ(verts[0].get<std::string>(), "user1");
    EXPECT_EQ(verts[1].get<std::string>(), "user2");
    EXPECT_EQ(verts[2].get<std::string>(), "user3");
    EXPECT_DOUBLE_EQ(cost, 3.0);
    // Edges should be edge1, edge2
    ASSERT_TRUE(edges.is_array());
    ASSERT_EQ(edges.size(), 2);
    EXPECT_EQ(edges[0].get<std::string>(), "edge1");
    EXPECT_EQ(edges[1].get<std::string>(), "edge2");
}
