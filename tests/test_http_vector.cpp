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

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpVectorApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create isolated test database
        const std::string db_path = "data/themis_http_vector_test";
        
        // Clean up old test data
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
        
        themis::RocksDBWrapper::Config cfg;
        cfg.db_path = db_path;
        cfg.memtable_size_mb = 64;
        cfg.block_cache_size_mb = 128;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());

        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);

        // Initialize vector index
        auto st = vector_index_->init("test_docs", 3, themis::VectorIndexManager::Metric::COSINE, 16, 200, 64);
        ASSERT_TRUE(st.ok) << st.message;

        // Start HTTP server
        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18085; // avoid clashes with other HTTP tests
        scfg.num_threads = 2;

        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        setupTestData();
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        
        // Cleanup test data
        std::filesystem::remove_all("data/themis_http_vector_test");
        std::filesystem::remove_all("./data/vector_http_test_save");
    }

    void setupTestData() {
        // Insert test vectors
        themis::BaseEntity e1("doc1");
        e1.setField("vec", std::vector<float>{1.0f, 0.0f, 0.0f});
        e1.setField("content", "first document");
        ASSERT_TRUE(vector_index_->addEntity(e1, "vec").ok);

        themis::BaseEntity e2("doc2");
        e2.setField("vec", std::vector<float>{0.0f, 1.0f, 0.0f});
        e2.setField("content", "second document");
        ASSERT_TRUE(vector_index_->addEntity(e2, "vec").ok);

        themis::BaseEntity e3("doc3");
        e3.setField("vec", std::vector<float>{0.0f, 0.0f, 1.0f});
        e3.setField("content", "third document");
        ASSERT_TRUE(vector_index_->addEntity(e3, "vec").ok);
    }

    json httpPost(const std::string& target, const json& body) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18085));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "127.0.0.1");
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

    json httpGet(const std::string& target) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18085));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "127.0.0.1");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return json::parse(res.body());
    }

    json httpPut(const std::string& target, const json& body) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18085));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::put, target, 11};
        req.set(http::field::host, "127.0.0.1");
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

    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::unique_ptr<themis::server::HttpServer> server_;
};

TEST_F(HttpVectorApiTest, VectorIndexStats_ReturnsConfiguration) {
    auto response = httpGet("/vector/index/stats");

    ASSERT_TRUE(response.contains("objectName"));
    EXPECT_EQ(response["objectName"], "test_docs");
    
    ASSERT_TRUE(response.contains("dimension"));
    EXPECT_EQ(response["dimension"], 3);
    
    ASSERT_TRUE(response.contains("metric"));
    EXPECT_EQ(response["metric"], "COSINE");
    
    ASSERT_TRUE(response.contains("vectorCount"));
    EXPECT_EQ(response["vectorCount"], 3); // 3 docs inserted in setup
    
    ASSERT_TRUE(response.contains("M"));
    EXPECT_EQ(response["M"], 16);
    
    ASSERT_TRUE(response.contains("efConstruction"));
    EXPECT_EQ(response["efConstruction"], 200);
    
    ASSERT_TRUE(response.contains("efSearch"));
    EXPECT_EQ(response["efSearch"], 64);
}

TEST_F(HttpVectorApiTest, VectorIndexConfigGet_ReturnsCurrentConfig) {
    auto response = httpGet("/vector/index/config");

    ASSERT_TRUE(response.contains("objectName"));
    EXPECT_EQ(response["objectName"], "test_docs");
    
    ASSERT_TRUE(response.contains("efSearch"));
    EXPECT_EQ(response["efSearch"], 64);
    
    ASSERT_TRUE(response.contains("M"));
    ASSERT_TRUE(response.contains("efConstruction"));
    ASSERT_TRUE(response.contains("hnswEnabled"));
}

TEST_F(HttpVectorApiTest, VectorIndexConfigPut_UpdatesEfSearch) {
    // Update efSearch to 100
    json request = {{"efSearch", 100}};
    auto response = httpPut("/vector/index/config", request);

    ASSERT_TRUE(response.contains("message"));
    EXPECT_EQ(response["message"], "Vector index configuration updated");
    
    // Verify it was updated
    auto config = httpGet("/vector/index/config");
    EXPECT_EQ(config["efSearch"], 100);
}

TEST_F(HttpVectorApiTest, VectorIndexConfigPut_RejectsInvalidEfSearch) {
    // Try invalid efSearch (too large)
    json request = {{"efSearch", 50000}};
    auto response = httpPut("/vector/index/config", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    EXPECT_NE(response["message"].get<std::string>().find("efSearch must be between"), std::string::npos);
}

TEST_F(HttpVectorApiTest, VectorIndexSave_CreatesFiles) {
    std::string save_dir = "./data/vector_http_test_save";
    
    // Clean up if exists
    if (std::filesystem::exists(save_dir)) {
        std::filesystem::remove_all(save_dir);
    }
    
    json request = {{"directory", save_dir}};
    auto response = httpPost("/vector/index/save", request);

    ASSERT_TRUE(response.contains("message"));
    EXPECT_EQ(response["message"], "Vector index saved successfully");
    ASSERT_TRUE(response.contains("directory"));
    EXPECT_EQ(response["directory"], save_dir);
    
    // Verify files were created
    EXPECT_TRUE(std::filesystem::exists(save_dir + "/meta.txt"));
    EXPECT_TRUE(std::filesystem::exists(save_dir + "/labels.txt"));
    EXPECT_TRUE(std::filesystem::exists(save_dir + "/index.bin"));
}

TEST_F(HttpVectorApiTest, VectorIndexLoad_RestoresFromDisk) {
    std::string save_dir = "./data/vector_http_test_save";
    
    // First save the index
    if (std::filesystem::exists(save_dir)) {
        std::filesystem::remove_all(save_dir);
    }
    
    json save_request = {{"directory", save_dir}};
    auto save_response = httpPost("/vector/index/save", save_request);
    ASSERT_EQ(save_response["message"], "Vector index saved successfully");
    
    // Now load it (in real scenario this would be after server restart)
    json load_request = {{"directory", save_dir}};
    auto load_response = httpPost("/vector/index/load", load_request);

    ASSERT_TRUE(load_response.contains("message"));
    EXPECT_EQ(load_response["message"], "Vector index loaded successfully");
    ASSERT_TRUE(load_response.contains("directory"));
    EXPECT_EQ(load_response["directory"], save_dir);
    
    // Verify config is still correct after load
    auto config = httpGet("/vector/index/config");
    EXPECT_EQ(config["objectName"], "test_docs");
    EXPECT_EQ(config["dimension"], 3);
    EXPECT_EQ(config["metric"], "COSINE");
}

TEST_F(HttpVectorApiTest, VectorIndexLoad_FailsOnInvalidDirectory) {
    json request = {{"directory", "./nonexistent_dir_12345"}};
    auto response = httpPost("/vector/index/load", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    EXPECT_NE(response["message"].get<std::string>().find("Failed to load index"), std::string::npos);
}

TEST_F(HttpVectorApiTest, VectorIndexLoad_RequiresDirectory) {
    json request = {}; // Missing directory parameter
    auto response = httpPost("/vector/index/load", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    EXPECT_NE(response["message"].get<std::string>().find("Missing required field: directory"), std::string::npos);
}

TEST_F(HttpVectorApiTest, VectorSearch_FindsNearestNeighbors) {
    // Search for vectors near [1, 0, 0] - should find doc1 first
    json request = {
        {"vector", {1.0f, 0.0f, 0.0f}},
        {"k", 2}
    };
    auto response = httpPost("/vector/search", request);

    ASSERT_TRUE(response.contains("results")) << "Response: " << response.dump();
    ASSERT_TRUE(response.contains("count"));
    ASSERT_TRUE(response.contains("k"));
    EXPECT_EQ(response["k"], 2);
    
    auto results = response["results"];
    ASSERT_TRUE(results.is_array());
    EXPECT_GE(results.size(), 1); // At least one result
    EXPECT_LE(results.size(), 2); // At most k results
    
    // First result should be doc1 with smallest distance
    EXPECT_EQ(results[0]["pk"], "doc1");
    EXPECT_LT(results[0]["distance"].get<float>(), 0.1f); // Very close (cosine distance)
}

TEST_F(HttpVectorApiTest, VectorSearch_RespectsKParameter) {
    // Search with k=1
    json request = {
        {"vector", {0.5f, 0.5f, 0.0f}},
        {"k", 1}
    };
    auto response = httpPost("/vector/search", request);

    ASSERT_TRUE(response.contains("results"));
    EXPECT_EQ(response["count"], 1);
    
    auto results = response["results"];
    ASSERT_EQ(results.size(), 1);
}

TEST_F(HttpVectorApiTest, VectorSearch_DefaultsK) {
    // Search without k (should default to 10)
    json request = {
        {"vector", {0.0f, 0.0f, 1.0f}}
    };
    auto response = httpPost("/vector/search", request);

    ASSERT_TRUE(response.contains("k"));
    EXPECT_EQ(response["k"], 10); // Default value
    
    // Should return all 3 vectors since we only have 3 in total
    EXPECT_EQ(response["count"], 3);
}

TEST_F(HttpVectorApiTest, VectorSearch_ValidatesDimension) {
    // Wrong dimension (2D instead of 3D)
    json request = {
        {"vector", {1.0f, 0.0f}},
        {"k", 1}
    };
    auto response = httpPost("/vector/search", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    EXPECT_NE(response["message"].get<std::string>().find("dimension mismatch"), std::string::npos);
}

TEST_F(HttpVectorApiTest, VectorSearch_RequiresVectorField) {
    json request = {
        {"k", 5}
    };
    auto response = httpPost("/vector/search", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    EXPECT_NE(response["message"].get<std::string>().find("Missing required field: vector"), std::string::npos);
}

TEST_F(HttpVectorApiTest, VectorSearch_RejectsInvalidK) {
    json request = {
        {"vector", {1.0f, 0.0f, 0.0f}},
        {"k", 0}
    };
    auto response = httpPost("/vector/search", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    EXPECT_NE(response["message"].get<std::string>().find("k' must be greater than 0"), std::string::npos);
}
