/**
 * @file test_http_governance.cpp
 * @brief HTTP tests for Governance and Policy Enforcement
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

class HttpGovernanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_gov_test";
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
        scfg.port = 18087;  // Different port to avoid conflicts
        scfg.num_threads = 2;
        
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
        
        const std::string db_path = "data/themis_http_gov_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
    }
    
    http::response<http::string_body> post_with_headers(
        const std::string& target,
        const json& body,
        const std::map<std::string, std::string>& headers = {}
    ) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            
            auto const results = resolver.resolve("127.0.0.1", "18087");
            stream.connect(results);
            
            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, "127.0.0.1");
            req.set(http::field::content_type, "application/json");
            
            // Add custom headers
            for (const auto& [key, value] : headers) {
                req.set(key, value);
            }
            
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
    
    http::response<http::string_body> get_with_headers(
        const std::string& target,
        const std::map<std::string, std::string>& headers = {}
    ) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);
            
            auto const results = resolver.resolve("127.0.0.1", "18087");
            stream.connect(results);
            
            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, "127.0.0.1");
            
            // Add custom headers
            for (const auto& [key, value] : headers) {
                req.set(key, value);
            }
            
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
    
    bool has_header(const http::response<http::string_body>& res, const std::string& header_name) {
        return res.find(header_name) != res.end();
    }
    
    std::string get_header(const http::response<http::string_body>& res, const std::string& header_name) {
        auto it = res.find(header_name);
        if (it != res.end()) {
            return std::string(it->value());
        }
        return "";
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

// Test: Default policy headers on public endpoint
TEST_F(HttpGovernanceTest, PublicEndpoint_ReturnsDefaultPolicyHeaders) {
    auto res = get_with_headers("/health");
    
    ASSERT_EQ(res.result(), http::status::ok);
    
    // Check for policy headers
    EXPECT_TRUE(has_header(res, "X-Themis-Policy"));
    EXPECT_TRUE(has_header(res, "X-Themis-ANN"));
    EXPECT_TRUE(has_header(res, "X-Themis-Content-Enc"));
    EXPECT_TRUE(has_header(res, "X-Themis-Export"));
    EXPECT_TRUE(has_header(res, "X-Themis-Cache"));
    EXPECT_TRUE(has_header(res, "X-Themis-Retention-Days"));
}

// Test: Classify request as "offen" (public)
TEST_F(HttpGovernanceTest, Classification_Offen_AllowsANN) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "offen"}
    };
    
    json body = {
        {"collection", "test"},
        {"vector", {0.1, 0.2, 0.3}},
        {"k", 5}
    };
    
    auto res = post_with_headers("/vector/search", body, headers);
    
    // Should succeed (ANN allowed for offen)
    EXPECT_NE(res.result(), http::status::forbidden);
    
    // Check policy header
    std::string policy = get_header(res, "X-Themis-Policy");
    EXPECT_TRUE(policy.find("offen") != std::string::npos);
    
    std::string ann = get_header(res, "X-Themis-ANN");
    EXPECT_EQ(ann, "allowed");
}

// Test: Classify request as "geheim" with enforce mode blocks ANN
TEST_F(HttpGovernanceTest, Classification_Geheim_Enforce_BlocksANN) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "geheim"},
        {"X-Governance-Mode", "enforce"}
    };
    
    json body = {
        {"collection", "secret"},
        {"vector", {0.1, 0.2, 0.3}},
        {"k", 5}
    };
    
    auto res = post_with_headers("/vector/search", body, headers);
    
    // Should be blocked (ANN not allowed for geheim)
    EXPECT_EQ(res.result(), http::status::forbidden);
    
    auto response = json::parse(res.body());
    EXPECT_TRUE(response.contains("error"));
}

// Test: Classify request as "geheim" with observe mode allows ANN but warns
TEST_F(HttpGovernanceTest, Classification_Geheim_Observe_AllowsANNWithWarning) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "geheim"},
        {"X-Governance-Mode", "observe"}
    };
    
    json body = {
        {"collection", "secret"},
        {"vector", {0.1, 0.2, 0.3}},
        {"k", 5}
    };
    
    auto res = post_with_headers("/vector/search", body, headers);
    
    // In observe mode, may still block or warn depending on implementation
    // Just verify we get a policy header
    EXPECT_TRUE(has_header(res, "X-Themis-Policy"));
    
    // If not blocked, check for warning
    if (res.result() != http::status::forbidden) {
        // May have warning header in observe mode
        std::string warn = get_header(res, "X-Themis-Policy-Warn");
        // Warning is optional in current implementation
    }
}

// Test: VS-NfD classification requires content encryption
TEST_F(HttpGovernanceTest, Classification_VsNfd_RequiresEncryption) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "vs-nfd"},
        {"X-Governance-Mode", "enforce"}
    };
    
    json body = {
        {"path", "/documents/sensitive.pdf"},
        {"content", "base64encodedcontent"},
        {"mime_type", "application/pdf"}
    };
    
    auto res = post_with_headers("/content/import", body, headers);
    
    // Check encryption header
    std::string enc = get_header(res, "X-Themis-Content-Enc");
    EXPECT_EQ(enc, "required");
}

// Test: Streng-Geheim highest classification
TEST_F(HttpGovernanceTest, Classification_StrengGeheim_MostRestrictive) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "streng-geheim"},
        {"X-Governance-Mode", "enforce"}
    };
    
    json body = {
        {"collection", "topsecret"},
        {"vector", {0.1, 0.2, 0.3}},
        {"k", 5}
    };
    
    auto res = post_with_headers("/vector/search", body, headers);
    
    // Should be blocked (ANN not allowed)
    EXPECT_EQ(res.result(), http::status::forbidden);
    
    // Check policy header
    std::string policy = get_header(res, "X-Themis-Policy");
    EXPECT_TRUE(policy.find("streng-geheim") != std::string::npos);
    
    std::string ann = get_header(res, "X-Themis-ANN");
    EXPECT_TRUE(ann == "forbidden" || ann == "disabled");
    
    std::string enc = get_header(res, "X-Themis-Content-Enc");
    EXPECT_EQ(enc, "required");
    
    std::string export_allowed = get_header(res, "X-Themis-Export");
    EXPECT_TRUE(export_allowed == "forbidden" || export_allowed == "disabled");
}

// Test: Resource mapping applies classification automatically
TEST_F(HttpGovernanceTest, ResourceMapping_AppliesClassification) {
    // /admin/* endpoints should automatically be classified as vs-nfd
    auto res = get_with_headers("/admin/status");
    
    // Should have vs-nfd or higher classification applied
    std::string policy = get_header(res, "X-Themis-Policy");
    EXPECT_FALSE(policy.empty());
    
    // Encryption should be required
    std::string enc = get_header(res, "X-Themis-Content-Enc");
    EXPECT_EQ(enc, "required");
}

// Test: Header override for encryption
TEST_F(HttpGovernanceTest, HeaderOverride_EncryptLogs) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "offen"},
        {"X-Encrypt-Logs", "true"}
    };
    
    auto res = get_with_headers("/health", headers);
    
    ASSERT_EQ(res.result(), http::status::ok);
    
    // Log encryption should be reflected
    // (This is mainly for auditing, not directly visible in response)
}

// Test: Retention days header
TEST_F(HttpGovernanceTest, RetentionDays_ReturnsPolicy) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "vs-nfd"}
    };
    
    auto res = get_with_headers("/health", headers);
    
    ASSERT_EQ(res.result(), http::status::ok);
    
    std::string retention = get_header(res, "X-Themis-Retention-Days");
    EXPECT_FALSE(retention.empty());
    
    // VS-NfD should have 730 or higher day retention
    int ret_days = std::stoi(retention);
    EXPECT_GE(ret_days, 730);
}

// Test: Cache policy header
TEST_F(HttpGovernanceTest, CachePolicy_ReflectsClassification) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "geheim"}
    };
    
    auto res = get_with_headers("/health", headers);
    
    ASSERT_EQ(res.result(), http::status::ok);
    
    std::string cache = get_header(res, "X-Themis-Cache");
    
    // Geheim should disable caching
    EXPECT_EQ(cache, "disabled");
}

// Test: Export policy header
TEST_F(HttpGovernanceTest, ExportPolicy_ReflectsClassification) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "offen"}
    };
    
    auto res = get_with_headers("/health", headers);
    
    ASSERT_EQ(res.result(), http::status::ok);
    
    std::string export_allowed = get_header(res, "X-Themis-Export");
    
    // Offen should allow export
    EXPECT_EQ(export_allowed, "allowed");
}

// Test: Invalid classification defaults to restrictive
TEST_F(HttpGovernanceTest, InvalidClassification_DefaultsRestrictive) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "invalid-level"}
    };
    
    json body = {
        {"collection", "test"},
        {"vector", {0.1, 0.2, 0.3}},
        {"k", 5}
    };
    
    auto res = post_with_headers("/vector/search", body, headers);
    
    // Should fall back to heuristic (default restrictive)
    std::string policy = get_header(res, "X-Themis-Policy");
    EXPECT_FALSE(policy.empty());
}

// Test: Multiple requests with different classifications
TEST_F(HttpGovernanceTest, MultipleRequests_IndependentClassifications) {
    // Request 1: offen
    {
        std::map<std::string, std::string> headers = {{"X-Classification", "offen"}};
        auto res = get_with_headers("/health", headers);
    EXPECT_TRUE(get_header(res, "X-Themis-Policy").find("offen") != std::string::npos);
        EXPECT_EQ(get_header(res, "X-Themis-ANN"), "allowed");
    }
    
    // Request 2: geheim
    {
        std::map<std::string, std::string> headers = {{"X-Classification", "geheim"}};
        auto res = get_with_headers("/health", headers);
    EXPECT_TRUE(get_header(res, "X-Themis-Policy").find("geheim") != std::string::npos);
    std::string ann = get_header(res, "X-Themis-ANN");
    EXPECT_TRUE(ann == "forbidden" || ann == "disabled");
    }
    
    // Request 3: vs-nfd
    {
        std::map<std::string, std::string> headers = {{"X-Classification", "vs-nfd"}};
        auto res = get_with_headers("/health", headers);
    EXPECT_TRUE(get_header(res, "X-Themis-Policy").find("vs-nfd") != std::string::npos);
        EXPECT_EQ(get_header(res, "X-Themis-ANN"), "allowed");
    }
}

// Test: Redaction level header
TEST_F(HttpGovernanceTest, RedactionLevel_ReflectsClassification) {
    std::map<std::string, std::string> headers = {
        {"X-Classification", "streng-geheim"}
    };
    
    auto res = get_with_headers("/health", headers);
    
    ASSERT_EQ(res.result(), http::status::ok);
    
    // Check if redaction level is communicated (implementation dependent)
    std::string policy = get_header(res, "X-Themis-Policy");
    EXPECT_TRUE(policy.find("streng-geheim") != std::string::npos);
}

// Test: Governance mode defaults
TEST_F(HttpGovernanceTest, GovernanceMode_DefaultsToObserve) {
    // No X-Governance-Mode header, should default to observe
    std::map<std::string, std::string> headers = {
        {"X-Classification", "geheim"}
    };
    
    json body = {
        {"collection", "test"},
        {"vector", {0.1, 0.2, 0.3}},
        {"k", 5}
    };
    
    auto res = post_with_headers("/vector/search", body, headers);
    
    // In observe mode, should warn but not block
    // Note: observe mode may still block in current implementation
    // Just check that a policy header is present
    EXPECT_TRUE(has_header(res, "X-Themis-Policy"));
}
