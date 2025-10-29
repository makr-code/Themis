#include <gtest/gtest.h>
#include "utils/cursor.h"
#include <nlohmann/json.hpp>

using namespace themis::utils;

class CursorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CursorTest, EncodeDecodeRoundtrip) {
    std::string pk = "users:alice123";
    std::string collection = "users";
    
    std::string token = Cursor::encode(pk, collection);
    
    EXPECT_FALSE(token.empty());
    
    auto decoded = Cursor::decode(token);
    ASSERT_TRUE(decoded.has_value());
    
    auto [decoded_pk, decoded_collection] = *decoded;
    EXPECT_EQ(decoded_pk, pk);
    EXPECT_EQ(decoded_collection, collection);
}

TEST_F(CursorTest, EncodeSpecialCharacters) {
    std::string pk = "products:item-123/special#chars";
    std::string collection = "products";
    
    std::string token = Cursor::encode(pk, collection);
    auto decoded = Cursor::decode(token);
    
    ASSERT_TRUE(decoded.has_value());
    auto [decoded_pk, decoded_collection] = *decoded;
    EXPECT_EQ(decoded_pk, pk);
    EXPECT_EQ(decoded_collection, collection);
}

TEST_F(CursorTest, DecodeInvalidToken) {
    std::string invalid_token = "this-is-not-valid-base64!!!";
    
    auto decoded = Cursor::decode(invalid_token);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(CursorTest, DecodeEmptyToken) {
    auto decoded = Cursor::decode("");
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(CursorTest, DecodeMalformedJSON) {
    // Valid base64 but invalid JSON content
    std::string malformed = Cursor::encode("pk", "coll");
    // Corrupt the base64
    if (!malformed.empty()) {
        malformed[0] = 'X';
    }
    
    auto decoded = Cursor::decode(malformed);
    // May or may not decode depending on corruption - just verify it doesn't crash
    // In most cases, this will fail gracefully
}

TEST_F(CursorTest, PaginatedResponseJSON) {
    PaginatedResponse response;
    response.items = nlohmann::json::array({
        {{"name", "Alice"}, {"age", 30}},
        {{"name", "Bob"}, {"age", 25}}
    });
    response.has_more = true;
    response.next_cursor = "abc123";
    response.batch_size = 2;
    
    auto json = response.toJSON();
    
    EXPECT_EQ(json["items"].size(), 2);
    EXPECT_TRUE(json["has_more"].get<bool>());
    EXPECT_EQ(json["next_cursor"].get<std::string>(), "abc123");
    EXPECT_EQ(json["batch_size"].get<size_t>(), 2);
}

TEST_F(CursorTest, PaginatedResponseNoMoreResults) {
    PaginatedResponse response;
    response.items = nlohmann::json::array({
        {{"name", "Charlie"}, {"age", 35}}
    });
    response.has_more = false;
    response.next_cursor = "";
    response.batch_size = 1;
    
    auto json = response.toJSON();
    
    EXPECT_EQ(json["items"].size(), 1);
    EXPECT_FALSE(json["has_more"].get<bool>());
    EXPECT_FALSE(json.contains("next_cursor")); // Should not include empty cursor
    EXPECT_EQ(json["batch_size"].get<size_t>(), 1);
}

TEST_F(CursorTest, EncodeDifferentCollections) {
    std::string pk1 = "item:1";
    std::string pk2 = "item:2";
    
    std::string token1 = Cursor::encode(pk1, "collection_a");
    std::string token2 = Cursor::encode(pk2, "collection_b");
    
    EXPECT_NE(token1, token2); // Different collections should produce different tokens
    
    auto decoded1 = Cursor::decode(token1);
    auto decoded2 = Cursor::decode(token2);
    
    ASSERT_TRUE(decoded1.has_value());
    ASSERT_TRUE(decoded2.has_value());
    
    EXPECT_EQ(decoded1->second, "collection_a");
    EXPECT_EQ(decoded2->second, "collection_b");
}
