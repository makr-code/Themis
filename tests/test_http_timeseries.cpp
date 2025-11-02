/**
 * @file test_http_timeseries.cpp
 * @brief HTTP tests for Time Series API endpoints
 */

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

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpTimeSeriesTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_ts_test";
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
        tx_manager_ = std::make_shared<themis::TransactionManager>(
            *storage_, *secondary_index_, *graph_index_, *vector_index_
        );
        
        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18086;  // Different port to avoid conflicts
        scfg.num_threads = 2;
    scfg.feature_timeseries = true;  // Enable time-series feature for tests
        
        server_ = std::make_unique<themis::server::HttpServer>(
            scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_
        );
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        storage_->close();
        
        const std::string db_path = "data/themis_http_ts_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
    }
    
    http::response<http::string_body> post(const std::string& target, const json& body) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            
            auto const results = resolver.resolve("127.0.0.1", "18086");
            stream.connect(results);
            
            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::content_type, "application/json");
            req.body() = body.dump();
            req.prepare_payload();
            
            http::write(stream, req);
            
            beast::flat_buffer buf;
            http::response<http::string_body> res;
            http::read(stream, buf, res);
            
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            
            return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "POST failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }
    
    http::response<http::string_body> get(const std::string& target) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            
            auto const results = resolver.resolve("127.0.0.1", "18086");
            stream.connect(results);
            
            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, "127.0.0.1");
            
            http::write(stream, req);
            
            beast::flat_buffer buf;
            http::response<http::string_body> res;
            http::read(stream, buf, res);
            
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            
            return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "GET failed: " << e.what();
            return http::response<http::string_body>{http::status::internal_server_error, 11};
        }
    }
    
    http::response<http::string_body> put(const std::string& target, const json& body) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            
            auto const results = resolver.resolve("127.0.0.1", "18086");
            stream.connect(results);
            
            http::request<http::string_body> req{http::verb::put, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::content_type, "application/json");
            req.body() = body.dump();
            req.prepare_payload();
            
            http::write(stream, req);
            
            beast::flat_buffer buf;
            http::response<http::string_body> res;
            http::read(stream, buf, res);
            
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            
            return res;
        } catch (const std::exception& e) {
            ADD_FAILURE() << "PUT failed: " << e.what();
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

// Test: Get default TS config
TEST_F(HttpTimeSeriesTest, GetTSConfig_ReturnsDefault) {
    auto res = get("/ts/config");
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    auto config = json::parse(res.body());
    ASSERT_TRUE(config.contains("compression"));
    ASSERT_TRUE(config.contains("chunk_size_hours"));
    
    // Default values (Gorilla is enabled by default)
    EXPECT_EQ(config["compression"], "gorilla");
    EXPECT_EQ(config["chunk_size_hours"], 24);
}

// Test: Update TS config to gorilla compression
TEST_F(HttpTimeSeriesTest, PutTSConfig_UpdateCompression) {
    json body = {
        {"compression", "gorilla"},
        {"chunk_size_hours", 12}
    };
    
    auto res = put("/ts/config", body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    // Response may vary - just check it's successful
    // auto response = json::parse(res.body());
    
    // Verify change persisted
    auto get_res = get("/ts/config");
    ASSERT_EQ(get_res.result(), http::status::ok);
    
    auto config = json::parse(get_res.body());
    EXPECT_EQ(config["compression"], "gorilla");
    EXPECT_EQ(config["chunk_size_hours"], 12);
}

// Test: Invalid compression type
TEST_F(HttpTimeSeriesTest, PutTSConfig_InvalidCompression) {
    json body = {
        {"compression", "invalid_type"},
        {"chunk_size_hours", 24}
    };
    
    auto res = put("/ts/config", body);
    EXPECT_EQ(res.result(), http::status::bad_request) << res.body();
}

// Test: Invalid chunk size (too small)
TEST_F(HttpTimeSeriesTest, PutTSConfig_ChunkSizeTooSmall) {
    json body = {
        {"compression", "none"},
        {"chunk_size_hours", 0}
    };
    
    auto res = put("/ts/config", body);
    EXPECT_EQ(res.result(), http::status::bad_request) << res.body();
}

// Test: Invalid chunk size (too large)
TEST_F(HttpTimeSeriesTest, PutTSConfig_ChunkSizeTooLarge) {
    json body = {
        {"compression", "none"},
        {"chunk_size_hours", 1000}
    };
    
    auto res = put("/ts/config", body);
    EXPECT_EQ(res.result(), http::status::bad_request) << res.body();
}

// Test: Put time series data
TEST_F(HttpTimeSeriesTest, PutTS_StoresMetric) {
    json body = {
        {"metric", "cpu.usage"},
        {"entity", "server1"},
        {"value", 75.5},
        {"timestamp_ms", 1730400000000},
        {"tags", {
            {"host", "server1"},
            {"region", "eu-west"}
        }}
    };
    
    auto res = post("/ts/put", body);
    ASSERT_EQ(res.result(), http::status::created) << res.body();
    
    auto response = json::parse(res.body());
    EXPECT_TRUE(response.contains("success"));
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_EQ(response["metric"], "cpu.usage");
    EXPECT_EQ(response["entity"], "server1");
}

// Test: Put multiple time series points
TEST_F(HttpTimeSeriesTest, PutTS_StoresMultiplePoints) {
    // Insert 3 data points
    for (int i = 0; i < 3; ++i) {
        json body = {
            {"metric", "memory.usage"},
            {"entity", "server1"},
            {"timestamp_ms", 1730400000000 + (int64_t)i * 60000},  // 1 minute apart
            {"value", 50.0 + i * 5.0},
            {"tags", {
                {"host", "server1"}
            }}
        };
        
        auto res = post("/ts/put", body);
        ASSERT_EQ(res.result(), http::status::created) << res.body();
    }
}

// Test: Query time series data
TEST_F(HttpTimeSeriesTest, QueryTS_RetrievesMetrics) {
    // First insert some data
    json put_body = {
        {"metric", "disk.usage"},
        {"entity", "server2"},
        {"timestamp_ms", 1730400000000},
        {"value", 80.0},
        {"tags", {
            {"host", "server2"}
        }}
    };
    
    auto put_res = post("/ts/put", put_body);
    ASSERT_EQ(put_res.result(), http::status::created);
    
    // Query the data
    json query_body = {
        {"metric", "disk.usage"},
        {"from_ms", 1730399000000},
        {"to_ms", 1730401000000},
        {"tags", {
            {"host", "server2"}
        }}
    };
    
    auto res = post("/ts/query", query_body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    auto response = json::parse(res.body());
    ASSERT_TRUE(response.contains("data"));
    EXPECT_FALSE(response["data"].empty());
}

// Test: Query with no matching data
TEST_F(HttpTimeSeriesTest, QueryTS_NoMatchingData) {
    json body = {
        {"metric", "nonexistent.metric"},
        {"from_ms", 1730400000000},
        {"to_ms", 1730401000000}
    };
    
    auto res = post("/ts/query", body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    auto response = json::parse(res.body());
    ASSERT_TRUE(response.contains("data"));
    EXPECT_TRUE(response["data"].empty());
}

// Test: Aggregate time series data
TEST_F(HttpTimeSeriesTest, AggregateTS_ComputesStats) {
    // Insert multiple points
    for (int i = 0; i < 5; ++i) {
        json body = {
            {"metric", "temperature"},
            {"entity", "room1"},
            {"timestamp_ms", 1730400000000 + (int64_t)i * 300000},  // 5 minutes apart
            {"value", 20.0 + i * 2.0},  // 20, 22, 24, 26, 28
            {"tags", {
                {"sensor", "room1"}
            }}
        };
        
        auto res = post("/ts/put", body);
        ASSERT_EQ(res.result(), http::status::created);
    }
    
    // Aggregate
    json agg_body = {
        {"metric", "temperature"},
        {"from_ms", 1730399000000},
        {"to_ms", 1730402000000},
        {"tags", {
            {"sensor", "room1"}
        }}
    };
    
    auto res = post("/ts/aggregate", agg_body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    auto response = json::parse(res.body());
    ASSERT_TRUE(response.contains("aggregation"));
    ASSERT_TRUE(response["aggregation"].contains("avg"));
    // Average of 20, 22, 24, 26, 28 = 24
    EXPECT_NEAR(response["aggregation"]["avg"].get<double>(), 24.0, 0.1);
}

// Test: Get continuous aggregates list
TEST_F(HttpTimeSeriesTest, GetAggregates_ReturnsList) {
    auto res = get("/ts/aggregates");
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    auto response = json::parse(res.body());
    ASSERT_TRUE(response.contains("aggregates"));
    EXPECT_TRUE(response["aggregates"].is_array());
}

// Test: Get retention policies
TEST_F(HttpTimeSeriesTest, GetRetention_ReturnsPolicies) {
    auto res = get("/ts/retention");
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    auto response = json::parse(res.body());
    ASSERT_TRUE(response.contains("policies"));
    EXPECT_TRUE(response["policies"].is_array());
}

// Test: Multiple metrics with label filtering
TEST_F(HttpTimeSeriesTest, QueryTS_LabelFiltering) {
    // Insert data for multiple hosts
    json body1 = {
        {"metric", "network.throughput"},
        {"entity", "web1"},
        {"timestamp_ms", 1730400000000},
        {"value", 100.0},
        {"tags", {
            {"host", "web1"}
        }}
    };
    
    json body2 = {
        {"metric", "network.throughput"},
        {"entity", "web2"},
        {"timestamp_ms", 1730400000000},
        {"value", 200.0},
        {"tags", {
            {"host", "web2"}
        }}
    };
    
    auto res1 = post("/ts/put", body1);
    auto res2 = post("/ts/put", body2);
    ASSERT_EQ(res1.result(), http::status::created);
    ASSERT_EQ(res2.result(), http::status::created);
    
    // Query only web1
    json query_body = {
        {"metric", "network.throughput"},
        {"from_ms", 1730399000000},
        {"to_ms", 1730401000000},
        {"tags", {
            {"host", "web1"}
        }}
    };
    
    auto res = post("/ts/query", query_body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    
    auto response = json::parse(res.body());
    ASSERT_TRUE(response.contains("data"));
    EXPECT_FALSE(response["data"].empty());
    // Verify we only got web1 data by entity filter
    for (const auto& point : response["data"]) {
        ASSERT_TRUE(point.contains("entity"));
        EXPECT_EQ(point["entity"], "web1");
    }
}

// Test: Config persistence across updates
TEST_F(HttpTimeSeriesTest, PutTSConfig_Persistence) {
    // Update to gorilla
    json body1 = {
        {"compression", "gorilla"},
        {"chunk_size_hours", 6}
    };
    auto res1 = put("/ts/config", body1);
    ASSERT_EQ(res1.result(), http::status::ok);
    
    // Update to none
    json body2 = {
        {"compression", "none"},
        {"chunk_size_hours", 48}
    };
    auto res2 = put("/ts/config", body2);
    ASSERT_EQ(res2.result(), http::status::ok);
    
    // Verify final state
    auto get_res = get("/ts/config");
    ASSERT_EQ(get_res.result(), http::status::ok);
    
    auto config = json::parse(get_res.body());
    EXPECT_EQ(config["compression"], "none");
    EXPECT_EQ(config["chunk_size_hours"], 48);
}
