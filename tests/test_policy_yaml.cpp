#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>

#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"

#ifdef _WIN32
#include <cstdlib>
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class PolicyYamlTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure config directory exists in working directory
        std::filesystem::create_directories("config");

        // Write policies.yaml
        const char* yaml = R"YAML(
- id: allow-metrics-readonly
  name: readonly darf /metrics
  subjects: ["readonly"]
  actions: ["metrics.read"]
  resources: ["/metrics"]
  effect: allow

- id: hr-allow-internal-read
  name: HR-Lesen nur intern erlaubt
  subjects: ["*"]
  actions: ["read"]
  resources: ["/entities/hr:"]
  allowed_ip_prefixes: ["10.", "192.168.", "172.16.", "172.17.", "172.18."]
  effect: allow

- id: hr-deny-external-read
  name: HR-Lesen extern verbieten
  subjects: ["*"]
  actions: ["read"]
  resources: ["/entities/hr:"]
  effect: deny
)YAML";
        std::ofstream pf("config/policies.yaml", std::ios::binary);
        pf << yaml;
        pf.close();

        // Setup RocksDB and server
        const std::string db_path = "data/themis_policy_yaml_test";
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

        // Configure readonly token via env so auth middleware picks it up
    #ifdef _WIN32
        _putenv_s("THEMIS_TOKEN_READONLY", "readonly-token-yaml");
    #else
        setenv("THEMIS_TOKEN_READONLY", "readonly-token-yaml", 1);
    #endif

        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18089; // test port
        scfg.num_threads = 2;

        server_ = std::make_unique<themis::server::HttpServer>(
            scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_
        );
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (server_) server_->stop();
        if (storage_) storage_->close();
        const std::string db_path = "data/themis_policy_yaml_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
        // Do not remove policies.yaml to allow investigation on failure
    }

    http::response<http::string_body> get_with_headers(
        const std::string& target,
        const std::map<std::string, std::string>& headers = {}
    ) {
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::tcp_stream stream(ioc);

            auto const results = resolver.resolve("127.0.0.1", "18089");
            stream.connect(results);

            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, "127.0.0.1");

            for (const auto& [k, v] : headers) req.set(k, v);

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

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(PolicyYamlTest, MetricsAllowedForReadonlyFromYaml) {
    auto res = get_with_headers(
        "/metrics",
        {
            {"Authorization", "Bearer readonly-token-yaml"}
        }
    );
    EXPECT_EQ(res.result(), http::status::ok);
}

TEST_F(PolicyYamlTest, HRReadDeniedForExternalIP) {
    auto res = get_with_headers(
        "/entities/hr:123",
        {
            {"Authorization", "Bearer readonly-token-yaml"},
            {"X-Forwarded-For", "203.0.113.10"}
        }
    );
    EXPECT_EQ(res.result(), http::status::forbidden);
}
