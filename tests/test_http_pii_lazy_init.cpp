#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>

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

class PiiHttpLazyInitTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef _WIN32
        _putenv_s("THEMIS_TOKEN_ADMIN", "admin-token-pii");
#else
        setenv("THEMIS_TOKEN_ADMIN", "admin-token-pii", 1);
#endif
        const std::string db_path = "data/themis_pii_http_test";
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
        tx_manager_ = std::make_shared<themis::TransactionManager>(
            *storage_, *secondary_index_, *graph_index_, *vector_index_
        );

        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18091; // dedicated port
        scfg.num_threads = 4;

        server_ = std::make_unique<themis::server::HttpServer>(
            scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_
        );
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
            server_.reset(); // Destroy server instance to clear PIIPseudonymizer singleton
        }
        if (storage_) storage_->close();
        const std::string db_path = "data/themis_pii_http_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
        
        // Clear test environment variables AFTER server is destroyed
#ifdef _WIN32
        _putenv_s("THEMIS_PII_FORCE_INIT_FAIL", "");
#else
        unsetenv("THEMIS_PII_FORCE_INIT_FAIL");
#endif
    }

    static http::response<http::string_body> http_get(const std::string& host, const std::string& port,
                                                      const std::string& target,
                                                      const std::map<std::string, std::string>& headers = {}) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve(host, port);
        stream.connect(results);

        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        for (const auto& [k, v] : headers) req.set(k, v);

        http::write(stream, req);

        beast::flat_buffer buf;
        http::response<http::string_body> res;
        http::read(stream, buf, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return res;
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(PiiHttpLazyInitTest, FirstRevealTriggersLazyInit_No5xx) {
    // Unknown UUID; we only assert that server does not return 5xx (503 init failure or 500)
    auto res = http_get("127.0.0.1", "18091", "/pii/reveal/aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", {
        {"Authorization", "Bearer admin-token-pii"}
    });
    // Accept 200 or 404 depending on implementation, but no 5xx
    EXPECT_NE(res.result_int(), 500);
    EXPECT_NE(res.result_int(), 503);
}

TEST_F(PiiHttpLazyInitTest, ForcedInitFailure_ThrowsAndReturns503) {
    // Force lazy init hard failure (throw) -> expect 503 service unavailable
#ifdef _WIN32
    _putenv_s("THEMIS_PII_FORCE_INIT_FAIL", "1");
#else
    setenv("THEMIS_PII_FORCE_INIT_FAIL", "1", 1);
#endif
    auto res_fail = http_get("127.0.0.1", "18091", "/pii/reveal/99999999-1111-2222-3333-444444444444", {
        {"Authorization", "Bearer admin-token-pii"}
    });
    EXPECT_EQ(res_fail.result_int(), 503) << "Expected 503 during forced throw failure";

    // Clear failure injection and retry – should initialize and return non-503 (likely 404)
#ifdef _WIN32
    _putenv_s("THEMIS_PII_FORCE_INIT_FAIL", "");
#else
    unsetenv("THEMIS_PII_FORCE_INIT_FAIL");
#endif
    auto res_ok = http_get("127.0.0.1", "18091", "/pii/reveal/99999999-1111-2222-3333-444444444444", {
        {"Authorization", "Bearer admin-token-pii"}
    });
    EXPECT_NE(res_ok.result_int(), 503);
    EXPECT_NE(res_ok.result_int(), 500);
}

TEST_F(PiiHttpLazyInitTest, ForcedInitFailure_Silent503Mode) {
    // Force lazy init silent failure (no throw, leave null) -> expect 503
#ifdef _WIN32
    _putenv_s("THEMIS_PII_FORCE_INIT_FAIL", "503");
#else
    setenv("THEMIS_PII_FORCE_INIT_FAIL", "503", 1);
#endif
    auto res_fail = http_get("127.0.0.1", "18091", "/pii/reveal/aaaaaaaa-1111-2222-3333-bbbbbbbbbbbb", {
        {"Authorization", "Bearer admin-token-pii"}
    });
    EXPECT_EQ(res_fail.result_int(), 503) << "Expected 503 during silent failure mode";

    // Clear failure injection and retry
#ifdef _WIN32
    _putenv_s("THEMIS_PII_FORCE_INIT_FAIL", "");
#else
    unsetenv("THEMIS_PII_FORCE_INIT_FAIL");
#endif
    auto res_ok = http_get("127.0.0.1", "18091", "/pii/reveal/aaaaaaaa-1111-2222-3333-bbbbbbbbbbbb", {
        {"Authorization", "Bearer admin-token-pii"}
    });
    EXPECT_NE(res_ok.result_int(), 503);
    EXPECT_NE(res_ok.result_int(), 500);
}

TEST_F(PiiHttpLazyInitTest, ConcurrentReveal_No5xxAndNoCrash) {
    const int N = 16;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&]() {
            try {
                auto res = http_get("127.0.0.1", "18091", "/pii/reveal/ffffffff-1111-2222-3333-444444444444", {
                    {"Authorization", "Bearer admin-token-pii"}
                });
                if (res.result_int() == 500 || res.result_int() == 503) {
                    ++errors;
                }
            } catch (...) {
                ++errors;
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(errors.load(), 0) << "At least one concurrent request failed with 5xx or exception";
}
