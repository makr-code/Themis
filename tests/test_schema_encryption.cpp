#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <thread>
#include <chrono>

#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "storage/base_entity.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

static http::response<http::string_body> http_request(http::verb method, const std::string& host, uint16_t port, const std::string& target, const std::optional<json>& body = std::nullopt, const std::map<std::string,std::string>& headers = {}) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    auto const results = resolver.resolve(host, std::to_string(port));
    stream.connect(results);
    http::request<http::string_body> req{method, target, 11};
    req.set(http::field::host, host);
    if (body) {
        req.set(http::field::content_type, "application/json");
        req.body() = body->dump();
        req.prepare_payload();
    }
    for (auto& kv : headers) req.set(kv.first, kv.second);
    http::write(stream, req);
    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(stream, buf, res);
    beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return res;
}

class SchemaEncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef _WIN32
        _putenv_s("THEMIS_TOKEN_ADMIN", "admin-token-schema-test");
#else
        setenv("THEMIS_TOKEN_ADMIN", "admin-token-schema-test", 1);
#endif
        const std::string db_path = "data/themis_schema_encryption_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
        themis::RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 32; cfg.block_cache_size_mb = 64;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg); ASSERT_TRUE(storage_->open());
        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);
        themis::server::HttpServer::Config scfg; scfg.host="127.0.0.1"; scfg.port=18200; scfg.num_threads=2;
        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start(); std::this_thread::sleep_for(std::chrono::milliseconds(300));
        admin_headers_ = {{"Authorization", "Bearer admin-token-schema-test"}};
    }
    void TearDown() override {
        if (server_) { server_->stop(); server_.reset(); }
        if (storage_) storage_->close();
        const std::string db_path = "data/themis_schema_encryption_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
    }

    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::map<std::string, std::string> admin_headers_;
};

TEST_F(SchemaEncryptionTest, GetSchema_DefaultReturnsEmptyCollections) {
    auto res = http_request(http::verb::get, "127.0.0.1", 18200, "/config/encryption-schema", std::nullopt, admin_headers_);
    ASSERT_EQ(res.result(), http::status::ok);
    auto body = json::parse(res.body());
    EXPECT_TRUE(body.contains("collections"));
}
TEST_F(SchemaEncryptionTest, PutSchema_ValidSchema_ReturnsOk) {
    json schema = {{"collections", {{"users", {{"encryption", {{"enabled", true}, {"context_type", "user"}, {"fields", json::array({"email", "ssn"})}}}}}}}};
    auto res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["status"], "ok");
    EXPECT_TRUE(body.contains("collections_configured"));
}

TEST_F(SchemaEncryptionTest, PutEntity_WithSchemaEnabled_EncryptsFields) {
    json schema = {{"collections", {{"users", {{"encryption", {{"enabled", true}, {"context_type", "user"}, {"fields", json::array({"email", "ssn"})}}}}}}}};
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    json entity_data = {{"name", "Alice"}, {"email", "alice@example.com"}, {"ssn", "123-45-6789"}};
    json put_body = {{"key", "users:user1"}, {"blob", entity_data.dump()}};
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200, "/entities/users:user1", put_body, admin_headers_);
    ASSERT_TRUE(put_res.result() == http::status::ok || put_res.result() == http::status::created) << put_res.body();
    auto raw_blob = storage_->get("users:user1");
    ASSERT_TRUE(raw_blob.has_value());
    auto loaded = themis::BaseEntity::deserialize("user1", *raw_blob);
    EXPECT_TRUE(loaded.hasField("name"));
    EXPECT_TRUE(loaded.hasField("email_encrypted"));
    EXPECT_TRUE(loaded.hasField("ssn_encrypted"));
}

TEST_F(SchemaEncryptionTest, PutEntity_WithSchemaDisabled_NoEncryption) {
    json schema = {{"collections", {{"users", {{"encryption", {{"enabled", false}, {"fields", json::array({"email"})}}}}}}}};
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    json entity_data = {{"name", "Bob"}, {"email", "bob@example.com"}};
    json put_body = {{"key", "users:user2"}, {"blob", entity_data.dump()}};
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200, "/entities/users:user2", put_body, admin_headers_);
    ASSERT_TRUE(put_res.result() == http::status::ok || put_res.result() == http::status::created);
    auto raw_blob = storage_->get("users:user2");
    ASSERT_TRUE(raw_blob.has_value());
    auto loaded = themis::BaseEntity::deserialize("user2", *raw_blob);
    EXPECT_TRUE(loaded.hasField("email"));
    EXPECT_FALSE(loaded.hasField("email_encrypted"));
}

TEST_F(SchemaEncryptionTest, PutEntity_PartialFields_OnlySpecifiedFieldsEncrypted) {
    json schema = {{"collections", {{"users", {{"encryption", {{"enabled", true}, {"fields", json::array({"ssn"})}}}}}}}};
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    json entity_data = {{"name", "Charlie"}, {"email", "charlie@example.com"}, {"ssn", "999-88-7777"}};
    json put_body = {{"key", "users:user3"}, {"blob", entity_data.dump()}};
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200, "/entities/users:user3", put_body, admin_headers_);
    ASSERT_TRUE(put_res.result() == http::status::ok || put_res.result() == http::status::created);
    auto raw_blob = storage_->get("users:user3");
    ASSERT_TRUE(raw_blob.has_value());
    auto loaded = themis::BaseEntity::deserialize("user3", *raw_blob);
    EXPECT_TRUE(loaded.hasField("email"));
    EXPECT_TRUE(loaded.hasField("ssn_encrypted"));
    EXPECT_FALSE(loaded.hasField("email_encrypted"));
}

TEST_F(SchemaEncryptionTest, PutEntity_MultipleCollections_IndependentSchemas) {
    json schema = {{"collections", {
        {"users", {{"encryption", {{"enabled", true}, {"fields", json::array({"email"})}}}}},
        {"orders", {{"encryption", {{"enabled", true}, {"fields", json::array({"payment_info"})}}}}}
    }}};
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    json user_data = {{"name", "Dave"}, {"email", "dave@example.com"}};
    json user_put_body = {{"key", "users:user4"}, {"blob", user_data.dump()}};
    auto user_put = http_request(http::verb::put, "127.0.0.1", 18200, "/entities/users:user4", user_put_body, admin_headers_);
    ASSERT_TRUE(user_put.result() == http::status::ok || user_put.result() == http::status::created);
    auto user_blob = storage_->get("users:user4");
    ASSERT_TRUE(user_blob.has_value());
    auto user_loaded = themis::BaseEntity::deserialize("user4", *user_blob);
    EXPECT_TRUE(user_loaded.hasField("email_encrypted"));
}

TEST_F(SchemaEncryptionTest, PutSchema_InvalidJson_ReturnsBadRequest) {
    json invalid_schema = {{"collections", "not_an_object"}};
    auto res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", invalid_schema, admin_headers_);
    EXPECT_EQ(res.result(), http::status::bad_request);
}

TEST_F(SchemaEncryptionTest, GetSchema_AfterPut_ReturnsSavedSchema) {
    json schema = {{"collections", {{"users", {{"encryption", {{"enabled", true}, {"fields", json::array({"email"})}}}}}}}};
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(put_res.result(), http::status::ok);
    auto get_res = http_request(http::verb::get, "127.0.0.1", 18200, "/config/encryption-schema", std::nullopt, admin_headers_);
    ASSERT_EQ(get_res.result(), http::status::ok);
    auto body = json::parse(get_res.body());
    EXPECT_TRUE(body["collections"].contains("users"));
    EXPECT_TRUE(body["collections"]["users"]["encryption"]["enabled"].get<bool>());
}

TEST_F(SchemaEncryptionTest, QueryAql_WithEncryptedFields_AutoDecrypts) {
    // Create secondary index on name field
    json index_body = {{"table", "users"}, {"column", "name"}};
    auto index_res = http_request(http::verb::post, "127.0.0.1", 18200, "/index/create", index_body, admin_headers_);
    ASSERT_TRUE(index_res.result() == http::status::ok || index_res.result() == http::status::created) << "Index creation failed: " << index_res.body();
    
    json schema = {{"collections", {{"users", {{"encryption", {{"enabled", true}, {"context_type", "user"}, {"fields", json::array({"email", "ssn"})}}}}}}}};
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200, "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    json entity1 = {{"name", "Alice"}, {"email", "alice@example.com"}, {"ssn", "123-45-6789"}};
    json put1 = {{"key", "users:user1"}, {"blob", entity1.dump()}};
    auto put1_res = http_request(http::verb::put, "127.0.0.1", 18200, "/entities/users:user1", put1, admin_headers_);
    ASSERT_TRUE(put1_res.result() == http::status::ok || put1_res.result() == http::status::created) << "Failed to insert Alice: " << put1_res.body();
    
    json entity2 = {{"name", "Bob"}, {"email", "bob@example.com"}, {"ssn", "987-65-4321"}};
    json put2 = {{"key", "users:user2"}, {"blob", entity2.dump()}};
    auto put2_res = http_request(http::verb::put, "127.0.0.1", 18200, "/entities/users:user2", put2, admin_headers_);
    ASSERT_TRUE(put2_res.result() == http::status::ok || put2_res.result() == http::status::created);
    
    // Verify entity was stored by fetching directly
    auto get_res = http_request(http::verb::get, "127.0.0.1", 18200, "/entities/users:user1", std::nullopt, admin_headers_);
    ASSERT_EQ(get_res.result(), http::status::ok) << "Direct GET failed: " << get_res.body();
    auto get_entity = json::parse(get_res.body());
    std::cout << "Direct GET entity: " << get_entity.dump(2) << std::endl;
    
    // Query with simple full scan (no filter to test retrieval first)
    json query_body1 = {{"query", "FOR u IN users RETURN u"}, {"allow_full_scan", true}};
    auto query_res1 = http_request(http::verb::post, "127.0.0.1", 18200, "/query/aql", query_body1, admin_headers_);
    std::cout << "Full scan response status: " << query_res1.result_int() << std::endl;
    std::cout << "Full scan response body: " << query_res1.body() << std::endl;
    
    // Query with filter to use index
    json query_body = {{"query", "FOR u IN users FILTER u.name == 'Alice' RETURN u"}};
    auto query_res = http_request(http::verb::post, "127.0.0.1", 18200, "/query/aql", query_body, admin_headers_);
    std::cout << "Filtered query response status: " << query_res.result_int() << std::endl;
    std::cout << "Filtered query response body: " << query_res.body() << std::endl;
    
    ASSERT_EQ(query_res.result(), http::status::ok) << query_res.body();
    
    auto response = json::parse(query_res.body());
    ASSERT_TRUE(response.contains("entities"));
    EXPECT_GE(response["entities"].size(), 1) << "Expected at least 1 entity, got: " << response["entities"].dump();
    
    // Verify Alice's data is decrypted
    bool found_alice = false;
    for (const auto& entity : response["entities"]) {
        if (entity.contains("name") && entity["name"] == "Alice") {
            found_alice = true;
            EXPECT_TRUE(entity.contains("email"));
            EXPECT_EQ(entity["email"], "alice@example.com");
            EXPECT_TRUE(entity.contains("ssn"));
            EXPECT_EQ(entity["ssn"], "123-45-6789");
            EXPECT_FALSE(entity.contains("email_encrypted"));
            EXPECT_FALSE(entity.contains("ssn_encrypted"));
            EXPECT_FALSE(entity.contains("email_enc"));
            EXPECT_FALSE(entity.contains("ssn_enc"));
        }
    }
    EXPECT_TRUE(found_alice) << "Alice not found in query results";
}
