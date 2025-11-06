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

class HttpAqlFulltextScoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_aql_fulltext_score_test";
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
        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18084; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Setup fulltext index and data
        themis::SecondaryIndexManager::FulltextConfig ftcfg; ftcfg.stemming_enabled = true; ftcfg.language = "en"; ftcfg.stopwords_enabled = true;
        auto st = secondary_index_->createFulltextIndex("articles", "content", ftcfg);
        ASSERT_TRUE(st.ok) << st.message;
        // Insert docs
        auto e1 = themis::BaseEntity::fromFields("a1", themis::BaseEntity::FieldMap{{"title","AI on the moon"},{"content","ai moon base explores the moon with robots"}});
        auto e2 = themis::BaseEntity::fromFields("a2", themis::BaseEntity::FieldMap{{"title","AI in space"},{"content","ai rocket and space exploration"}});
        auto e3 = themis::BaseEntity::fromFields("a3", themis::BaseEntity::FieldMap{{"title","Pizza"},{"content","best pizza recipe with cheese"}});
        ASSERT_TRUE(secondary_index_->put("articles", e1).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e2).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e3).ok);
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
    }

    http::response<http::string_body> post(const std::string& target, const json& body) {
        try {
            net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
            auto const results = resolver.resolve("127.0.0.1", "18084");
            stream.connect(results);
            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::content_type, "application/json");
            req.body() = body.dump(); req.prepare_payload();
            http::write(stream, req);
            beast::flat_buffer buf; http::response<http::string_body> res; http::read(stream, buf, res);
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

TEST_F(HttpAqlFulltextScoreTest, FulltextScore_ReturnsPositiveScores) {
    // Basic fulltext with score in RETURN
    json req = {
        {"query", "FOR d IN articles FILTER FULLTEXT(d.content, \"ai\") RETURN {title: d.title, score: FULLTEXT_SCORE()}"}
    };
    auto res = post("/query/aql", req);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    ASSERT_TRUE(body.contains("entities"));
    auto arr = body["entities"].get<json::array_t>();
    ASSERT_GE(arr.size(), 2u); // a1 and a2 should match
    // Check all have numeric score > 0
    for (const auto& row : arr) {
        ASSERT_TRUE(row.is_object());
        ASSERT_TRUE(row.contains("score"));
        ASSERT_TRUE(row["score"].is_number());
        EXPECT_GT(row["score"].get<double>(), 0.0);
    }
}

TEST_F(HttpAqlFulltextScoreTest, FulltextScore_WithoutFulltext_YieldsError) {
    json req = {
        {"query", "FOR d IN articles RETURN {s: FULLTEXT_SCORE()}"}
    };
    auto res = post("/query/aql", req);
    EXPECT_EQ(res.result(), http::status::bad_request) << res.body();
}
