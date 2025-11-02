#include <gtest/gtest.h>
#include "server/http_server.h"
#include "storage/storage_engine.h"
#include "document/document.h"
#include "utils/json_utils.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <crow.h>

namespace fs = std::filesystem;

/**
 * Integration tests for backup and restore functionality
 * Tests the complete backup/restore cycle with data integrity verification
 */
class BackupRestoreIntegrationTest : public ::testing::Test {
protected:
    static constexpr int TEST_PORT = 18090;
    const std::string BASE_DB_PATH = "./data/backup_restore_test";
    const std::string BACKUP_ROOT = "./data/backup_restore_test_backups";
    const std::string COLLECTION = "test_backup_collection";
    
    std::unique_ptr<vccdb::HttpServer> server_;
    std::thread server_thread_;

    void SetUp() override {
        // Clean up any existing test data
        fs::remove_all(BASE_DB_PATH);
        fs::remove_all(BACKUP_ROOT);
        fs::create_directories(BASE_DB_PATH);
        fs::create_directories(BACKUP_ROOT);

        // Start HTTP server
        vccdb::HttpServer::Config config;
        config.port = TEST_PORT;
        config.db_path = BASE_DB_PATH;
        config.max_connections = 10;
        config.request_timeout_ms = 5000;
        config.sse_max_events_per_second = 0;

        server_ = std::make_unique<vccdb::HttpServer>(config);
        server_thread_ = std::thread([this]() { server_->run(); });
        
        // Wait for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        // Clean up test data
        fs::remove_all(BASE_DB_PATH);
        fs::remove_all(BACKUP_ROOT);
    }

    // HTTP helper for POST requests
    std::string httpPost(const std::string& endpoint, const std::string& body) {
        crow::SimpleApp tempApp;
        crow::request req;
        req.url = "http://localhost:" + std::to_string(TEST_PORT) + endpoint;
        req.method = "POST"_method;
        req.body = body;
        
        std::string response;
        try {
            CURL* curl = curl_easy_init();
            if (curl) {
                std::string url = "http://localhost:" + std::to_string(TEST_PORT) + endpoint;
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                
                struct WriteCallback {
                    static size_t write(char* ptr, size_t size, size_t nmemb, std::string* data) {
                        data->append(ptr, size * nmemb);
                        return size * nmemb;
                    }
                };
                
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback::write);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
                
                struct curl_slist* headers = nullptr;
                headers = curl_slist_append(headers, "Content-Type: application/json");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                
                CURLcode res = curl_easy_perform(curl);
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                
                if (res != CURLE_OK) {
                    return "{\"error\": \"curl failed\"}";
                }
            }
        } catch (...) {
            return "{\"error\": \"exception\"}";
        }
        return response;
    }

    // HTTP helper for GET requests
    std::string httpGet(const std::string& endpoint) {
        std::string response;
        try {
            CURL* curl = curl_easy_init();
            if (curl) {
                std::string url = "http://localhost:" + std::to_string(TEST_PORT) + endpoint;
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                
                struct WriteCallback {
                    static size_t write(char* ptr, size_t size, size_t nmemb, std::string* data) {
                        data->append(ptr, size * nmemb);
                        return size * nmemb;
                    }
                };
                
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback::write);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
                
                CURLcode res = curl_easy_perform(curl);
                curl_easy_cleanup(curl);
                
                if (res != CURLE_OK) {
                    return "{\"error\": \"curl failed\"}";
                }
            }
        } catch (...) {
            return "{\"error\": \"exception\"}";
        }
        return response;
    }

    // Create test documents
    std::vector<std::string> createTestDocuments(int count) {
        std::vector<std::string> docIds;
        for (int i = 0; i < count; ++i) {
            nlohmann::json doc = {
                {"name", "Document_" + std::to_string(i)},
                {"index", i},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
                {"data", std::string(100, 'A' + (i % 26))}  // Some bulk data
            };
            
            std::string response = httpPost("/api/collections/" + COLLECTION + "/documents", doc.dump());
            auto respJson = nlohmann::json::parse(response);
            
            if (respJson.contains("_id")) {
                docIds.push_back(respJson["_id"].get<std::string>());
            }
        }
        return docIds;
    }

    // Verify documents exist and match expected data
    bool verifyDocuments(const std::vector<std::string>& docIds) {
        for (size_t i = 0; i < docIds.size(); ++i) {
            std::string response = httpGet("/api/collections/" + COLLECTION + "/documents/" + docIds[i]);
            auto doc = nlohmann::json::parse(response);
            
            if (!doc.contains("name") || doc["name"] != "Document_" + std::to_string(i)) {
                return false;
            }
            if (!doc.contains("index") || doc["index"] != i) {
                return false;
            }
        }
        return true;
    }

    // Create backup using HTTP API
    std::string createBackup() {
        std::string response = httpPost("/api/admin/backup", "{}");
        auto respJson = nlohmann::json::parse(response);
        
        if (respJson.contains("backup_path")) {
            return respJson["backup_path"].get<std::string>();
        }
        return "";
    }

    // Manually copy checkpoint to simulate backup
    std::string manualBackup() {
        std::string timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        fs::path backupPath = fs::path(BACKUP_ROOT) / ("checkpoint_" + timestamp);
        fs::path dbPath = fs::path(BASE_DB_PATH) / ".rocksdb";
        
        if (fs::exists(dbPath)) {
            fs::create_directories(backupPath);
            fs::copy(dbPath, backupPath / ".rocksdb", fs::copy_options::recursive);
            
            // Create manifest
            nlohmann::json manifest = {
                {"timestamp", timestamp},
                {"db_path", BASE_DB_PATH},
                {"backup_type", "manual_checkpoint"}
            };
            
            std::ofstream manifestFile(backupPath / "manifest.json");
            manifestFile << manifest.dump(2);
            manifestFile.close();
            
            return backupPath.string();
        }
        return "";
    }

    // Archive WAL files
    void archiveWALFiles(const std::string& backupPath) {
        fs::path walArchive = fs::path(backupPath) / "wal_archive";
        fs::create_directories(walArchive);
        
        fs::path dbPath = fs::path(BASE_DB_PATH) / ".rocksdb";
        
        if (fs::exists(dbPath)) {
            for (const auto& entry : fs::directory_iterator(dbPath)) {
                if (entry.path().extension() == ".log") {
                    std::string timestamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                    std::string archiveName = timestamp + "-" + entry.path().filename().string();
                    fs::copy_file(entry.path(), walArchive / archiveName, fs::copy_options::overwrite_existing);
                }
            }
        }
    }

    // Restore from backup
    bool restoreFromBackup(const std::string& backupPath, bool withWAL = false) {
        // Stop server
        server_->stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        // Wait for clean shutdown
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Remove current DB
        fs::remove_all(BASE_DB_PATH);
        fs::create_directories(BASE_DB_PATH);
        
        // Copy checkpoint back
        fs::path checkpointSrc = fs::path(backupPath) / ".rocksdb";
        fs::path dbDest = fs::path(BASE_DB_PATH) / ".rocksdb";
        
        if (!fs::exists(checkpointSrc)) {
            return false;
        }
        
        fs::copy(checkpointSrc, dbDest, fs::copy_options::recursive);
        
        // Optionally restore WAL files
        if (withWAL) {
            fs::path walArchive = fs::path(backupPath) / "wal_archive";
            if (fs::exists(walArchive)) {
                for (const auto& entry : fs::directory_iterator(walArchive)) {
                    // Extract original filename (after timestamp prefix)
                    std::string archiveName = entry.path().filename().string();
                    size_t dashPos = archiveName.find('-');
                    if (dashPos != std::string::npos) {
                        std::string originalName = archiveName.substr(dashPos + 1);
                        fs::copy_file(entry.path(), dbDest / originalName, fs::copy_options::overwrite_existing);
                    }
                }
            }
        }
        
        // Restart server
        vccdb::HttpServer::Config config;
        config.port = TEST_PORT;
        config.db_path = BASE_DB_PATH;
        config.max_connections = 10;
        config.request_timeout_ms = 5000;
        config.sse_max_events_per_second = 0;

        server_ = std::make_unique<vccdb::HttpServer>(config);
        server_thread_ = std::thread([this]() { server_->run(); });
        
        // Wait for server to restart
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        return true;
    }
};

// Test basic backup and restore cycle
TEST_F(BackupRestoreIntegrationTest, BasicBackupRestore) {
    // Create test data
    auto docIds = createTestDocuments(50);
    ASSERT_EQ(docIds.size(), 50);
    
    // Verify initial data
    ASSERT_TRUE(verifyDocuments(docIds));
    
    // Create backup
    std::string backupPath = manualBackup();
    ASSERT_FALSE(backupPath.empty());
    ASSERT_TRUE(fs::exists(backupPath));
    
    // Verify manifest exists
    fs::path manifestPath = fs::path(backupPath) / "manifest.json";
    ASSERT_TRUE(fs::exists(manifestPath));
    
    // Modify data (add more documents)
    auto newDocs = createTestDocuments(25);
    ASSERT_EQ(newDocs.size(), 25);
    
    // Restore from backup
    ASSERT_TRUE(restoreFromBackup(backupPath));
    
    // Verify original data is restored
    ASSERT_TRUE(verifyDocuments(docIds));
    
    // Verify new documents are gone
    for (const auto& id : newDocs) {
        std::string response = httpGet("/api/collections/" + COLLECTION + "/documents/" + id);
        auto doc = nlohmann::json::parse(response);
        // Document should not exist or return error
        ASSERT_TRUE(doc.contains("error") || !doc.contains("_id"));
    }
}

// Test backup with WAL archiving
TEST_F(BackupRestoreIntegrationTest, BackupWithWALArchive) {
    // Create test data
    auto docIds = createTestDocuments(30);
    ASSERT_EQ(docIds.size(), 30);
    
    // Create backup
    std::string backupPath = manualBackup();
    ASSERT_FALSE(backupPath.empty());
    
    // Archive WAL files
    archiveWALFiles(backupPath);
    
    // Verify WAL archive exists
    fs::path walArchivePath = fs::path(backupPath) / "wal_archive";
    ASSERT_TRUE(fs::exists(walArchivePath));
    
    // Count archived WAL files
    int walFileCount = 0;
    for (const auto& entry : fs::directory_iterator(walArchivePath)) {
        if (entry.path().extension() == ".log") {
            ++walFileCount;
        }
    }
    
    // Should have at least some WAL files
    EXPECT_GT(walFileCount, 0);
    
    // Restore with WAL
    ASSERT_TRUE(restoreFromBackup(backupPath, true));
    
    // Verify data
    ASSERT_TRUE(verifyDocuments(docIds));
}

// Test manifest structure and content
TEST_F(BackupRestoreIntegrationTest, ManifestValidation) {
    // Create test data
    createTestDocuments(10);
    
    // Create backup
    std::string backupPath = manualBackup();
    ASSERT_FALSE(backupPath.empty());
    
    // Read manifest
    fs::path manifestPath = fs::path(backupPath) / "manifest.json";
    ASSERT_TRUE(fs::exists(manifestPath));
    
    std::ifstream manifestFile(manifestPath);
    nlohmann::json manifest;
    manifestFile >> manifest;
    manifestFile.close();
    
    // Validate manifest structure
    ASSERT_TRUE(manifest.contains("timestamp"));
    ASSERT_TRUE(manifest.contains("db_path"));
    ASSERT_TRUE(manifest.contains("backup_type"));
    
    // Validate manifest content
    EXPECT_EQ(manifest["db_path"].get<std::string>(), BASE_DB_PATH);
    EXPECT_EQ(manifest["backup_type"].get<std::string>(), "manual_checkpoint");
    
    // Timestamp should be numeric string
    std::string timestamp = manifest["timestamp"].get<std::string>();
    EXPECT_FALSE(timestamp.empty());
    EXPECT_TRUE(std::all_of(timestamp.begin(), timestamp.end(), ::isdigit));
}

// Test multiple backup generations
TEST_F(BackupRestoreIntegrationTest, MultipleBackupGenerations) {
    // First generation
    auto docs1 = createTestDocuments(10);
    std::string backup1 = manualBackup();
    ASSERT_FALSE(backup1.empty());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Second generation
    auto docs2 = createTestDocuments(10);
    std::string backup2 = manualBackup();
    ASSERT_FALSE(backup2.empty());
    
    // Verify backups are different
    EXPECT_NE(backup1, backup2);
    
    // Restore first generation
    ASSERT_TRUE(restoreFromBackup(backup1));
    ASSERT_TRUE(verifyDocuments(docs1));
    
    // Verify second generation docs are missing
    for (const auto& id : docs2) {
        std::string response = httpGet("/api/collections/" + COLLECTION + "/documents/" + id);
        auto doc = nlohmann::json::parse(response);
        ASSERT_TRUE(doc.contains("error") || !doc.contains("_id"));
    }
    
    // Restore second generation
    ASSERT_TRUE(restoreFromBackup(backup2));
    
    // Now both generations should be present
    ASSERT_TRUE(verifyDocuments(docs1));
    ASSERT_TRUE(verifyDocuments(docs2));
}
