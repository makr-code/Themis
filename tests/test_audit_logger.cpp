#include <gtest/gtest.h>

#include "utils/audit_logger.h"
#include "security/mock_key_provider.h"

#include <fstream>
#include <filesystem>

using namespace themis;
using namespace themis::utils;

class AuditLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        key_provider_ = std::make_shared<MockKeyProvider>();
        // Create default key for saga_log
        key_provider_->createKey("saga_log", 1);
        
        enc_ = std::make_shared<FieldEncryption>(key_provider_);
        
        PKIConfig pki_cfg;
        pki_cfg.service_id = "test";
        pki_ = std::make_shared<VCCPKIClient>(pki_cfg);
        
        log_path_ = "data/logs/test_audit.jsonl";
        std::filesystem::remove(log_path_);
    }
    
    void TearDown() override {
        std::filesystem::remove(log_path_);
    }
    
    std::shared_ptr<MockKeyProvider> key_provider_;
    std::shared_ptr<FieldEncryption> enc_;
    std::shared_ptr<VCCPKIClient> pki_;
    std::string log_path_;
};

TEST_F(AuditLoggerTest, EncryptThenSignFlow) {
    AuditLoggerConfig cfg;
    cfg.enabled = true;
    cfg.encrypt_then_sign = true;
    cfg.log_path = log_path_;
    cfg.key_id = "saga_log";
    
    AuditLogger logger(enc_, pki_, cfg);
    
    nlohmann::json event = {
        {"user", "admin"},
        {"action", "read"},
        {"resource", "/content/doc123"},
        {"result", "success"}
    };
    
    logger.logEvent(event);
    
    // Verify log file exists and has encrypted payload
    ASSERT_TRUE(std::filesystem::exists(log_path_));
    
    std::ifstream ifs(log_path_);
    std::string line;
    ASSERT_TRUE(std::getline(ifs, line));
    
    auto record = nlohmann::json::parse(line);
    EXPECT_TRUE(record.contains("ts"));
    EXPECT_EQ(record["category"], "AUDIT");
    EXPECT_EQ(record["payload"]["type"], "ciphertext");
    EXPECT_TRUE(record["payload"].contains("iv_b64"));
    EXPECT_TRUE(record["payload"].contains("ciphertext_b64"));
    EXPECT_TRUE(record["payload"].contains("tag_b64"));
    EXPECT_TRUE(record["signature"]["ok"]);
    EXPECT_FALSE(record["signature"]["id"].get<std::string>().empty());
}

TEST_F(AuditLoggerTest, PlaintextSignFlow) {
    AuditLoggerConfig cfg;
    cfg.enabled = true;
    cfg.encrypt_then_sign = false; // disable encryption
    cfg.log_path = log_path_;
    
    AuditLogger logger(enc_, pki_, cfg);
    
    nlohmann::json event = {
        {"user", "user1"},
        {"action", "write"},
        {"resource", "/data/file.txt"}
    };
    
    logger.logEvent(event);
    
    // Verify log file exists with plaintext payload
    ASSERT_TRUE(std::filesystem::exists(log_path_));
    
    std::ifstream ifs(log_path_);
    std::string line;
    ASSERT_TRUE(std::getline(ifs, line));
    
    auto record = nlohmann::json::parse(line);
    EXPECT_EQ(record["payload"]["type"], "plaintext");
    EXPECT_TRUE(record["payload"].contains("data_b64"));
    EXPECT_TRUE(record["signature"]["ok"]);
}

TEST_F(AuditLoggerTest, DisabledLogger) {
    AuditLoggerConfig cfg;
    cfg.enabled = false;
    cfg.log_path = log_path_;
    
    AuditLogger logger(enc_, pki_, cfg);
    
    nlohmann::json event = {{"action", "test"}};
    logger.logEvent(event);
    
    // No file should be created
    EXPECT_FALSE(std::filesystem::exists(log_path_));
}

TEST_F(AuditLoggerTest, MultipleEvents) {
    AuditLoggerConfig cfg;
    cfg.enabled = true;
    cfg.encrypt_then_sign = true;
    cfg.log_path = log_path_;
    cfg.key_id = "saga_log";
    
    AuditLogger logger(enc_, pki_, cfg);
    
    for (int i = 0; i < 5; ++i) {
        nlohmann::json event = {
            {"event_id", i},
            {"action", "test_action"}
        };
        logger.logEvent(event);
    }
    
    // Verify 5 lines in log file
    std::ifstream ifs(log_path_);
    int count = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        auto record = nlohmann::json::parse(line);
        EXPECT_TRUE(record.contains("ts"));
        EXPECT_EQ(record["category"], "AUDIT");
        ++count;
    }
    EXPECT_EQ(count, 5);
}
