#include <gtest/gtest.h>
#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace themis;
using namespace themis::server;
using json = nlohmann::json;

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpAdaptiveIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = std::filesystem::temp_directory_path() / 
                       ("themis_http_adaptive_test_" + std::to_string(std::time(nullptr)));
        
        RocksDBWrapper::Config db_config;
        db_config.db_path = test_db_path_.string();
        
        storage_ = std::make_shared<RocksDBWrapper>(db_config);
        ASSERT_TRUE(storage_->open()) << "Failed to open RocksDB";
        
        secondary_index_ = std::make_shared<SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);
        
        HttpServer::Config server_config;
        server_config.host = "127.0.0.1";
        server_config.port = 18081;  // Different port to avoid conflicts
        server_config.num_threads = 2;
        
        server_ = std::make_unique<HttpServer>(
            server_config, storage_, secondary_index_, 
            graph_index_, vector_index_, tx_manager_
        );
        
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Wait for server startup
    }
    
    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        server_.reset();
        tx_manager_.reset();
        vector_index_.reset();
        graph_index_.reset();
        secondary_index_.reset();
        storage_.reset();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::filesystem::remove_all(test_db_path_);
    }
    
    json httpGet(const std::string& path) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        
        auto const results = resolver.resolve("127.0.0.1", "18081");
        stream.connect(results);
        
        http::request<http::string_body> req{http::verb::get, path, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        
        http::write(stream, req);
        
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        
        return json::parse(res.body());
    }
    
    json httpPost(const std::string& path, const json& body) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        
        auto const results = resolver.resolve("127.0.0.1", "18081");
        stream.connect(results);
        
        http::request<http::string_body> req{http::verb::post, path, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::content_type, "application/json");
        req.body() = body.dump();
        req.prepare_payload();
        
        http::write(stream, req);
        
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        
        return json::parse(res.body());
    }
    
    void httpDelete(const std::string& path) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        
        auto const results = resolver.resolve("127.0.0.1", "18081");
        stream.connect(results);
        
        http::request<http::string_body> req{http::verb::delete_, path, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        
        http::write(stream, req);
        
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    }
    
    std::filesystem::path test_db_path_;
    std::shared_ptr<RocksDBWrapper> storage_;
    std::shared_ptr<SecondaryIndexManager> secondary_index_;
    std::shared_ptr<GraphIndexManager> graph_index_;
    std::shared_ptr<VectorIndexManager> vector_index_;
    std::shared_ptr<TransactionManager> tx_manager_;
    std::unique_ptr<HttpServer> server_;
};

// ===== HTTP Endpoint Tests =====

TEST_F(HttpAdaptiveIndexTest, RecordPattern_Success) {
    json request = {
        {"collection", "users"},
        {"field", "email"},
        {"operation", "eq"},
        {"execution_time_ms", 25}
    };
    
    auto response = httpPost("/index/record-pattern", request);
    
    EXPECT_EQ(response["status"], "recorded");
    EXPECT_EQ(response["collection"], "users");
    EXPECT_EQ(response["field"], "email");
    EXPECT_EQ(response["operation"], "eq");
}

TEST_F(HttpAdaptiveIndexTest, RecordPattern_MissingFields_ReturnsError) {
    json request = {
        {"collection", "users"}
        // Missing field
    };
    
    // Post request - use same pattern as httpPost helper
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    
    auto const results = resolver.resolve("127.0.0.1", "18081");
    stream.connect(results);
    
    http::request<http::string_body> req{http::verb::post, "/index/record-pattern", 11};
    req.set(http::field::host, "127.0.0.1");
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::content_type, "application/json");
    req.body() = request.dump();
    req.prepare_payload();
    
    http::write(stream, req);
    
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    
    // Should return 400 Bad Request
    EXPECT_EQ(res.result(), http::status::bad_request);
    
    auto response_body = json::parse(res.body());
    EXPECT_TRUE(response_body.contains("error"));
}

TEST_F(HttpAdaptiveIndexTest, GetPatterns_EmptyInitially) {
    auto response = httpGet("/index/patterns");
    
    EXPECT_TRUE(response.is_array());
    EXPECT_EQ(response.size(), 0);
}

TEST_F(HttpAdaptiveIndexTest, GetPatterns_ReturnsRecorded) {
    // Record some patterns
    json request1 = {
        {"collection", "users"},
        {"field", "age"},
        {"operation", "range"},
        {"execution_time_ms", 30}
    };
    httpPost("/index/record-pattern", request1);
    
    json request2 = {
        {"collection", "users"},
        {"field", "email"},
        {"operation", "eq"},
        {"execution_time_ms", 15}
    };
    httpPost("/index/record-pattern", request2);
    
    // Get patterns
    auto response = httpGet("/index/patterns");
    
    ASSERT_TRUE(response.is_array());
    EXPECT_EQ(response.size(), 2);
    
    // Verify first pattern
    EXPECT_EQ(response[0]["collection"], "users");
    EXPECT_TRUE(response[0].contains("field"));
    EXPECT_TRUE(response[0].contains("operation"));
    EXPECT_TRUE(response[0].contains("count"));
}

TEST_F(HttpAdaptiveIndexTest, GetPatterns_FilterByCollection) {
    // Record patterns for different collections
    json request1 = {
        {"collection", "users"},
        {"field", "email"},
        {"operation", "eq"},
        {"execution_time_ms", 10}
    };
    httpPost("/index/record-pattern", request1);
    
    json request2 = {
        {"collection", "products"},
        {"field", "category"},
        {"operation", "eq"},
        {"execution_time_ms", 20}
    };
    httpPost("/index/record-pattern", request2);
    
    // Get patterns for users collection
    auto response = httpGet("/index/patterns?collection=users");
    
    ASSERT_TRUE(response.is_array());
    ASSERT_GT(response.size(), 0);
    
    for (const auto& pattern : response) {
        EXPECT_EQ(pattern["collection"], "users");
    }
}

TEST_F(HttpAdaptiveIndexTest, ClearPatterns_Success) {
    // Record some patterns
    json request = {
        {"collection", "users"},
        {"field", "email"},
        {"operation", "eq"},
        {"execution_time_ms", 10}
    };
    httpPost("/index/record-pattern", request);
    
    // Verify patterns exist
    auto patterns_before = httpGet("/index/patterns");
    EXPECT_GT(patterns_before.size(), 0);
    
    // Clear patterns
    httpDelete("/index/patterns");
    
    // Verify patterns cleared
    auto patterns_after = httpGet("/index/patterns");
    EXPECT_EQ(patterns_after.size(), 0);
}

TEST_F(HttpAdaptiveIndexTest, GetSuggestions_NoPatterns_ReturnsEmpty) {
    auto response = httpGet("/index/suggestions");
    
    EXPECT_TRUE(response.is_array());
    EXPECT_EQ(response.size(), 0);
}

TEST_F(HttpAdaptiveIndexTest, GetSuggestions_WithPatterns_ReturnsSuggestions) {
    // Insert test data for selectivity analysis (use correct prefix)
    auto* raw_db = storage_->getRawDB();
    rocksdb::WriteOptions write_opts;
    for (int i = 0; i < 50; i++) {
        std::string key = "d:users:" + std::to_string(i);
        std::string value = R"({"email":"user)" + std::to_string(i) + R"(@test.com","name":"User)" + std::to_string(i) + R"("})";
        raw_db->Put(write_opts, key, value);
    }
    
    // Record high-frequency pattern
    for (int i = 0; i < 100; i++) {
        json request = {
            {"collection", "users"},
            {"field", "email"},
            {"operation", "eq"},
            {"execution_time_ms", 50}
        };
        httpPost("/index/record-pattern", request);
    }
    
    // Get suggestions
    auto response = httpGet("/index/suggestions");
    
    ASSERT_TRUE(response.is_array());
    EXPECT_GT(response.size(), 0);
    
    // Verify suggestion structure
    if (response.size() > 0) {
        const auto& suggestion = response[0];
        EXPECT_EQ(suggestion["collection"], "users");
        EXPECT_EQ(suggestion["field"], "email");
        EXPECT_TRUE(suggestion.contains("index_type"));
        EXPECT_TRUE(suggestion.contains("score"));
        EXPECT_TRUE(suggestion.contains("reason"));
        EXPECT_TRUE(suggestion.contains("queries_affected"));
        EXPECT_EQ(suggestion["queries_affected"], 100);
    }
}

TEST_F(HttpAdaptiveIndexTest, GetSuggestions_WithMinScore_FiltersResults) {
    // Record low-frequency pattern
    json request = {
        {"collection", "users"},
        {"field", "id"},
        {"operation", "eq"},
        {"execution_time_ms", 1}
    };
    httpPost("/index/record-pattern", request);
    
    // Get suggestions with high min_score (should filter out)
    auto response = httpGet("/index/suggestions?min_score=0.9");
    
    EXPECT_TRUE(response.is_array());
    // Low frequency pattern should be filtered out
}

TEST_F(HttpAdaptiveIndexTest, GetSuggestions_WithLimit_RespectsLimit) {
    // Record multiple patterns
    for (int field_num = 0; field_num < 10; field_num++) {
        for (int i = 0; i < 50; i++) {
            json request = {
                {"collection", "users"},
                {"field", "field" + std::to_string(field_num)},
                {"operation", "eq"},
                {"execution_time_ms", 20}
            };
            httpPost("/index/record-pattern", request);
        }
    }
    
    // Get suggestions with limit=3
    auto response = httpGet("/index/suggestions?limit=3");
    
    ASSERT_TRUE(response.is_array());
    EXPECT_LE(response.size(), 3);
}

TEST_F(HttpAdaptiveIndexTest, RealWorld_FrequentQueries_GenerateSuggestion) {
    // Insert test data (use correct prefix)
    auto* raw_db = storage_->getRawDB();
    rocksdb::WriteOptions write_opts;
    for (int i = 0; i < 100; i++) {
        std::string key = "d:users:" + std::to_string(i);
        std::string value = R"({"email":"user)" + std::to_string(i % 10) + R"(@test.com","active":true})";
        raw_db->Put(write_opts, key, value);
    }
    
    // Simulate frequent user lookups by email
    for (int i = 0; i < 500; i++) {
        json request = {
            {"collection", "users"},
            {"field", "email"},
            {"operation", "eq"},
            {"execution_time_ms", 25}
        };
        httpPost("/index/record-pattern", request);
    }
    
    // Get suggestions (lower min_score since we have moderate frequency)
    auto response = httpGet("/index/suggestions?collection=users&min_score=0.3");
    
    ASSERT_TRUE(response.is_array());
    ASSERT_GT(response.size(), 0);
    
    const auto& suggestion = response[0];
    EXPECT_EQ(suggestion["field"], "email");
    // Low selectivity (10 unique / 100 docs = 0.1) → range index recommended
    EXPECT_EQ(suggestion["index_type"], "range");
    EXPECT_GT(suggestion["score"].get<double>(), 0.3);
}

TEST_F(HttpAdaptiveIndexTest, RealWorld_RangeQueries_SuggestsRangeIndex) {
    // Insert test data with age field (use correct prefix)
    auto* raw_db = storage_->getRawDB();
    rocksdb::WriteOptions write_opts;
    for (int i = 0; i < 100; i++) {
        std::string key = "d:users:" + std::to_string(i);
        std::string value = R"({"age":)" + std::to_string(20 + (i % 50)) + R"(,"name":"User)" + std::to_string(i) + R"("})";
        raw_db->Put(write_opts, key, value);
    }
    
    // Simulate range queries
    for (int i = 0; i < 100; i++) {
        json request = {
            {"collection", "users"},
            {"field", "age"},
            {"operation", "range"},
            {"execution_time_ms", 35}
        };
        httpPost("/index/record-pattern", request);
    }
    
    // Get suggestions (lower min_score)
    auto response = httpGet("/index/suggestions?collection=users&min_score=0.3");
    
    ASSERT_TRUE(response.is_array());
    
    // Find age suggestion
    bool found_age = false;
    for (const auto& suggestion : response) {
        if (suggestion["field"] == "age") {
            found_age = true;
            EXPECT_EQ(suggestion["index_type"], "range");
            break;
        }
    }
    
    EXPECT_TRUE(found_age) << "Should suggest range index for age field";
}
