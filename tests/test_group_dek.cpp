#include <gtest/gtest.h>
#include "security/pki_key_provider.h"
#include "utils/pki_client.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>

using namespace themis::security;
using namespace themis;

class GroupDEKTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test DB
        test_dir_ = std::filesystem::temp_directory_path() / "themis_group_dek_test";
        std::filesystem::create_directories(test_dir_);
        
        // Initialize RocksDB
        RocksDBWrapper::Config db_config;
        db_config.db_path = test_dir_.string();
        db_ = std::make_shared<RocksDBWrapper>(db_config);
        db_->open();
        
        // Create mock PKI client
        themis::utils::PKIConfig pki_config;
        pki_config.service_id = "test-service";
        pki_config.endpoint = "https://localhost:8443/api/v1";
        pki_config.cert_path = "test-cert.pem";
        pki_config.key_path = "test-key.pem";
        pki_ = std::make_shared<themis::utils::VCCPKIClient>(pki_config);
        
        // Create PKIKeyProvider
        provider_ = std::make_unique<PKIKeyProvider>(pki_, db_, "test-service");
    }
    
    void TearDown() override {
        provider_.reset();
        db_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    std::filesystem::path test_dir_;
    std::shared_ptr<RocksDBWrapper> db_;
    std::shared_ptr<themis::utils::VCCPKIClient> pki_;
    std::unique_ptr<PKIKeyProvider> provider_;
};

TEST_F(GroupDEKTest, GetGroupDEKCreatesNewKey) {
    auto dek = provider_->getGroupDEK("hr_team");
    
    EXPECT_EQ(dek.size(), 32);  // AES-256
    EXPECT_GT(provider_->getGroupDEKVersion("hr_team"), 0);
}

TEST_F(GroupDEKTest, GetGroupDEKIsDeterministic) {
    auto dek1 = provider_->getGroupDEK("hr_team");
    auto dek2 = provider_->getGroupDEK("hr_team");
    
    EXPECT_EQ(dek1, dek2);  // Same DEK returned for same group
}

TEST_F(GroupDEKTest, DifferentGroupsHaveDifferentDEKs) {
    auto hr_dek = provider_->getGroupDEK("hr_team");
    auto finance_dek = provider_->getGroupDEK("finance_dept");
    
    EXPECT_NE(hr_dek, finance_dek);
}

TEST_F(GroupDEKTest, GroupDEKPersistsAcrossRestart) {
    auto dek_before = provider_->getGroupDEK("hr_team");
    
    // Simulate restart: recreate provider
    provider_.reset();
    provider_ = std::make_unique<PKIKeyProvider>(pki_, db_, "test-service");
    
    auto dek_after = provider_->getGroupDEK("hr_team");
    
    EXPECT_EQ(dek_before, dek_after);
}

TEST_F(GroupDEKTest, RotateGroupDEKCreatesNewVersion) {
    auto dek_v1 = provider_->getGroupDEK("hr_team");
    auto version_before = provider_->getGroupDEKVersion("hr_team");
    
    auto new_version = provider_->rotateGroupDEK("hr_team");
    
    EXPECT_GT(new_version, version_before);
    
    auto dek_v2 = provider_->getGroupDEK("hr_team");
    
    EXPECT_NE(dek_v1, dek_v2);  // Different keys after rotation
}

TEST_F(GroupDEKTest, ListGroupsReturnsAllGroups) {
    provider_->getGroupDEK("hr_team");
    provider_->getGroupDEK("finance_dept");
    provider_->getGroupDEK("admin");
    
    auto groups = provider_->listGroups();
    
    EXPECT_EQ(groups.size(), 3);
    EXPECT_TRUE(std::find(groups.begin(), groups.end(), "hr_team") != groups.end());
    EXPECT_TRUE(std::find(groups.begin(), groups.end(), "finance_dept") != groups.end());
    EXPECT_TRUE(std::find(groups.begin(), groups.end(), "admin") != groups.end());
}

TEST_F(GroupDEKTest, MultipleGroupsCanCoexist) {
    // Simulate multi-party access scenario
    auto hr_dek = provider_->getGroupDEK("hr_team");
    auto police_dek = provider_->getGroupDEK("police_dept");
    auto court_dek = provider_->getGroupDEK("court_judges");
    
    // Verify all are unique
    EXPECT_NE(hr_dek, police_dek);
    EXPECT_NE(hr_dek, court_dek);
    EXPECT_NE(police_dek, court_dek);
    
    // Verify all are 32 bytes
    EXPECT_EQ(hr_dek.size(), 32);
    EXPECT_EQ(police_dek.size(), 32);
    EXPECT_EQ(court_dek.size(), 32);
}

TEST_F(GroupDEKTest, EncryptDecryptWithGroupDEK) {
    // Get group DEK
    auto group_dek = provider_->getGroupDEK("hr_team");
    
    // Simulate encryption with this DEK (simple XOR for test)
    std::string plaintext = "Sensitive salary data: $150,000";
    std::vector<uint8_t> ciphertext(plaintext.size());
    
    for (size_t i = 0; i < plaintext.size(); ++i) {
        ciphertext[i] = plaintext[i] ^ group_dek[i % group_dek.size()];
    }
    
    // Simulate another user from same group decrypting
    auto same_group_dek = provider_->getGroupDEK("hr_team");
    
    std::string decrypted(ciphertext.size(), '\0');
    for (size_t i = 0; i < ciphertext.size(); ++i) {
        decrypted[i] = ciphertext[i] ^ same_group_dek[i % same_group_dek.size()];
    }
    
    EXPECT_EQ(plaintext, decrypted);
}

TEST_F(GroupDEKTest, NonExistentGroupReturnsZeroVersion) {
    auto version = provider_->getGroupDEKVersion("nonexistent_group");
    EXPECT_EQ(version, 0);
}
