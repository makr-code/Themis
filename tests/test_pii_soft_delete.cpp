#include <gtest/gtest.h>
#include "utils/pii_pseudonymizer.h"
#include "utils/pii_detector.h"
#include "security/mock_key_provider.h"
#include "security/encryption.h"
#include "storage/rocksdb_wrapper.h"

using namespace themis;
using namespace themis::utils;

class PIISoftDeleteTest : public ::testing::Test {
protected:
    std::shared_ptr<RocksDBWrapper> storage_;
    std::shared_ptr<MockKeyProvider> key_provider_;
    std::shared_ptr<FieldEncryption> enc_;
    std::shared_ptr<PIIDetector> detector_;
    std::shared_ptr<AuditLogger> audit_; // optional

    void SetUp() override {
        const std::string db_path = "data/pii_soft_delete_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
        RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 32; cfg.block_cache_size_mb = 64;
        storage_ = std::make_shared<RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());
        key_provider_ = std::make_shared<MockKeyProvider>();
        key_provider_->createKey("default", 1);
        enc_ = std::make_shared<FieldEncryption>(key_provider_);
        detector_ = std::make_shared<PIIDetector>();
    }

    void TearDown() override {
        if (storage_) storage_->close();
        const std::string db_path = "data/pii_soft_delete_test";
        if (std::filesystem::exists(db_path)) std::filesystem::remove_all(db_path);
    }
};

TEST_F(PIISoftDeleteTest, SoftDeleteBlocksReveal) {
    PIIPseudonymizer p(storage_, enc_, detector_, audit_);
    // Input JSON with PII
    nlohmann::json in = {
        {"name", "Alice"},
        {"email", "alice@example.com"}
    };
    auto [pseudo, uuids] = p.pseudonymize(in);
    // At least one UUID must be created
    ASSERT_FALSE(uuids.empty());
    const std::string uuid = uuids.front();

    // Reveal works before soft delete
    auto val_before = p.revealPII(uuid, "tester");
    ASSERT_TRUE(val_before.has_value());

    // Soft delete and then reveal must be blocked
    bool updated = p.softDeletePII(uuid, "tester");
    ASSERT_TRUE(updated);

    auto val_after = p.revealPII(uuid, "tester");
    EXPECT_FALSE(val_after.has_value());
}
