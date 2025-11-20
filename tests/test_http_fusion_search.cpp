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
using tcp = net::ip::tcp;
using json = nlohmann::json;

class HttpFusionSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_fusion_search_test";
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

        // HTTP server
        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18086; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Fulltext index and data
        themis::SecondaryIndexManager::FulltextConfig ftcfg; ftcfg.stemming_enabled = true; ftcfg.language = "en"; ftcfg.stopwords_enabled = true;
        auto st = secondary_index_->createFulltextIndex("articles", "content", ftcfg);
        ASSERT_TRUE(st.ok) << st.message;

        // Documents with differing BM25 strength for term "ai"
        auto e1 = themis::BaseEntity::fromFields("a1", themis::BaseEntity::FieldMap{{"title","AI moon base"},{"content","ai ai moon base explores robots ai"}});
        auto e2 = themis::BaseEntity::fromFields("a2", themis::BaseEntity::FieldMap{{"title","AI rocket"},{"content","ai rocket and space exploration"}});
        auto e3 = themis::BaseEntity::fromFields("a3", themis::BaseEntity::FieldMap{{"title","Pizza"},{"content","best pizza recipe with cheese"}});
        ASSERT_TRUE(secondary_index_->put("articles", e1).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e2).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e3).ok);

        // Vector index: small 3D space; closest to q near v1
        ASSERT_TRUE(vector_index_->init("articles", 3, themis::VectorIndexManager::Metric::COSINE, 16, 200, 64).ok);
        themis::BaseEntity v1("a1"); v1.setField("vec", std::vector<float>{1.0f, 0.0f, 0.0f}); ASSERT_TRUE(vector_index_->addEntity(v1, "vec").ok);
        themis::BaseEntity v2("a2"); v2.setField("vec", std::vector<float>{0.0f, 1.0f, 0.0f}); ASSERT_TRUE(vector_index_->addEntity(v2, "vec").ok);
        themis::BaseEntity v3("a3"); v3.setField("vec", std::vector<float>{0.0f, 0.0f, 1.0f}); ASSERT_TRUE(vector_index_->addEntity(v3, "vec").ok);
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        std::filesystem::remove_all("data/themis_http_fusion_search_test");
    }

    http::response<http::string_body> post(const std::string& target, const json& body) {
        net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("127.0.0.1", "18086");
        stream.connect(results);
        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::content_type, "application/json");
        req.body() = body.dump(); req.prepare_payload();
        http::write(stream, req);
        beast::flat_buffer buf; http::response<http::string_body> res; http::read(stream, buf, res);
        beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res;
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(HttpFusionSearchTest, FusionRRF_Basic_TextOnly) {
    json req = {
        {"table","articles"},
        {"k", 5},
        {"fusion_mode","rrf"},
        {"text_column","content"},
        {"text_query","ai"},
        {"text_limit", 100},
        {"tie_break","pk"},
        {"tie_break_epsilon", 1e-9}
    };
    auto res = post("/search/fusion", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_EQ(body.value("fusion_mode", ""), std::string("rrf"));
    ASSERT_EQ(body.value("table", ""), std::string("articles"));
    ASSERT_TRUE(body.contains("results"));
    auto arr = body["results"].get<json::array_t>();
    ASSERT_GE(arr.size(), 2u);
    for (const auto& r : arr) {
        ASSERT_TRUE(r.contains("pk"));
        ASSERT_TRUE(r.contains("score"));
    }
}

TEST_F(HttpFusionSearchTest, FusionWeighted_AliasAlpha_TextDominates) {
    // alpha=1.0 -> Ergebnis wie reiner Text (Top-Dokument sollte a1 sein)
    json req = {
        {"table","articles"},
        {"k", 3},
        {"fusion_mode","weighted"},
        {"text_column","content"},
        {"text_query","ai"},
        {"text_limit", 100},
        {"vector_query", json::array({1.0, 0.0, 0.0})},
        {"vector_limit", 10},
        {"alpha", 1.0},
        {"tie_break","pk"}
    };
    auto res = post("/search/fusion", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_EQ(body.value("fusion_mode", ""), std::string("weighted"));
    ASSERT_TRUE(body.contains("results"));
    auto arr = body["results"].get<json::array_t>();
    ASSERT_GE(arr.size(), 1u);
    // Erwartung: a1 ist wegen mehrfach "ai" im Content Top-BM25 → auch hier Top
    EXPECT_EQ(arr.front().value("pk", ""), std::string("a1"));
}

TEST_F(HttpFusionSearchTest, Cutoff_MinTextScore_FiltersOutAll) {
    // Setze min_text_score sehr hoch, damit keine Texttreffer verbleiben
    json req = {
        {"table","articles"},
        {"k", 5},
        {"fusion_mode","rrf"},
        {"text_column","content"},
        {"text_query","ai"},
        {"text_limit", 100},
        {"min_text_score", 1e9}
    };
    auto res = post("/search/fusion", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body.value("text_count", 12345), 0);
    auto arr = body["results"].get<json::array_t>();
    EXPECT_EQ(arr.size(), 0u);
}

TEST_F(HttpFusionSearchTest, Cutoff_MaxVectorDistance_OneNearest) {
    // Vector-only Suche nahe an a1; enge Distanzschwelle -> nur a1 verbleibt
    json req = {
        {"table","articles"},
        {"k", 5},
        {"fusion_mode","rrf"},
        {"vector_query", json::array({1.0, 0.0, 0.0})},
        {"vector_limit", 10},
        {"max_vector_distance", 0.01}
    };
    auto res = post("/search/fusion", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body.value("vector_count", 12345), 1);
    auto arr = body["results"].get<json::array_t>();
    ASSERT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr.front().value("pk", ""), std::string("a1"));
}

TEST_F(HttpFusionSearchTest, RRF_TieBreak_ByPkOnEqualScores) {
    // Construct equal fused scores by opposing ranks: text A>B, vector B>A
    // Vector query at 45° between a1 and a2 biases ordering to a2 first (due to search/hnsw tie behavior may vary;
    // however with COSINE and this setup it should swap ranks compared to text which prefers a1).
    json req = {
        {"table","articles"},
        {"k", 2},
        {"fusion_mode","rrf"},
        {"text_column","content"},
        {"text_query","ai"},
        {"text_limit", 10},
        {"vector_query", json::array({0.70710678, 0.70710678, 0.0})},
        {"vector_limit", 10},
        {"k_rrf", 60},
        {"tie_break","pk"},
        {"tie_break_epsilon", 1e-12}
    };
    auto res = post("/search/fusion", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("results"));
    auto arr = body["results"].get<json::array_t>();
    ASSERT_EQ(arr.size(), 2u);
    // With equal fused scores, pk-based tie-break should put a1 before a2
    EXPECT_EQ(arr[0].value("pk", ""), std::string("a1"));
    EXPECT_EQ(arr[1].value("pk", ""), std::string("a2"));
}
