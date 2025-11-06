#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

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

class PoliciesExportHttpTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure config directory exists and write a simple policies.yaml
        std::filesystem::create_directories("config");
                const char* yaml = R"YAML(
- id: allow-metrics-readonly
    name: readonly darf /metrics
    subjects: ["readonly"]
    actions: ["metrics.read"]
    resources: ["/metrics"]
    effect: allow

- id: allow-admin-policies-export
    name: admin darf Policies exportieren
    subjects: ["admin"]
    actions: ["admin"]
    resources: ["/policies/export/ranger"]
    effect: allow
)YAML";
        std::ofstream pf("config/policies.yaml", std::ios::binary);
        pf << yaml;
        pf.close();

        // Configure admin token via env so we can call export endpoint
#ifdef _WIN32
        _putenv_s("THEMIS_TOKEN_ADMIN", "admin-token-http-export");
#else
        setenv("THEMIS_TOKEN_ADMIN", "admin-token-http-export", 1);
#endif

        // Start server
        const std::string db_path = "data/themis_policy_export_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
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
        scfg.port = 18090; // separate port for this test
        scfg.num_threads = 2;

        server_ = std::make_unique<themis::server::HttpServer>(
            scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_
        );
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    void TearDown() override {
        if (server_) server_->stop();
        if (storage_) storage_->close();
        const std::string db_path = "data/themis_policy_export_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
    }

    http::response<http::string_body> get_with_headers(
        const std::string& target,
        const std::map<std::string, std::string>& headers = {}
    ) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", "18090");
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
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(PoliciesExportHttpTest, ExportPolicies_AsAdmin_ReturnsRangerJson) {
    auto res = get_with_headers(
        "/policies/export/ranger",
        {
            {"Authorization", "Bearer admin-token-http-export"}
        }
    );
    EXPECT_EQ(res.result(), http::status::ok);

        // Debug: output actual response
        std::cout << "Response status: " << res.result_int() << std::endl;
        std::cout << "Response body: " << res.body() << std::endl;

    // Very simple validation: response contains service name and resources
    auto body = res.body();
    EXPECT_NE(body.find("\"service\""), std::string::npos);
    EXPECT_NE(body.find("\"resources\""), std::string::npos);
}
