#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <filesystem>

#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "server/http_server.h"

// Global in-process HTTP server for integration tests using Google Test Environment
// This avoids spawning external processes and stabilizes tests.

namespace {
class ThemisServerEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        try {
            // Set auth tokens via ENV before creating any servers
#ifdef _WIN32
            _putenv_s("THEMIS_TOKEN_ADMIN", "admin-token-pii-tests");
            _putenv_s("THEMIS_TOKEN_READONLY", "readonly-token-pii-tests");
#else
            setenv("THEMIS_TOKEN_ADMIN", "admin-token-pii-tests", 1);
            setenv("THEMIS_TOKEN_READONLY", "readonly-token-pii-tests", 1);
#endif
            // Prepare clean test DB directory
            base_path_ = std::filesystem::path("./data/themis_gtest_env");
            if (std::filesystem::exists(base_path_)) {
                std::error_code ec; std::filesystem::remove_all(base_path_, ec);
            }

            // RocksDB configuration
            themis::RocksDBWrapper::Config cfg;
            cfg.db_path = base_path_.string();
            cfg.memtable_size_mb = 64;
            cfg.block_cache_size_mb = 256;
            cfg.enable_wal = true;

            storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
            if (!storage_->open()) {
                throw std::runtime_error("Failed to open RocksDB for test env");
            }

            secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
            graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
            vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
            tx_manager_ = std::make_shared<themis::TransactionManager>(
                *storage_, *secondary_index_, *graph_index_, *vector_index_
            );

            // HTTP server on standard test port
            themis::server::HttpServer::Config scfg;
            scfg.host = "127.0.0.1";
            scfg.port = 8765;
            scfg.num_threads = 2;
            // Enable features needed by tests
            scfg.feature_semantic_cache = false;
            scfg.feature_llm_store = false;
            scfg.feature_cdc = false;
            scfg.feature_timeseries = false; // timeseries tests bring their own server
            scfg.feature_pii_manager = true; // ensure PII feature enabled globally

            server_ = std::make_unique<themis::server::HttpServer>(
                scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_
            );
            server_->start();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        } catch (const std::exception& e) {
            ADD_FAILURE() << "ThemisServerEnvironment setup failed: " << e.what();
        }
    }

    void TearDown() override {
        try {
            if (server_) server_->stop();
            if (storage_) storage_->close();
            // Cleanup test DB directory
            if (!base_path_.empty() && std::filesystem::exists(base_path_)) {
                std::error_code ec; std::filesystem::remove_all(base_path_, ec);
            }
        } catch (...) {
            // best effort
        }
        server_.reset();
        tx_manager_.reset();
        vector_index_.reset();
        graph_index_.reset();
        secondary_index_.reset();
        storage_.reset();
    }

private:
    std::filesystem::path base_path_;
    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::unique_ptr<themis::server::HttpServer> server_;
};

// Register environment via static initializer (works with gtest_main)
struct EnvRegistrar {
    EnvRegistrar() {
        ::testing::AddGlobalTestEnvironment(new ThemisServerEnvironment());
    }
} g_env_registrar;

} // namespace
