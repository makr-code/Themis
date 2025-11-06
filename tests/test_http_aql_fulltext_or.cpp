#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <filesystem>

#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "transaction/transaction_manager.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

class HttpAqlFulltextOrTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_aql_fulltext_or_test";
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
        themis::server::HttpServer::Config scfg; scfg.host = "127.0.0.1"; scfg.port = 18085; scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Create fulltext index
        themis::SecondaryIndexManager::FulltextConfig ftcfg; ftcfg.stemming_enabled = true; ftcfg.language = "en"; ftcfg.stopwords_enabled = true;
        auto st = secondary_index_->createFulltextIndex("articles", "content", ftcfg);
        ASSERT_TRUE(st.ok) << st.message;
        
        // Insert test documents
        auto e1 = themis::BaseEntity::fromFields("a1", themis::BaseEntity::FieldMap{{"title","AI Basics"},{"content","Artificial intelligence and machine learning"},{"year",2020}});
        auto e2 = themis::BaseEntity::fromFields("a2", themis::BaseEntity::FieldMap{{"title","Database Theory"},{"content","Relational databases and SQL fundamentals"},{"year",2018}});
        auto e3 = themis::BaseEntity::fromFields("a3", themis::BaseEntity::FieldMap{{"title","AI Applications"},{"content","Deep learning in artificial intelligence"},{"year",2022}});
        auto e4 = themis::BaseEntity::fromFields("a4", themis::BaseEntity::FieldMap{{"title","Old Document"},{"content","Historical records from ancient times"},{"year",1990}});
        auto e5 = themis::BaseEntity::fromFields("a5", themis::BaseEntity::FieldMap{{"title","Recent Update"},{"content","Latest news and current events"},{"year",2023}});
        
        ASSERT_TRUE(secondary_index_->put("articles", e1).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e2).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e3).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e4).ok);
        ASSERT_TRUE(secondary_index_->put("articles", e5).ok);
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
    }

    json executeAQL(const std::string& query) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);

            auto const results = resolver.resolve("127.0.0.1", "18085");
            stream.connect(results);

            json body = {{"query", query}};
            std::string body_str = body.dump();

            http::request<http::string_body> req{http::verb::post, "/api/aql", 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/json");
            req.body() = body_str;
            req.prepare_payload();

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);

            if (res.result() == http::status::ok) {
                return json::parse(res.body());
            } else {
                return {{"error", true}, {"status", static_cast<int>(res.result())}};
            }
        } catch (std::exception const& e) {
            return {{"error", true}, {"message", e.what()}};
        }
    }

    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::unique_ptr<themis::server::HttpServer> server_;
};

// Test 1: FULLTEXT OR structural condition (year)
TEST_F(HttpAqlFulltextOrTest, FulltextOr_Structural_ReturnsUnion) {
    std::string query = R"(
        FOR d IN articles
        FILTER FULLTEXT(d.content, "artificial intelligence") OR d.year < 2000
        RETURN {title: d.title, year: d.year}
    )";
    
    auto result = executeAQL(query);
    ASSERT_FALSE(result.contains("error"));
    ASSERT_TRUE(result.contains("result"));
    
    auto docs = result["result"];
    ASSERT_GE(docs.size(), 3);  // Should include: AI Basics, AI Applications, Old Document
    
    // Verify we have both fulltext matches and structural matches
    bool has_ai_doc = false;
    bool has_old_doc = false;
    
    for (const auto& doc : docs) {
        std::string title = doc["title"];
        if (title.find("AI") != std::string::npos) {
            has_ai_doc = true;
        }
        if (doc["year"] < 2000) {
            has_old_doc = true;
        }
    }
    
    EXPECT_TRUE(has_ai_doc);
    EXPECT_TRUE(has_old_doc);
}

// Test 2: FULLTEXT OR FULLTEXT (two different search terms)
TEST_F(HttpAqlFulltextOrTest, FulltextOr_TwoFulltext_ReturnsUnion) {
    std::string query = R"(
        FOR d IN articles
        FILTER FULLTEXT(d.content, "artificial intelligence") OR FULLTEXT(d.content, "database SQL")
        RETURN {title: d.title}
    )";
    
    auto result = executeAQL(query);
    ASSERT_FALSE(result.contains("error"));
    ASSERT_TRUE(result.contains("result"));
    
    auto docs = result["result"];
    ASSERT_GE(docs.size(), 3);  // AI Basics, AI Applications, Database Theory
    
    bool has_ai = false;
    bool has_db = false;
    
    for (const auto& doc : docs) {
        std::string title = doc["title"];
        if (title.find("AI") != std::string::npos) {
            has_ai = true;
        }
        if (title.find("Database") != std::string::npos) {
            has_db = true;
        }
    }
    
    EXPECT_TRUE(has_ai);
    EXPECT_TRUE(has_db);
}

// Test 3: Complex OR with AND inside - FULLTEXT AND structural OR other
TEST_F(HttpAqlFulltextOrTest, ComplexOr_FulltextAnd_Or_Structural) {
    std::string query = R"(
        FOR d IN articles
        FILTER (FULLTEXT(d.content, "artificial") AND d.year > 2020) OR d.year < 2000
        RETURN {title: d.title, year: d.year}
    )";
    
    auto result = executeAQL(query);
    ASSERT_FALSE(result.contains("error"));
    ASSERT_TRUE(result.contains("result"));
    
    auto docs = result["result"];
    ASSERT_GE(docs.size(), 2);  // AI Applications (2022, has "artificial"), Old Document (1990)
    
    bool has_recent_ai = false;
    bool has_old_doc = false;
    
    for (const auto& doc : docs) {
        std::string title = doc["title"];
        int year = doc["year"];
        
        if (title.find("AI Applications") != std::string::npos && year > 2020) {
            has_recent_ai = true;
        }
        if (year < 2000) {
            has_old_doc = true;
        }
    }
    
    EXPECT_TRUE(has_recent_ai);
    EXPECT_TRUE(has_old_doc);
}

// Test 4: OR with FULLTEXT and multiple structural conditions
TEST_F(HttpAqlFulltextOrTest, FulltextOr_MultipleStructural) {
    std::string query = R"(
        FOR d IN articles
        FILTER FULLTEXT(d.content, "database") OR d.year == 2023 OR d.year == 1990
        RETURN {title: d.title, year: d.year}
    )";
    
    auto result = executeAQL(query);
    ASSERT_FALSE(result.contains("error"));
    ASSERT_TRUE(result.contains("result"));
    
    auto docs = result["result"];
    ASSERT_GE(docs.size(), 3);  // Database Theory, Recent Update (2023), Old Document (1990)
    
    std::set<std::string> found_titles;
    for (const auto& doc : docs) {
        found_titles.insert(doc["title"]);
    }
    
    // Should include at least these
    bool has_matches = 
        found_titles.count("Database Theory") > 0 ||
        found_titles.count("Recent Update") > 0 ||
        found_titles.count("Old Document") > 0;
    
    EXPECT_TRUE(has_matches);
    EXPECT_GE(found_titles.size(), 3);
}

// Test 5: FULLTEXT with LIMIT inside OR expression
TEST_F(HttpAqlFulltextOrTest, FulltextWithLimit_InOr) {
    std::string query = R"(
        FOR d IN articles
        FILTER FULLTEXT(d.content, "artificial intelligence", 1) OR d.year > 2022
        RETURN {title: d.title}
    )";
    
    auto result = executeAQL(query);
    ASSERT_FALSE(result.contains("error"));
    ASSERT_TRUE(result.contains("result"));
    
    auto docs = result["result"];
    // Should have at least: 1 fulltext match (limit=1) + Recent Update (2023)
    ASSERT_GE(docs.size(), 1);
}

// Test 6: Nested OR with FULLTEXT
TEST_F(HttpAqlFulltextOrTest, NestedOr_WithFulltext) {
    std::string query = R"(
        FOR d IN articles
        FILTER (FULLTEXT(d.content, "artificial") OR d.year == 2018) OR d.year == 2023
        RETURN {title: d.title, year: d.year}
    )";
    
    auto result = executeAQL(query);
    ASSERT_FALSE(result.contains("error"));
    ASSERT_TRUE(result.contains("result"));
    
    auto docs = result["result"];
    // Should include: AI docs, Database Theory (2018), Recent Update (2023)
    ASSERT_GE(docs.size(), 4);
}
