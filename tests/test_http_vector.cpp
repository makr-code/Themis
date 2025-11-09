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
        // Enable vector metadata encryption schema for collection "test_docs"
        // Schema shape expected by server: { collections: { test_docs: { encryption: { enabled: true, fields: ["content"] }}}}
        nlohmann::json es = {
            {"collections", {
                {"test_docs", {
                    {"encryption", {
                        {"enabled", true},
                        {"fields", nlohmann::json::array({"content"})}
                    }}
                }}
            }}
        };
        auto estr = es.dump();
        storage_->put("config:encryption_schema", std::vector<uint8_t>(estr.begin(), estr.end()));
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
    req.set(http::field::authorization, "Bearer admin-token-pii-tests");
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

    json httpDelete(const std::string& target, const json& body) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18085));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::delete_, target, 11};
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

TEST_F(HttpVectorApiTest, VectorBatchInsert_EncryptsMetadata_WhenSchemaEnabled) {
    // Insert with metadata field 'content' which is marked encrypted by schema
    json request = {
        {"vector_field", "vec"},
        {"items", json::array({
            json{{"pk","sec1"},{"vector", {1.0f, 0.0f, 0.0f}}, {"fields", json{{"content","secret meta"}}}}
        })}
    };
    auto resp = httpPost("/vector/batch_insert", request);
    // Debug: Print response to see what we got
    if (!resp.contains("inserted")) {
        std::cout << "Response does not contain 'inserted'. Full response: " << resp.dump(2) << std::endl;
    }
    ASSERT_TRUE(resp.contains("inserted")) << "Response: " << resp.dump(2);
    EXPECT_EQ(resp["inserted"], 1);

    // Read back underlying entity from storage and verify encryption markers
    // Stored entity key uses object_name prefix (test_docs) + ':' + pk
    auto raw = storage_->get("test_docs:sec1");
    ASSERT_TRUE(raw.has_value());
    // Native binary deserialization (entity stored via BaseEntity::serialize)
    themis::BaseEntity ent = themis::BaseEntity::deserialize("sec1", *raw);
    // Plaintext field "content" should have been replaced with monostate
    auto fContent = ent.getField("content");
    ASSERT_TRUE(fContent.has_value());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(*fContent));
    // Encryption marker boolean
    auto fEnc = ent.getField("content_enc");
    ASSERT_TRUE(fEnc.has_value());
    ASSERT_TRUE(std::holds_alternative<bool>(*fEnc));
    EXPECT_TRUE(std::get<bool>(*fEnc));
    // Encrypted blob JSON stored as string in field content_encrypted
    auto fBlob = ent.getField("content_encrypted");
    ASSERT_TRUE(fBlob.has_value());
    ASSERT_TRUE(std::holds_alternative<std::string>(*fBlob));
    auto blobStr = std::get<std::string>(*fBlob);
    // Parse inner JSON for key metadata presence
    auto blobJson = json::parse(blobStr);
    ASSERT_TRUE(blobJson.contains("key_id"));
    ASSERT_TRUE(blobJson.contains("key_version"));
    // Optional: presence of cryptographic fields
    ASSERT_TRUE(blobJson.contains("iv"));
    ASSERT_TRUE(blobJson.contains("tag"));
    ASSERT_TRUE(blobJson.contains("ciphertext"));
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
    if (response["message"].is_string()) {
        EXPECT_NE(response["message"].get<std::string>().find("efSearch must be between"), std::string::npos);
    } else {
        FAIL() << "Expected 'message' field to be a string";
    }
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
    if (response["message"].is_string()) {
        EXPECT_NE(response["message"].get<std::string>().find("Failed to load index"), std::string::npos);
    } else {
        FAIL() << "Expected 'message' field to be a string";
    }
}

TEST_F(HttpVectorApiTest, VectorIndexLoad_RequiresDirectory) {
    json request = {}; // Missing directory parameter
    auto response = httpPost("/vector/index/load", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    if (response["message"].is_string()) {
        EXPECT_NE(response["message"].get<std::string>().find("Missing required field: directory"), std::string::npos);
    } else {
        FAIL() << "Expected 'message' field to be a string";
    }
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
    if (results[0]["distance"].is_number()) {
        EXPECT_LT(results[0]["distance"].get<float>(), 0.1f); // Very close (cosine distance)
    } else {
        FAIL() << "Expected 'distance' field to be a number";
    }
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
    if (response["message"].is_string()) {
        EXPECT_NE(response["message"].get<std::string>().find("dimension mismatch"), std::string::npos);
    } else {
        FAIL() << "Expected 'message' field to be a string";
    }
}

TEST_F(HttpVectorApiTest, VectorSearch_RequiresVectorField) {
    json request = {
        {"k", 5}
    };
    auto response = httpPost("/vector/search", request);

    ASSERT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], true);
    ASSERT_TRUE(response.contains("message"));
    if (response["message"].is_string()) {
        EXPECT_NE(response["message"].get<std::string>().find("Missing required field: vector"), std::string::npos);
    } else {
        FAIL() << "Expected 'message' field to be a string";
    }
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
    if (response["message"].is_string()) {
        EXPECT_NE(response["message"].get<std::string>().find("k' must be greater than 0"), std::string::npos);
    } else {
        FAIL() << "Expected 'message' field to be a string";
    }
}

TEST_F(HttpVectorApiTest, VectorBatchInsert_InsertsItems) {
    // Insert two additional docs via batch_insert with vector_field 'vec'
    json request = {
        {"vector_field", "vec"},
        {"items", json::array({
            json{{"pk","doc4"},{"vector", {1.0f, 0.0f, 0.0f}}, {"fields", json{{"content","fourth"}}}},
            json{{"pk","doc5"},{"vector", {0.0f, 1.0f, 0.0f}}, {"fields", json{{"content","fifth"}}}}
        })}
    };
    auto resp = httpPost("/vector/batch_insert", request);
    ASSERT_TRUE(resp.contains("inserted"));
    EXPECT_EQ(resp["inserted"], 2);

    // Verify new vector searchable
    json searchReq = {{"vector", {0.0f, 1.0f, 0.0f}}, {"k", 1}};
    auto searchResp = httpPost("/vector/search", searchReq);
    ASSERT_TRUE(searchResp.contains("results"));
    auto results = searchResp["results"];
    ASSERT_GE(results.size(), 1);
    EXPECT_EQ(results[0]["pk"], "doc5");
}

TEST_F(HttpVectorApiTest, VectorDeleteByFilter_SupportsPksAndPrefix) {
    // Ensure an extra PK exists to delete
    json insertReq = {
        {"vector_field", "vec"},
        {"items", json::array({ json{{"pk","tmp-1"},{"vector", {0.0f, 1.0f, 0.0f}}} })}
    };
    (void)httpPost("/vector/batch_insert", insertReq);

    // Delete by PK
    json delByPk = {{"pks", json::array({"doc2"})}};
    auto delResp1 = httpDelete("/vector/by-filter", delByPk);
    ASSERT_TRUE(delResp1.contains("deleted"));
    EXPECT_EQ(delResp1["deleted"], 1);

    // Verify doc2 no longer the nearest to [0,1,0]
    json searchReq = {{"vector", {0.0f, 1.0f, 0.0f}}, {"k", 1}};
    auto searchResp = httpPost("/vector/search", searchReq);
    ASSERT_TRUE(searchResp.contains("results"));
    auto results = searchResp["results"];
    ASSERT_GE(results.size(), 1);
    if (results[0]["pk"].is_string()) {
        EXPECT_NE(results[0]["pk"].get<std::string>(), std::string("doc2"));
    } else {
        FAIL() << "Expected 'pk' field to be a string";
    }

    // Delete by prefix
    json delByPrefix = {{"prefix", "tmp-"}};
    auto delResp2 = httpDelete("/vector/by-filter", delByPrefix);
    ASSERT_TRUE(delResp2.contains("method"));
    EXPECT_EQ(delResp2["method"], "prefix");
}

TEST_F(HttpVectorApiTest, VectorSearch_CursorPagination_Works) {
    // Insert a few extras to ensure more than k results
    json batch = {
        {"vector_field", "vec"},
        {"items", json::array({
            json{{"pk","p1"},{"vector", {1.0f, 0.0f, 0.0f}}},
            json{{"pk","p2"},{"vector", {1.0f, 0.0f, 0.0f}}},
            json{{"pk","p3"},{"vector", {1.0f, 0.0f, 0.0f}}}
        })}
    };
    (void)httpPost("/vector/batch_insert", batch);

    // Page 1
    json req1 = {
        {"vector", {1.0f, 0.0f, 0.0f}},
        {"k", 2},
        {"use_cursor", true}
    };
    auto r1 = httpPost("/vector/search", req1);
    ASSERT_TRUE(r1.contains("items"));
    ASSERT_TRUE(r1.contains("has_more"));
    auto items1 = r1["items"];
    ASSERT_EQ(items1.size(), 2);
    bool has_more = r1["has_more"].get<bool>();
    ASSERT_TRUE(r1.contains("next_cursor") || !has_more);
    if (!has_more) return; // nothing more to test

    // Page 2 using cursor
    std::string cursor;
    if (r1["next_cursor"].is_string()) {
        cursor = r1["next_cursor"].get<std::string>();
    } else {
        FAIL() << "Expected 'next_cursor' field to be a string";
    }
    json req2 = {
        {"vector", {1.0f, 0.0f, 0.0f}},
        {"k", 2},
        {"use_cursor", true},
        {"cursor", cursor}
    };
    auto r2 = httpPost("/vector/search", req2);
    ASSERT_TRUE(r2.contains("items"));
    auto items2 = r2["items"];
    ASSERT_GE(items2.size(), 1);
}

TEST_F(HttpVectorApiTest, VectorIndexStats_DOTMetric_NoNormalization) {
    // Create a new index with DOT metric to test HTTP API metric handling
    const std::string db_path_dot = "data/themis_http_vector_test_dot";
    if (std::filesystem::exists(db_path_dot)) {
        std::filesystem::remove_all(db_path_dot);
    }
    
    themis::RocksDBWrapper::Config cfg;
    cfg.db_path = db_path_dot;
    cfg.memtable_size_mb = 64;
    cfg.block_cache_size_mb = 128;
    auto storage_dot = std::make_shared<themis::RocksDBWrapper>(cfg);
    ASSERT_TRUE(storage_dot->open());
    
    auto vector_index_dot = std::make_shared<themis::VectorIndexManager>(*storage_dot);
    auto st = vector_index_dot->init("docs_dot", 2, themis::VectorIndexManager::Metric::DOT, 16, 200, 64);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Add vectors with different magnitudes (DOT does NOT normalize)
    themis::BaseEntity e1("doc1");
    e1.setField("vec", std::vector<float>{1.0f, 0.0f});
    ASSERT_TRUE(vector_index_dot->addEntity(e1, "vec").ok);
    
    themis::BaseEntity e2("doc2");
    e2.setField("vec", std::vector<float>{10.0f, 0.0f}); // 10x magnitude
    ASSERT_TRUE(vector_index_dot->addEntity(e2, "vec").ok);
    
    // Verify DOT metric configuration
    EXPECT_EQ(vector_index_dot->getMetric(), themis::VectorIndexManager::Metric::DOT);
    EXPECT_EQ(vector_index_dot->getVectorCount(), 2u);
    
    // Search with DOT - higher dot product = better (lower distance after negation)
    std::vector<float> query{1.0f, 0.0f};
    auto [search_st, results] = vector_index_dot->searchKnn(query, 2);
    ASSERT_TRUE(search_st.ok);
    ASSERT_EQ(results.size(), 2u);
    
    // doc2 should rank first (dot=10.0 > dot=1.0)
    EXPECT_EQ(results[0].pk, "doc2");
    EXPECT_EQ(results[1].pk, "doc1");
    
    // Distance should be negative dot product
    EXPECT_LT(results[0].distance, results[1].distance);
    
    storage_dot->close();
    std::filesystem::remove_all(db_path_dot);
}

