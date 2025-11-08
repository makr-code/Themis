#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <optional>
#include <map>

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
using json = nlohmann::json;

static http::response<http::string_body> http_request(
    http::verb method,
    const std::string& host,
    uint16_t port,
    const std::string& target,
    const std::optional<json>& body = std::nullopt,
    const std::map<std::string,std::string>& headers = {}
) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    auto const results = resolver.resolve(host, std::to_string(port));
    stream.connect(results);
    http::request<http::string_body> req{method, target, 11};
    req.set(http::field::host, host);
    for (auto& kv : headers) req.set(kv.first, kv.second);
    if (body) {
        req.set(http::field::content_type, "application/json");
        req.body() = body->dump();
        req.prepare_payload();
    }
    http::write(stream, req);
    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(stream, buf, res);
    beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return res;
}

class HttpPiiManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef _WIN32
        _putenv_s("THEMIS_TOKEN_ADMIN", "admin-token-pii-tests");
#else
        setenv("THEMIS_TOKEN_ADMIN", "admin-token-pii-tests", 1);
#endif
        const std::string db_path = "data/themis_pii_manager_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
        themis::RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 32; cfg.block_cache_size_mb = 64;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());
        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);
        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18111;
        scfg.num_threads = 2;
        scfg.feature_pii_manager = true;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        if (server_) { server_->stop(); server_.reset(); }
        if (storage_) storage_->close();
        const std::string db_path = "data/themis_pii_manager_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
};

TEST_F(HttpPiiManagerTest, CreateAndGetMapping) {
    auto create = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","1111"},{"pseudonym","aaa"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(create.result(), http::status::created);
    json j = json::parse(create.body());
    EXPECT_EQ(j["original_uuid"], "1111");
    EXPECT_EQ(j["pseudonym"], "aaa");
    auto get = http_request(http::verb::get, "127.0.0.1", 18111, "/pii/1111", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(get.result(), http::status::ok);
}

TEST_F(HttpPiiManagerTest, DuplicateMappingReturnsConflict) {
    auto c1 = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","2222"},{"pseudonym","bbb"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(c1.result(), http::status::created);
    auto c2 = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","2222"},{"pseudonym","ccc"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(c2.result(), http::status::conflict);
}

TEST_F(HttpPiiManagerTest, ListPaginationAndFilters) {
    for (int i=0;i<12;++i) {
        auto r = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","id"+std::to_string(i)},{"pseudonym","p"+std::to_string(i)}}, {{"Authorization","Bearer admin-token-pii-tests"}});
        ASSERT_EQ(r.result(), http::status::created);
    }
    auto page1 = http_request(http::verb::get, "127.0.0.1", 18111, "/pii?page=1&page_size=5", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(page1.result(), http::status::ok);
    auto page3 = http_request(http::verb::get, "127.0.0.1", 18111, "/pii?page=3&page_size=5", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(page3.result(), http::status::ok);
    auto filter = http_request(http::verb::get, "127.0.0.1", 18111, "/pii?original_uuid=id1", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(filter.result(), http::status::ok);
}

TEST_F(HttpPiiManagerTest, ExportCsv) {
    auto c = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","9999"},{"pseudonym","zz"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(c.result(), http::status::created);
    auto csv = http_request(http::verb::get, "127.0.0.1", 18111, "/pii/export.csv", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(csv.result(), http::status::ok);
    ASSERT_NE(csv.body().find("original_uuid,pseudonym"), std::string::npos);
}

TEST_F(HttpPiiManagerTest, GetUnknownReturnsNotFound) {
    auto get = http_request(http::verb::get, "127.0.0.1", 18111, "/pii/does-not-exist", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(get.result(), http::status::not_found);
}

TEST_F(HttpPiiManagerTest, DeleteMapping) {
    auto c = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","7777"},{"pseudonym","pp"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(c.result(), http::status::created);
    auto del = http_request(http::verb::delete_, "127.0.0.1", 18111, "/pii/7777?mode=hard", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(del.result(), http::status::ok);
    auto again = http_request(http::verb::get, "127.0.0.1", 18111, "/pii/7777", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(again.result(), http::status::not_found);
}

TEST_F(HttpPiiManagerTest, CreateMalformedJsonReturnsBadRequest) {
    net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
    auto const results = resolver.resolve("127.0.0.1", "18111"); stream.connect(results);
    http::request<http::string_body> req{http::verb::post, "/pii", 11};
    req.set(http::field::host, "127.0.0.1"); req.set(http::field::content_type, "application/json");
    req.set("Authorization", "Bearer admin-token-pii-tests");
    req.body() = "{not-json}"; req.prepare_payload();
    http::write(stream, req);
    beast::flat_buffer buf; http::response<http::string_body> res; http::read(stream, buf, res);
    EXPECT_EQ(res.result(), http::status::bad_request);
    beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
}

TEST_F(HttpPiiManagerTest, CreateMissingFieldsReturnsBadRequest) {
    auto res = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"only","x"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    EXPECT_EQ(res.result(), http::status::bad_request);
}

TEST_F(HttpPiiManagerTest, UnauthorizedWithoutToken) {
    auto res = http_request(http::verb::get, "127.0.0.1", 18111, "/pii", std::nullopt, {});
    EXPECT_EQ(res.result(), http::status::unauthorized);
}

TEST_F(HttpPiiManagerTest, ReadOnlyTokenCannotCreate) {
#ifdef _WIN32
    _putenv_s("THEMIS_TOKEN_READONLY", "readonly-token-pii-tests");
#else
    setenv("THEMIS_TOKEN_READONLY", "readonly-token-pii-tests", 1);
#endif
    themis::server::HttpServer::Config scfg;
    scfg.host = "127.0.0.1";
    scfg.port = 18112;
    scfg.num_threads = 1;
    scfg.feature_pii_manager = true;
    auto server2 = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
    server2->start(); std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto res = http_request(http::verb::post, "127.0.0.1", 18112, "/pii", json{{"original_uuid","abc"},{"pseudonym","def"}}, {{"Authorization","Bearer readonly-token-pii-tests"}});
    EXPECT_EQ(res.result(), http::status::forbidden);
    server2->stop();
}

TEST_F(HttpPiiManagerTest, CsvExportHonorsFilter) {
    auto c1 = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","f1"},{"pseudonym","x1"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    auto c2 = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","f2"},{"pseudonym","x2"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(c1.result(), http::status::created);
    ASSERT_EQ(c2.result(), http::status::created);
    auto csv = http_request(http::verb::get, "127.0.0.1", 18111, "/pii/export.csv?original_uuid=f1", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(csv.result(), http::status::ok);
    EXPECT_NE(csv.body().find("f1"), std::string::npos);
    EXPECT_EQ(csv.body().find("f2"), std::string::npos);
}

TEST_F(HttpPiiManagerTest, ActiveOnlyFilter) {
    auto c1 = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","a1"},{"pseudonym","p1"}}, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(c1.result(), http::status::created);
    auto list = http_request(http::verb::get, "127.0.0.1", 18111, "/pii?active_only=true", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(list.result(), http::status::ok);
}

TEST_F(HttpPiiManagerTest, PaginationBeyondRange) {
    auto pg = http_request(http::verb::get, "127.0.0.1", 18111, "/pii?page=999&page_size=10", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(pg.result(), http::status::ok);
}

TEST_F(HttpPiiManagerTest, Insert50AndListPerformance) {
    for (int i=0;i<50;++i) {
        auto r = http_request(http::verb::post, "127.0.0.1", 18111, "/pii", json{{"original_uuid","perf"+std::to_string(i)},{"pseudonym","pv"+std::to_string(i)}}, {{"Authorization","Bearer admin-token-pii-tests"}});
        ASSERT_EQ(r.result(), http::status::created);
    }
    auto list = http_request(http::verb::get, "127.0.0.1", 18111, "/pii?page=1&page_size=20", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    ASSERT_EQ(list.result(), http::status::ok);
}

TEST_F(HttpPiiManagerTest, DeleteIdempotent) {
    auto del1 = http_request(http::verb::delete_, "127.0.0.1", 18111, "/pii/notthere", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    EXPECT_EQ(del1.result(), http::status::ok);
    auto del2 = http_request(http::verb::delete_, "127.0.0.1", 18111, "/pii/notthere", std::nullopt, {{"Authorization","Bearer admin-token-pii-tests"}});
    EXPECT_EQ(del2.result(), http::status::ok);
}
