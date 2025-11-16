#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class AuditHttpApiTest : public ::testing::Test {
protected:
    void SetUp() override {
    // Reduce rate limit for tests via environment (applies in server ctor)
#ifdef _WIN32
    _putenv("THEMIS_AUDIT_RATE_LIMIT=5");
#else
    setenv("THEMIS_AUDIT_RATE_LIMIT", "5", 1);
#endif
        const std::string db_path = "data/themis_http_audit_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
        themis::RocksDBWrapper::Config cfg;
        cfg.db_path = db_path;
        cfg.memtable_size_mb = 32;
        cfg.block_cache_size_mb = 64;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());
        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);

        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18111; // dedicated port
        scfg.num_threads = 2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Create a sample audit log line (plaintext payload JSON string)
        std::filesystem::create_directories("data/logs");
        std::ofstream ofs("data/logs/audit.jsonl", std::ios::app | std::ios::binary);
            // Minimal event: user alice CREATE user entity
            nlohmann::json event = {
                {"user", "alice"},
                {"action", "CREATE"},
                {"entity_type", "user"},
                {"entity_id", "alice"},
                {"success", true}
            };
            nlohmann::json line = {
                {"ts", 1730860000000LL},
                {"payload", event.dump()}
            };
            ofs << line.dump() << "\n";
    }

    void TearDown() override {
        if (server_) server_->stop();
        if (storage_) storage_->close();
        const std::string db_path = "data/themis_http_audit_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
        if (std::filesystem::exists("data/logs/audit.jsonl")) std::filesystem::remove("data/logs/audit.jsonl");
    }

    static http::response<http::string_body> http_get(const std::string& host, const std::string& port, const std::string& target) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve(host, port);
        stream.connect(results);
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        http::write(stream, req);
        beast::flat_buffer buf;
        http::response<http::string_body> res;
        http::read(stream, buf, res);
        beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res;
    }

    static http::response<http::string_body> http_get_auth(const std::string& host, const std::string& port, const std::string& target, const std::string& bearer) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve(host, port);
        stream.connect(results);
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::authorization, std::string("Bearer ") + bearer);
        http::write(stream, req);
        beast::flat_buffer buf;
        http::response<http::string_body> res;
        http::read(stream, buf, res);
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

TEST_F(AuditHttpApiTest, QueryReturnsSingleEntry) {
    auto res = http_get("127.0.0.1", "18111", "/api/audit?page=1&page_size=10");
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = nlohmann::json::parse(res.body());
    ASSERT_TRUE(body.contains("entries"));
    ASSERT_EQ(body["entries"].size(), 1);
    auto e = body["entries"][0];
    EXPECT_EQ(e["user"], "alice");
    EXPECT_EQ(e["action"], "CREATE");
    EXPECT_EQ(e["entityType"], "user");
    EXPECT_EQ(e["entityId"], "alice");
}

TEST_F(AuditHttpApiTest, CsvExportReturnsHeaderAndRow) {
    auto res = http_get("127.0.0.1", "18111", "/api/audit/export/csv?page_size=100");
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    ASSERT_TRUE(res.base().find(http::field::content_type) != res.base().end());
    std::string csv = res.body();
    // Expect first line header contains User,Action
    auto pos = csv.find('\n');
    ASSERT_NE(pos, std::string::npos);
    std::string header = csv.substr(0, pos);
    EXPECT_NE(header.find("User"), std::string::npos);
    EXPECT_NE(header.find("Action"), std::string::npos);
    // Second line contains alice
    auto pos2 = csv.find('\n', pos + 1);
    auto line2 = csv.substr(pos + 1, pos2 == std::string::npos ? std::string::npos : pos2 - (pos + 1));
    EXPECT_NE(line2.find("alice"), std::string::npos);
}

TEST_F(AuditHttpApiTest, UrlDecodingAndIso8601RangeAndRateLimit) {
    // Append an entry with special chars and a known action
    std::filesystem::create_directories("data/logs");
    {
        std::ofstream ofs("data/logs/audit.jsonl", std::ios::app | std::ios::binary);
        nlohmann::json ev = {
            {"user", "alice+admin"},
            {"action", "VIEW/ACCESS"},
            {"entity_type", "user"},
            {"entity_id", "alice"},
            {"success", true}
        };
        nlohmann::json line = {
            {"ts", 1730860000000LL},
            {"payload", ev.dump()}
        };
        ofs << line.dump() << "\n";
    }
    // URL decode: %2B -> '+', %2F -> '/'; ISO8601 with Z and offset
    auto res = http_get("127.0.0.1", "18111",
        "/api/audit?user=alice%2Badmin&action=VIEW%2FACCESS&start=1969-12-31T00:00:00Z&end=2100-01-01T00:00:00%2B02:00&page=1&page_size=10");
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = nlohmann::json::parse(res.body());
    ASSERT_TRUE(body.contains("entries"));
    // At least one entry should match
    bool found = false;
    for (auto& e : body["entries"]) {
        if (e.value("user", "") == "alice+admin" && e.value("action", "") == "VIEW/ACCESS") { found = true; break; }
    }
    EXPECT_TRUE(found);

    // Rate limit: use a unique Authorization bearer to isolate the bucket
    // Send repeated requests until 429 appears, assert it happens within a reasonable bound (<= 6)
#ifdef _WIN32
    std::string bearer = std::string("rate-limit-test-") + std::to_string(::GetCurrentProcessId());
#else
    std::string bearer = std::string("rate-limit-test-") + std::to_string(getpid());
#endif
    http::response<http::string_body> last;
    int ok_count = 0;
    const int max_attempts = 10;
    for (int i = 0; i < max_attempts; ++i) {
        last = http_get_auth("127.0.0.1", "18111", "/api/audit/export/csv?page_size=1", bearer);
        if (last.result() == http::status::ok) {
            ++ok_count;
        } else if (last.result() == http::status::too_many_requests) {
            break;
        } else {
            ADD_FAILURE() << "Unexpected status: " << last.result_int() << ", body=" << last.body();
            break;
        }
    }
    // Expect at least one OK before rate limiting kicks in, but not more than 6
    EXPECT_GE(ok_count, 1);
    EXPECT_LE(ok_count, 6);
    // Last response should be 429
    if (last.result() != http::status::too_many_requests) {
        // Try one more time to trigger 429
        last = http_get_auth("127.0.0.1", "18111", "/api/audit/export/csv?page_size=1", bearer);
    }
    ASSERT_EQ(last.result(), http::status::too_many_requests) << last.body();
    // Retry-After header should be present
    ASSERT_TRUE(last.base().find(http::field::retry_after) != last.base().end());
}
