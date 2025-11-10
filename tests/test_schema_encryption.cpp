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
#include "security/mock_key_provider.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

namespace {

http::response<http::string_body> http_request(
    http::verb method,
    const std::string& host,
    uint16_t port,
    const std::string& target,
    const std::optional<json>& body = std::nullopt,
    const std::map<std::string, std::string>& headers = {}
) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    
    auto const results = resolver.resolve(host, std::to_string(port));
    stream.connect(results);
    
    http::request<http::string_body> req{method, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "themis_test");
    
    for (const auto& [key, value] : headers) {
        req.set(key, value);
    }
    
    if (body.has_value()) {
        req.set(http::field::content_type, "application/json");
        req.body() = body->dump();
        req.prepare_payload();
    }
    
    http::write(stream, req);
    
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    
    return res;
}

} // namespace

class SchemaEncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set admin token BEFORE creating server
#ifdef _WIN32
        _putenv_s("THEMIS_TOKEN_ADMIN", "admin-token-schema-test");
        // Verify ENV var is set
        if (const char* token = getenv("THEMIS_TOKEN_ADMIN")) {
            std::cout << "[TEST] ENV THEMIS_TOKEN_ADMIN = " << token << std::endl;
        } else {
            std::cout << "[TEST] ENV THEMIS_TOKEN_ADMIN NOT SET!" << std::endl;
        }
#else
        setenv("THEMIS_TOKEN_ADMIN", "admin-token-schema-test", 1);
#endif

        // Setup database
        const std::string db_path = "data/themis_schema_encryption_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
        
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
        
        // Setup HTTP server (Auth wird automatisch konfiguriert wenn ENV-Var gesetzt)
        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18200;
        scfg.num_threads = 2;
        
        server_ = std::make_unique<themis::server::HttpServer>(
            scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_
        );
        
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        admin_headers_ = {{"Authorization", "Bearer admin-token-schema-test"}};
    }
    
    void TearDown() override {
        if (server_) {
            server_->stop();
            server_.reset();
        }
        if (storage_) {
            storage_->close();
            storage_.reset();
        }
        const std::string db_path = "data/themis_schema_encryption_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
    }
    
    std::unique_ptr<themis::server::HttpServer> server_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::map<std::string, std::string> admin_headers_;
};

// ============================================================================
// Schema API Tests
// ============================================================================

TEST_F(SchemaEncryptionTest, GetSchema_DefaultReturnsEmptyCollections) {
    auto res = http_request(http::verb::get, "127.0.0.1", 18200, 
                           "/config/encryption-schema", std::nullopt, admin_headers_);
    
    ASSERT_EQ(res.result(), http::status::ok);
    auto body = json::parse(res.body());
    
    EXPECT_TRUE(body.contains("collections"));
    EXPECT_TRUE(body["collections"].is_object());
}

TEST_F(SchemaEncryptionTest, PutSchema_ValidSchema_ReturnsOk) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"email", "ssn", "salary"}}
                }}
            }}
        }}
    };
    
    auto res = http_request(http::verb::put, "127.0.0.1", 18200,
                           "/config/encryption-schema", schema, admin_headers_);
    
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    auto body = json::parse(res.body());
    EXPECT_EQ(body["status"], "success");
}

TEST_F(SchemaEncryptionTest, PutSchema_InvalidJson_ReturnsBadRequest) {
    json invalid_schema = {
        {"collections", "not_an_object"}  // Should be object
    };
    
    auto res = http_request(http::verb::put, "127.0.0.1", 18200,
                           "/config/encryption-schema", invalid_schema, admin_headers_);
    
    EXPECT_EQ(res.result(), http::status::bad_request);
}

TEST_F(SchemaEncryptionTest, PutSchema_MissingEncryptionConfig_ReturnsBadRequest) {
    json schema = {
        {"collections", {
            {"users", {
                // Missing "encryption" section
                {"some_other_field", "value"}
            }}
        }}
    };
    
    auto res = http_request(http::verb::put, "127.0.0.1", 18200,
                           "/config/encryption-schema", schema, admin_headers_);
    
    EXPECT_EQ(res.result(), http::status::bad_request);
}

TEST_F(SchemaEncryptionTest, GetSchema_AfterPut_ReturnsSavedSchema) {
    // First PUT schema
    json schema = {
        {"collections", {
            {"employees", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"salary", "bonus"}}
                }}
            }}
        }}
    };
    
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(put_res.result(), http::status::ok);
    
    // Then GET schema
    auto get_res = http_request(http::verb::get, "127.0.0.1", 18200,
                               "/config/encryption-schema", std::nullopt, admin_headers_);
    
    ASSERT_EQ(get_res.result(), http::status::ok);
    auto body = json::parse(get_res.body());
    
    ASSERT_TRUE(body["collections"].contains("employees"));
    auto emp_config = body["collections"]["employees"]["encryption"];
    EXPECT_TRUE(emp_config["enabled"].get<bool>());
    EXPECT_EQ(emp_config["context_type"], "user");
    EXPECT_EQ(emp_config["fields"].size(), 2);
}

// ============================================================================
// Automatic Field Encryption Tests
// ============================================================================

TEST_F(SchemaEncryptionTest, PutEntity_WithSchemaEnabled_EncryptsFields) {
    // Setup schema
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"email", "ssn"}}
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert entity
    json entity = {
        {"id", "user1"},
        {"name", "Alice"},
        {"email", "alice@example.com"},
        {"ssn", "123-45-6789"}
    };
    
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/users", entity, admin_headers_);
    
    ASSERT_EQ(put_res.result(), http::status::ok) << put_res.body();
    
    // Verify encrypted fields exist in storage
    auto raw_blob = storage_->get("users:user1");
    ASSERT_TRUE(raw_blob.has_value());
    
    auto loaded = themis::BaseEntity::deserialize("user1", *raw_blob);
    
    // Plaintext field should exist
    EXPECT_TRUE(loaded.hasField("name"));
    
    // Encrypted fields should have _encrypted suffix
    EXPECT_TRUE(loaded.hasField("email_encrypted"));
    EXPECT_TRUE(loaded.hasField("ssn_encrypted"));
    
    // Original plaintext fields should NOT exist
    EXPECT_FALSE(loaded.hasField("email"));
    EXPECT_FALSE(loaded.hasField("ssn"));
}

TEST_F(SchemaEncryptionTest, PutEntity_WithSchemaDisabled_StoresPlaintext) {
    // Setup schema with encryption DISABLED
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", false},
                    {"context_type", "user"},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert entity
    json entity = {
        {"id", "user2"},
        {"email", "bob@example.com"}
    };
    
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/users", entity, admin_headers_);
    
    ASSERT_EQ(put_res.result(), http::status::ok);
    
    // Verify plaintext storage
    auto raw_blob = storage_->get("users:user2");
    ASSERT_TRUE(raw_blob.has_value());
    
    auto loaded = themis::BaseEntity::deserialize("user2", *raw_blob);
    
    // Email should be in plaintext (not encrypted)
    EXPECT_TRUE(loaded.hasField("email"));
    EXPECT_FALSE(loaded.hasField("email_encrypted"));
}

TEST_F(SchemaEncryptionTest, PutEntity_PartialEncryption_SomeFieldsEncrypted) {
    // Setup schema encrypting only 1 of 3 fields
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"ssn"}}  // Only SSN encrypted
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert entity
    json entity = {
        {"id", "user3"},
        {"name", "Charlie"},
        {"email", "charlie@example.com"},
        {"ssn", "987-65-4321"}
    };
    
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/users", entity, admin_headers_);
    
    ASSERT_EQ(put_res.result(), http::status::ok);
    
    // Verify storage
    auto raw_blob = storage_->get("users:user3");
    ASSERT_TRUE(raw_blob.has_value());
    
    auto loaded = themis::BaseEntity::deserialize("user3", *raw_blob);
    
    // Plaintext fields
    EXPECT_TRUE(loaded.hasField("name"));
    EXPECT_TRUE(loaded.hasField("email"));
    
    // Encrypted field
    EXPECT_TRUE(loaded.hasField("ssn_encrypted"));
    EXPECT_FALSE(loaded.hasField("ssn"));
}

// ============================================================================
// Context Type Tests (User vs Group)
// ============================================================================

TEST_F(SchemaEncryptionTest, ContextType_User_UsesUserSpecificKey) {
    json schema = {
        {"collections", {
            {"private_notes", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"content"}}
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert entity with user context
    json entity = {
        {"id", "note1"},
        {"content", "User123's private note"}
    };
    
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/private_notes", entity, admin_headers_);
    
    ASSERT_EQ(put_res.result(), http::status::ok);
    
    // Verify encryption with user-specific key
    auto raw_blob = storage_->get("private_notes:note1");
    ASSERT_TRUE(raw_blob.has_value());
    
    auto loaded = themis::BaseEntity::deserialize("note1", *raw_blob);
    EXPECT_TRUE(loaded.hasField("content_encrypted"));
    
    // Encrypted blob should contain key_id and metadata
    auto enc_b64 = loaded.getFieldAsString("content_encrypted");
    ASSERT_TRUE(enc_b64.has_value());
    EXPECT_GT(enc_b64->size(), 50); // Encrypted data is larger than plaintext
}

TEST_F(SchemaEncryptionTest, ContextType_Group_UsesGroupKey) {
    json schema = {
        {"collections", {
            {"team_docs", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "group"},
                    {"fields", {"shared_data"}}
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert entity
    json entity = {
        {"id", "doc1"},
        {"shared_data", "Team project info"}
    };
    
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/team_docs", entity, admin_headers_);
    
    ASSERT_EQ(put_res.result(), http::status::ok);
    
    // Verify encryption
    auto raw_blob = storage_->get("team_docs:doc1");
    ASSERT_TRUE(raw_blob.has_value());
    
    auto loaded = themis::BaseEntity::deserialize("doc1", *raw_blob);
    EXPECT_TRUE(loaded.hasField("shared_data_encrypted"));
}

// ============================================================================
// Multi-Collection Schema Tests
// ============================================================================

TEST_F(SchemaEncryptionTest, MultiCollection_DifferentConfigs_BothWork) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"email"}}
                }}
            }},
            {"orders", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"credit_card"}}
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert user
    json user = {{"id", "u1"}, {"email", "test@example.com"}};
    auto user_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                 "/documents/users", user, admin_headers_);
    ASSERT_EQ(user_res.result(), http::status::ok);
    
    // Insert order
    json order = {{"id", "o1"}, {"credit_card", "4111-1111-1111-1111"}};
    auto order_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                  "/documents/orders", order, admin_headers_);
    ASSERT_EQ(order_res.result(), http::status::ok);
    
    // Verify both encrypted
    auto user_blob = storage_->get("users:u1");
    ASSERT_TRUE(user_blob.has_value());
    auto user_entity = themis::BaseEntity::deserialize("u1", *user_blob);
    EXPECT_TRUE(user_entity.hasField("email_encrypted"));
    
    auto order_blob = storage_->get("orders:o1");
    ASSERT_TRUE(order_blob.has_value());
    auto order_entity = themis::BaseEntity::deserialize("o1", *order_blob);
    EXPECT_TRUE(order_entity.hasField("credit_card_encrypted"));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(SchemaEncryptionTest, EmptyFieldsList_NoEncryption) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", json::array()}  // Empty array
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert entity
    json entity = {{"id", "u1"}, {"email", "test@example.com"}};
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/users", entity, admin_headers_);
    ASSERT_EQ(put_res.result(), http::status::ok);
    
    // Verify no encryption (empty fields list)
    auto raw_blob = storage_->get("users:u1");
    ASSERT_TRUE(raw_blob.has_value());
    auto loaded = themis::BaseEntity::deserialize("u1", *raw_blob);
    EXPECT_TRUE(loaded.hasField("email"));  // Plaintext
    EXPECT_FALSE(loaded.hasField("email_encrypted"));
}

TEST_F(SchemaEncryptionTest, NonexistentField_NoError) {
    json schema = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"nonexistent_field"}}
                }}
            }}
        }}
    };
    
    auto schema_res = http_request(http::verb::put, "127.0.0.1", 18200,
                                   "/config/encryption-schema", schema, admin_headers_);
    ASSERT_EQ(schema_res.result(), http::status::ok);
    
    // Insert entity without the field
    json entity = {{"id", "u1"}, {"name", "Alice"}};
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/users", entity, admin_headers_);
    
    // Should succeed (field doesn't exist, nothing to encrypt)
    ASSERT_EQ(put_res.result(), http::status::ok);
}

TEST_F(SchemaEncryptionTest, UpdateSchema_NewFieldsApplied) {
    // Initial schema
    json schema1 = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"email"}}
                }}
            }}
        }}
    };
    
    auto res1 = http_request(http::verb::put, "127.0.0.1", 18200,
                            "/config/encryption-schema", schema1, admin_headers_);
    ASSERT_EQ(res1.result(), http::status::ok);
    
    // Updated schema with additional field
    json schema2 = {
        {"collections", {
            {"users", {
                {"encryption", {
                    {"enabled", true},
                    {"context_type", "user"},
                    {"fields", {"email", "phone"}}
                }}
            }}
        }}
    };
    
    auto res2 = http_request(http::verb::put, "127.0.0.1", 18200,
                            "/config/encryption-schema", schema2, admin_headers_);
    ASSERT_EQ(res2.result(), http::status::ok);
    
    // Insert entity with new field
    json entity = {
        {"id", "u1"},
        {"email", "test@example.com"},
        {"phone", "+1-555-1234"}
    };
    
    auto put_res = http_request(http::verb::put, "127.0.0.1", 18200,
                               "/documents/users", entity, admin_headers_);
    ASSERT_EQ(put_res.result(), http::status::ok);
    
    // Verify both fields encrypted
    auto raw_blob = storage_->get("users:u1");
    ASSERT_TRUE(raw_blob.has_value());
    auto loaded = themis::BaseEntity::deserialize("u1", *raw_blob);
    EXPECT_TRUE(loaded.hasField("email_encrypted"));
    EXPECT_TRUE(loaded.hasField("phone_encrypted"));
}
