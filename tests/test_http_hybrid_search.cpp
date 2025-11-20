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

// Minimal Hybrid HTTP tests: tie-break ordering & IN/RANGE filters.

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpHybridSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_hybrid_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
        themis::RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 32; cfg.block_cache_size_mb = 64;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());
        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);

        // Init vector index for hybrid collection
        auto st = vector_index_->init("hybrid_docs", 3, themis::VectorIndexManager::Metric::COSINE, 16, 200, 64);
        ASSERT_TRUE(st.ok) << st.message;

        // Start server
        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18086; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));

        seedData();
        // Provide whitelist mapping for filters (dataset -> coll, score numeric) if required by server logic
        json filterSchema = {
            {"collections", {
                {"hybrid_docs", {
                    {"filter_whitelist", json::array({"dataset","score"})}
                }}
            }}
        };
        auto fstr = filterSchema.dump();
        storage_->put("config:content_filter_schema", std::vector<uint8_t>(fstr.begin(), fstr.end()));
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        std::filesystem::remove_all("data/themis_http_hybrid_test");
    }

    void seedData() {
        // Two embeddings designed to yield identical cosine distance to query to test tie-break (docA, docB)
        // Query vector later: {1,0,0}. Both docA & docB have identical vector.
        themis::BaseEntity a("docA");
        a.setField("embedding", std::vector<float>{1.0f, 0.0f, 0.0f});
        a.setField("dataset", std::string("alpha"));
        a.setField("score", std::string("10")); // treat as numeric string for RANGE test
        ASSERT_TRUE(vector_index_->addEntity(a, "embedding").ok);

        themis::BaseEntity b("docB");
        b.setField("embedding", std::vector<float>{1.0f, 0.0f, 0.0f});
        b.setField("dataset", std::string("beta"));
        b.setField("score", std::string("15"));
        ASSERT_TRUE(vector_index_->addEntity(b, "embedding").ok);

        themis::BaseEntity c("docC");
        c.setField("embedding", std::vector<float>{0.0f, 1.0f, 0.0f});
        c.setField("dataset", std::string("alpha"));
        c.setField("score", std::string("25"));
        ASSERT_TRUE(vector_index_->addEntity(c, "embedding").ok);
    }

    json httpPost(const std::string& target, const json& body) {
        net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("127.0.0.1", std::to_string(18086));
        stream.connect(results);
        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "127.0.0.1"); req.set(http::field::content_type, "application/json");
        req.body() = body.dump(); req.prepare_payload();
        http::write(stream, req); beast::flat_buffer buffer; http::response<http::string_body> res; http::read(stream, buffer, res);
        beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return json::parse(res.body());
    }

    std::shared_ptr<themis::RocksDBWrapper> storage_; 
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_; 
    std::shared_ptr<themis::GraphIndexManager> graph_index_; 
    std::shared_ptr<themis::VectorIndexManager> vector_index_; 
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::unique_ptr<themis::server::HttpServer> server_;
};

TEST_F(HttpHybridSearchTest, TieBreakPkOrderingIdenticalScores) {
    json req = {
        {"collection", "hybrid_docs"},
        {"vector", json::array({1.0,0.0,0.0})},
        {"k", 5},
        {"tie_break", "pk"},
        {"filters", json::array()} // no filters
    };
    auto res = httpPost("/search/hybrid", req);
    ASSERT_TRUE(res.contains("results")) << res.dump();
    auto arr = res["results"].get<json::array_t>();
    // Expect docA then docB (alphabetical pk) ahead of docC due to worse distance
    ASSERT_GE(arr.size(), 2u);
    EXPECT_EQ(arr[0]["pk"], "docA");
    EXPECT_EQ(arr[1]["pk"], "docB");
}

TEST_F(HttpHybridSearchTest, INandRANGE_FilteringWorks) {
    // Filter: dataset IN ["alpha"] AND score RANGE {min:5, max:20}
    json filters = json::array({
        json{{"field","dataset"},{"op","IN"},{"value", json::array({"alpha"})}},
        json{{"field","score"},{"op","RANGE"},{"value", json{{"min",5},{"max",20}}}}
    });
    json req = {
        {"collection", "hybrid_docs"},
        {"vector", json::array({1.0,0.0,0.0})},
        {"k", 10},
        {"tie_break", "pk"},
        {"filters", filters}
    };
    auto res = httpPost("/search/hybrid", req);
    ASSERT_TRUE(res.contains("results")) << res.dump();
    auto arr = res["results"].get<json::array_t>();
    // Should include docA (alpha, score 10) exclude docC (alpha, score 25) exclude docB (beta)
    std::vector<std::string> pks; for (auto& v : arr) pks.push_back(v["pk"].get<std::string>());
    EXPECT_NE(std::find(pks.begin(), pks.end(), "docA"), pks.end());
    EXPECT_EQ(std::find(pks.begin(), pks.end(), "docC"), pks.end());
    EXPECT_EQ(std::find(pks.begin(), pks.end(), "docB"), pks.end());
}

