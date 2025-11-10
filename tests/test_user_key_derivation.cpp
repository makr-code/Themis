#include <gtest/gtest.h>
#include "auth/jwt_validator.h"
#include "security/pki_key_provider.h"
#include "security/encryption.h"
#include "security/mock_key_provider.h"
#include "utils/hkdf_helper.h"

using namespace themis;
using namespace themis::auth;

class UserKeyDerivationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test claims
        claims_.sub = "user123";
        claims_.email = "test@example.com";
        claims_.groups = {"hr_team", "finance"};
        claims_.roles = {"employee"};
        claims_.issuer = "https://keycloak.vcc.local/realms/vcc";
        
        // Create test DEK
        dek_.resize(32);
        for (size_t i = 0; i < 32; ++i) {
            dek_[i] = static_cast<uint8_t>(i);
        }
    }
    
    JWTClaims claims_;
    std::vector<uint8_t> dek_;
};

TEST_F(UserKeyDerivationTest, DeriveUserKey_SameUserSameField_ProducesSameKey) {
    auto key1 = JWTValidator::deriveUserKey(dek_, claims_, "email");
    auto key2 = JWTValidator::deriveUserKey(dek_, claims_, "email");
    
    EXPECT_EQ(key1, key2);
}

TEST_F(UserKeyDerivationTest, DeriveUserKey_DifferentFields_ProduceDifferentKeys) {
    auto key_email = JWTValidator::deriveUserKey(dek_, claims_, "email");
    auto key_ssn = JWTValidator::deriveUserKey(dek_, claims_, "ssn");
    
    EXPECT_NE(key_email, key_ssn);
}

TEST_F(UserKeyDerivationTest, DeriveUserKey_DifferentUsers_ProduceDifferentKeys) {
    auto key1 = JWTValidator::deriveUserKey(dek_, claims_, "email");
    
    JWTClaims claims2 = claims_;
    claims2.sub = "user456";
    auto key2 = JWTValidator::deriveUserKey(dek_, claims2, "email");
    
    EXPECT_NE(key1, key2);
}

TEST_F(UserKeyDerivationTest, DeriveUserKey_Returns32Bytes) {
    auto key = JWTValidator::deriveUserKey(dek_, claims_, "salary");
    EXPECT_EQ(32u, key.size());
}

TEST_F(UserKeyDerivationTest, HasAccess_OwnUserId_ReturnsTrue) {
    bool access = JWTValidator::hasAccess(claims_, "user123");
    EXPECT_TRUE(access);
}

TEST_F(UserKeyDerivationTest, HasAccess_DifferentUserId_ReturnsFalse) {
    bool access = JWTValidator::hasAccess(claims_, "user456");
    EXPECT_FALSE(access);
}

TEST_F(UserKeyDerivationTest, HasAccess_UserGroup_ReturnsTrue) {
    bool access = JWTValidator::hasAccess(claims_, "hr_team");
    EXPECT_TRUE(access);
}

TEST_F(UserKeyDerivationTest, HasAccess_NonMemberGroup_ReturnsFalse) {
    bool access = JWTValidator::hasAccess(claims_, "engineering");
    EXPECT_FALSE(access);
}

TEST_F(UserKeyDerivationTest, HasAccess_MultipleGroups_ChecksAll) {
    EXPECT_TRUE(JWTValidator::hasAccess(claims_, "finance"));
    EXPECT_TRUE(JWTValidator::hasAccess(claims_, "hr_team"));
}

TEST_F(UserKeyDerivationTest, EncryptDecrypt_WithDerivedKey_RoundTrip) {
    // Setup
    auto key_provider = std::make_shared<MockKeyProvider>();
    auto field_encryption = std::make_shared<FieldEncryption>(key_provider);
    
    std::string plaintext = "Sensitive HR data: salary=$150000";
    auto derived_key = JWTValidator::deriveUserKey(dek_, claims_, "hr_records.salary");
    
    // Encrypt with user-derived key
    auto blob = field_encryption->encryptWithKey(plaintext, "hr_records.salary", 1, derived_key);
    
    // Decrypt with same key
    auto decrypted = field_encryption->decryptWithKey(blob, derived_key);
    
    EXPECT_EQ(plaintext, decrypted);
}

TEST_F(UserKeyDerivationTest, EncryptDecrypt_DifferentUserKey_Fails) {
    auto key_provider = std::make_shared<MockKeyProvider>();
    auto field_encryption = std::make_shared<FieldEncryption>(key_provider);
    
    std::string plaintext = "User1 secret data";
    
    // User1 encrypts
    auto key_user1 = JWTValidator::deriveUserKey(dek_, claims_, "notes");
    auto blob = field_encryption->encryptWithKey(plaintext, "notes", 1, key_user1);
    
    // User2 tries to decrypt with their key
    JWTClaims claims2 = claims_;
    claims2.sub = "user456";
    auto key_user2 = JWTValidator::deriveUserKey(dek_, claims2, "notes");
    
    // Decryption should fail due to authentication tag mismatch
    EXPECT_THROW(
        field_encryption->decryptWithKey(blob, key_user2),
        std::runtime_error
    );
}

TEST_F(UserKeyDerivationTest, GroupEncryption_MultipleUsersAccess) {
    auto key_provider = std::make_shared<MockKeyProvider>();
    auto field_encryption = std::make_shared<FieldEncryption>(key_provider);
    
    std::string plaintext = "HR Team shared document";
    
    // Use group name as context for key derivation
    std::string group_context = "hr_team";
    std::vector<uint8_t> salt(group_context.begin(), group_context.end());
    auto group_key = themis::utils::HKDFHelper::derive(dek_, salt, "group-field:documents", 32);
    
    // Encrypt with group key
    auto blob = field_encryption->encryptWithKey(plaintext, "hr_docs", 1, group_key);
    
    // User1 (HR member) can decrypt
    EXPECT_TRUE(JWTValidator::hasAccess(claims_, "hr_team"));
    auto decrypted1 = field_encryption->decryptWithKey(blob, group_key);
    EXPECT_EQ(plaintext, decrypted1);
    
    // User2 (also HR member) can decrypt
    JWTClaims claims2 = claims_;
    claims2.sub = "user456";
    claims2.groups = {"hr_team"};
    EXPECT_TRUE(JWTValidator::hasAccess(claims2, "hr_team"));
    auto decrypted2 = field_encryption->decryptWithKey(blob, group_key);
    EXPECT_EQ(plaintext, decrypted2);
    
    // User3 (not HR) cannot access
    JWTClaims claims3 = claims_;
    claims3.sub = "user789";
    claims3.groups = {"engineering"};
    EXPECT_FALSE(JWTValidator::hasAccess(claims3, "hr_team"));
}

TEST_F(UserKeyDerivationTest, FieldContext_SupportsHierarchy) {
    // Test nested field contexts
    auto key1 = JWTValidator::deriveUserKey(dek_, claims_, "users.profile.email");
    auto key2 = JWTValidator::deriveUserKey(dek_, claims_, "users.profile.phone");
    auto key3 = JWTValidator::deriveUserKey(dek_, claims_, "users.billing.credit_card");
    
    // All different
    EXPECT_NE(key1, key2);
    EXPECT_NE(key1, key3);
    EXPECT_NE(key2, key3);
    
    // Deterministic
    auto key1_again = JWTValidator::deriveUserKey(dek_, claims_, "users.profile.email");
    EXPECT_EQ(key1, key1_again);
}

TEST_F(UserKeyDerivationTest, EmptyFieldName_ProducesValidKey) {
    // Edge case: empty field name should still produce a valid key
    auto key = JWTValidator::deriveUserKey(dek_, claims_, "");
    EXPECT_EQ(32u, key.size());
}

TEST_F(UserKeyDerivationTest, LongFieldName_ProducesValidKey) {
    // Edge case: very long field name
    std::string long_name(1000, 'x');
    auto key = JWTValidator::deriveUserKey(dek_, claims_, long_name);
    EXPECT_EQ(32u, key.size());
}

TEST_F(UserKeyDerivationTest, SpecialCharactersInFieldName_ProducesValidKey) {
    // Test various special characters
    auto key1 = JWTValidator::deriveUserKey(dek_, claims_, "field:with:colons");
    auto key2 = JWTValidator::deriveUserKey(dek_, claims_, "field/with/slashes");
    auto key3 = JWTValidator::deriveUserKey(dek_, claims_, "field.with.dots");
    
    EXPECT_EQ(32u, key1.size());
    EXPECT_EQ(32u, key2.size());
    EXPECT_EQ(32u, key3.size());
    
    // All different
    EXPECT_NE(key1, key2);
    EXPECT_NE(key1, key3);
}
