#include <gtest/gtest.h>
#include "utils/saga_logger.h"
#include "utils/lek_manager.h"
#include "security/pki_key_provider.h"
#include "security/mock_key_provider.h"
#include "utils/pki_client.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>

using namespace themis;
using namespace themis::utils;
using namespace themis::security;

class SAGALoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean test directories
        std::filesystem::remove_all("data/test_saga");
        std::filesystem::create_directories("data/test_saga");
        
        // Setup components
        key_provider_ = std::make_shared<MockKeyProvider>();
        
        PKIConfig pki_cfg;
        pki_cfg.service_id = "test-saga";
        pki_cfg.endpoint = "https://localhost:8443";
        pki_client_ = std::make_shared<VCCPKIClient>(pki_cfg);
        
        enc_ = std::make_shared<FieldEncryption>(key_provider_);
        
        // Create LEK for testing
        key_provider_->createKey("saga_lek", 32);
    }
    
    void TearDown() override {
        std::filesystem::remove_all("data/test_saga");
    }
    
    std::shared_ptr<MockKeyProvider> key_provider_;
    std::shared_ptr<VCCPKIClient> pki_client_;
    std::shared_ptr<FieldEncryption> enc_;
};

TEST_F(SAGALoggerTest, LogAndFlush_CreatesSignedBatch) {
    SAGALoggerConfig cfg;
    cfg.batch_size = 2;
    cfg.batch_interval = std::chrono::minutes(60);
    cfg.log_path = "data/test_saga/saga.jsonl";
    cfg.signature_path = "data/test_saga/signatures.jsonl";
    cfg.key_id = "saga_lek";
    cfg.encrypt_then_sign = true;
    
    SAGALogger logger(enc_, pki_client_, cfg);
    
    // Log 2 steps to trigger flush
    SAGAStep step1;
    step1.saga_id = "tx_001";
    step1.step_name = "create_user";
    step1.action = "forward";
    step1.entity_id = "user_123";
    step1.payload = {{"email", "test@example.com"}};
    step1.status = "success";
    step1.timestamp = std::chrono::system_clock::now();
    
    SAGAStep step2;
    step2.saga_id = "tx_001";
    step2.step_name = "send_email";
    step2.action = "forward";
    step2.entity_id = "email_456";
    step2.payload = {{"to", "test@example.com"}};
    step2.status = "success";
    step2.timestamp = std::chrono::system_clock::now();
    
    logger.logStep(step1);
    logger.logStep(step2); // Triggers flush at batch_size=2
    
    // Verify files exist
    EXPECT_TRUE(std::filesystem::exists(cfg.log_path));
    EXPECT_TRUE(std::filesystem::exists(cfg.signature_path));
    
    // Verify batch can be listed
    auto batches = logger.listBatches();
    ASSERT_EQ(batches.size(), 1);
}

TEST_F(SAGALoggerTest, VerifyBatch_ValidSignature_ReturnsTrue) {
    SAGALoggerConfig cfg;
    cfg.batch_size = 1;
    cfg.log_path = "data/test_saga/saga2.jsonl";
    cfg.signature_path = "data/test_saga/signatures2.jsonl";
    cfg.key_id = "saga_lek";
    cfg.encrypt_then_sign = true;
    
    SAGALogger logger(enc_, pki_client_, cfg);
    
    SAGAStep step;
    step.saga_id = "tx_verify";
    step.step_name = "test_step";
    step.action = "forward";
    step.entity_id = "entity_789";
    step.payload = {{"data", "test"}};
    step.status = "success";
    step.timestamp = std::chrono::system_clock::now();
    
    logger.logStep(step); // Triggers flush
    
    auto batches = logger.listBatches();
    ASSERT_FALSE(batches.empty());
    
    // Verify first batch
    bool verified = logger.verifyBatch(batches[0]);
    EXPECT_TRUE(verified);
}

TEST_F(SAGALoggerTest, LoadBatch_DecryptsAndReturnsSteps) {
    SAGALoggerConfig cfg;
    cfg.batch_size = 2;
    cfg.log_path = "data/test_saga/saga3.jsonl";
    cfg.signature_path = "data/test_saga/signatures3.jsonl";
    cfg.key_id = "saga_lek";
    cfg.encrypt_then_sign = true;
    
    SAGALogger logger(enc_, pki_client_, cfg);
    
    SAGAStep step1;
    step1.saga_id = "tx_load";
    step1.step_name = "step_a";
    step1.action = "forward";
    step1.entity_id = "ent_a";
    step1.payload = {{"field", "value_a"}};
    step1.status = "success";
    step1.timestamp = std::chrono::system_clock::now();
    
    SAGAStep step2;
    step2.saga_id = "tx_load";
    step2.step_name = "step_b";
    step2.action = "compensate";
    step2.entity_id = "ent_b";
    step2.payload = {{"field", "value_b"}};
    step2.status = "success";
    step2.timestamp = std::chrono::system_clock::now();
    
    logger.logStep(step1);
    logger.logStep(step2);
    
    auto batches = logger.listBatches();
    ASSERT_FALSE(batches.empty());
    
    // Load and verify decryption
    auto loaded_steps = logger.loadBatch(batches[0]);
    ASSERT_EQ(loaded_steps.size(), 2);
    
    EXPECT_EQ(loaded_steps[0].saga_id, "tx_load");
    EXPECT_EQ(loaded_steps[0].step_name, "step_a");
    EXPECT_EQ(loaded_steps[0].action, "forward");
    
    EXPECT_EQ(loaded_steps[1].saga_id, "tx_load");
    EXPECT_EQ(loaded_steps[1].step_name, "step_b");
    EXPECT_EQ(loaded_steps[1].action, "compensate");
}

TEST_F(SAGALoggerTest, Flush_EmptyBuffer_DoesNothing) {
    SAGALoggerConfig cfg;
    cfg.batch_size = 100;
    cfg.log_path = "data/test_saga/saga4.jsonl";
    cfg.signature_path = "data/test_saga/signatures4.jsonl";
    cfg.key_id = "saga_lek";
    
    SAGALogger logger(enc_, pki_client_, cfg);
    
    logger.flush(); // Should not crash or create files
    
    auto batches = logger.listBatches();
    EXPECT_TRUE(batches.empty());
}
