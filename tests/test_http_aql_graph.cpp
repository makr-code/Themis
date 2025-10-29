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

class HttpAqlGraphApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean test DB
        db_path_ = "./data/themis_http_aql_graph_test";
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
        scfg.port = 18092; // separate port from other tests
        scfg.num_threads = 2;
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
        // user1 -> user2 -> user3
        themis::BaseEntity e1("edge1");
        e1.setField("id", std::string("edge1"));
        e1.setField("_from", std::string("user1"));
        e1.setField("_to", std::string("user2"));
        e1.setField("_weight", 1.0);
        e1.setField("ts", std::string("2025-10-28"));
        ASSERT_TRUE(graph_index_->addEdge(e1).ok);

        themis::BaseEntity e2("edge2");
        e2.setField("id", std::string("edge2"));
        e2.setField("_from", std::string("user2"));
        e2.setField("_to", std::string("user3"));
        e2.setField("_weight", 2.0);
        e2.setField("ts", std::string("2025-10-29"));
        ASSERT_TRUE(graph_index_->addEdge(e2).ok);
    }

    http::response<http::string_body> post(const std::string& target, const json& j) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18092");
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

TEST_F(HttpAqlGraphApiTest, Traversal_OneToTwoHops) {
    // Expect: depth 1..2 from user1 yields user2 and user3 => count 2
    json req = {
        {"query", "FOR v IN 1..2 OUTBOUND 'user1' GRAPH 'social' RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 2);
}

TEST_F(HttpAqlGraphApiTest, Traversal_OneHopOnly) {
    // Expect: depth 1..1 from user1 yields only user2 => count 1
    json req = {
        {"query", "FOR v IN 1..1 OUTBOUND 'user1' GRAPH 'social' RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_ReturnEdges) {
    // Expect: RETURN e liefert die Kanten edge1 und edge2 bei 1..2 OUTBOUND
    json req = {
        {"query", "FOR v IN 1..2 OUTBOUND 'user1' GRAPH 'social' RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 2);
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    std::set<std::string> ids;
    for (const auto& e : body["entities"]) {
        json ej;
        if (e.is_string()) {
            ej = json::parse(e.get<std::string>());
        } else if (e.is_object()) {
            ej = e;
        } else {
            ADD_FAILURE() << "Unexpected entity type for edge: " << e.dump();
            continue;
        }
        ASSERT_TRUE(ej.contains("id"));
        ids.insert(ej["id"].get<std::string>());
    }
    EXPECT_TRUE(ids.count("edge1") == 1);
    EXPECT_TRUE(ids.count("edge2") == 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_ReturnPaths) {
    // Expect: RETURN p liefert Pfade zu user2 (1 Kante) und user3 (2 Kanten)
    json req = {
        {"query", "FOR v IN 1..2 OUTBOUND 'user1' GRAPH 'social' RETURN p"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 2);
    ASSERT_TRUE(body.contains("entities"));
    ASSERT_TRUE(body["entities"].is_array());
    bool sawLen1 = false;
    bool sawLen2 = false;
    for (const auto& p : body["entities"]) {
        ASSERT_TRUE(p.is_object());
        ASSERT_TRUE(p.contains("vertices"));
        ASSERT_TRUE(p.contains("edges"));
        auto v = p["vertices"];
        auto e = p["edges"];
        ASSERT_TRUE(v.is_array());
        ASSERT_TRUE(e.is_array());
        int len = static_cast<int>(e.size());
        if (len == 1) {
            sawLen1 = true;
            // Ziel sollte user2 sein
            auto lastV = v.back();
            if (lastV.is_object() && lastV.contains("_from")) {
                // Edge-ähnlich? Ignorieren, wir prüfen nur Länge
            }
        } else if (len == 2) {
            sawLen2 = true;
        }
    }
    EXPECT_TRUE(sawLen1);
    EXPECT_TRUE(sawLen2);
}

TEST_F(HttpAqlGraphApiTest, Traversal_ForVEP_Syntax_Works) {
    // Prüft Parserunterstützung für FOR v,e,p und Edge-/Pfad-Return
    // Edges
    {
        json req = {
            {"query", "FOR v,e,p IN 1..2 OUTBOUND 'user1' GRAPH 'social' RETURN e"}
        };
        auto res = post("/query/aql", req);
        ASSERT_EQ(res.result(), http::status::ok) << res.body();
        auto body = json::parse(res.body());
        EXPECT_EQ(body["count"], 2);
    }
    // Paths
    {
        json req = {
            {"query", "FOR v,e,p IN 1..2 OUTBOUND 'user1' GRAPH 'social' RETURN p"}
        };
        auto res = post("/query/aql", req);
        ASSERT_EQ(res.result(), http::status::ok) << res.body();
        auto body = json::parse(res.body());
        EXPECT_EQ(body["count"], 2);
    }
}

TEST_F(HttpAqlGraphApiTest, Traversal_FilterOnEdgeId_ReturnsOnlyMatchingVertex) {
    // Filter: nur Kante edge2 soll matchen => Ergebnis: nur user3 (2. Hop)
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e.id == 'edge2' RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_FilterOnVertexKey_ReturnsOnlyUser2) {
    // Filter: v._key == 'user2' => nur erster Hop
    json req = {
        {"query", "FOR v IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER v._key == 'user2' RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_FilterOnEdgeWeight_ReturnsEdge2) {
    // _weight: edge1=1.0, edge2=2.0 → Filter >1.5 sollte nur edge2 liefern (als Vertex user3)
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e._weight > 1.5 RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_FilterOnEdgeDate_ReturnsEdge2) {
    // ts: edge1=2025-10-28, edge2=2025-10-29 → Filter >= '2025-10-29' liefert nur edge2
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e.ts >= '2025-10-29' RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_XOR_TwoVertexPredicates) {
    // XOR über zwei Vertex-Bedingungen: trifft für user2 bzw. user3 jeweils genau eine
    json req = {
        {"query", "FOR v IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER v._key == 'user2' XOR v._key == 'user3' RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 2); // user2 und user3
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_XOR_MixedVertexAndEdge) {
    // XOR zwischen Kanten-ID (edge1) und Zielknoten user3 → beide Hops sollten matchen
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e.id == 'edge1' XOR v._key == 'user3' RETURN v"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["table"], "graph");
    EXPECT_EQ(body["count"], 2);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_ABS_OnWeight) {
    // ABS(-2) == 2.0 → sollte nur edge2 matchen
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e._weight == ABS(-2) RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_DATE_TRUNC_OnEdgeTs) {
    // DATE_TRUNC('day', '2025-10-29T23:59:59Z') → '2025-10-29' → nur edge2
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e.ts >= DATE_TRUNC('day','2025-10-29T23:59:59Z') RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_CEIL_FiltersEdge2) {
    // CEIL(1.1) = 2 → nur edge2 erfüllt e._weight >= 2
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e._weight >= CEIL(1.1) RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_FLOOR_FiltersBoth) {
    // FLOOR(1.9) = 1 → e._weight >= 1 trifft für edge1 und edge2
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e._weight >= FLOOR(1.9) RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 2);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_ROUND_OnWeight) {
    // ROUND(1.6) = 2 → nur edge2
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e._weight == ROUND(1.6) RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_POW_OnWeight) {
    // POW(2,1) = 2 → nur edge2
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e._weight == POW(2,1) RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_DATE_ADD_DAY) {
    // DATE_ADD('2025-10-28',1,'day') = '2025-10-29' → nur edge2 bei >=
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e.ts >= DATE_ADD('2025-10-28',1,'day') RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_DATE_ADD_MONTH) {
    // DATE_ADD('2025-09-29',1,'month') = '2025-10-29' → nur edge2 bei >=
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e.ts >= DATE_ADD('2025-09-29',1,'month') RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}

TEST_F(HttpAqlGraphApiTest, Traversal_Filter_Function_DATE_SUB_DAY) {
    // DATE_SUB('2025-10-29',1,'day') = '2025-10-28' → > liefert nur edge2
    json req = {
        {"query", "FOR v,e IN 1..2 OUTBOUND 'user1' GRAPH 'social' FILTER e.ts > DATE_SUB('2025-10-29',1,'day') RETURN e"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["count"], 1);
}
