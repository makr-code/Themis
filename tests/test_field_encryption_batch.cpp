#include <gtest/gtest.h>
#include "security/encryption.h"
#include "security/mock_key_provider.h"

using namespace themis;

TEST(FieldEncryptionBatch, RoundtripEncryptDecrypt) {
    auto provider = std::make_shared<MockKeyProvider>();
    // Create a deterministic test key (create with bytes) to avoid randomness in CI
    std::vector<uint8_t> key_bytes(32, 0x42);
    provider->createKeyFromBytes("user_pii", key_bytes);

    FieldEncryption enc(provider);

    std::vector<std::pair<std::string,std::string>> items = {
        {"salt-1", "hello world"},
        {"salt-2", "The quick brown fox"},
        {"salt-3", "Lorem ipsum"}
    };

    auto out = enc.encryptEntityBatch(items, "user_pii");
    ASSERT_EQ(out.size(), items.size());

    for (size_t i = 0; i < out.size(); ++i) {
        auto decrypted = enc.decrypt(out[i]);
        EXPECT_EQ(decrypted, items[i].second);
    }
}
