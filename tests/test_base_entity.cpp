#include <gtest/gtest.h>
#include "storage/base_entity.h"
#include "storage/key_schema.h"

using namespace themis;

class BaseEntityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }

    void TearDown() override {
        // Cleanup code
    }
};

// ===== Constructor Tests =====

TEST_F(BaseEntityTest, ConstructorWithPK) {
    BaseEntity entity("test_pk");
    EXPECT_EQ(entity.getPrimaryKey(), "test_pk");
    EXPECT_TRUE(entity.isEmpty());
}

TEST_F(BaseEntityTest, SetAndGetPrimaryKey) {
    BaseEntity entity;
    entity.setPrimaryKey("new_pk");
    EXPECT_EQ(entity.getPrimaryKey(), "new_pk");
}

TEST_F(BaseEntityTest, ConstructorWithFields) {
    BaseEntity::FieldMap fields;
    fields["name"] = std::string("Alice");
    fields["age"] = int64_t(30);
    fields["active"] = true;
    
    BaseEntity entity("user_1", fields);
    EXPECT_EQ(entity.getPrimaryKey(), "user_1");
    EXPECT_FALSE(entity.isEmpty());
}

// ===== Field Access Tests =====

TEST_F(BaseEntityTest, SetAndGetStringField) {
    BaseEntity entity("test");
    entity.setField("name", std::string("Bob"));
    
    auto value = entity.getFieldAsString("name");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "Bob");
}

TEST_F(BaseEntityTest, SetAndGetIntField) {
    BaseEntity entity("test");
    entity.setField("age", int64_t(25));
    
    auto value = entity.getFieldAsInt("age");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 25);
}

TEST_F(BaseEntityTest, SetAndGetDoubleField) {
    BaseEntity entity("test");
    entity.setField("score", 95.5);
    
    auto value = entity.getFieldAsDouble("score");
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 95.5);
}

TEST_F(BaseEntityTest, SetAndGetBoolField) {
    BaseEntity entity("test");
    entity.setField("active", true);
    
    auto value = entity.getFieldAsBool("active");
    ASSERT_TRUE(value.has_value());
    EXPECT_TRUE(*value);
}

TEST_F(BaseEntityTest, SetAndGetVectorField) {
    BaseEntity entity("test");
    std::vector<float> embedding = {0.1f, 0.2f, 0.3f, 0.4f};
    entity.setField("embedding", embedding);
    
    auto value = entity.getFieldAsVector("embedding");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->size(), 4);
    EXPECT_FLOAT_EQ((*value)[0], 0.1f);
    EXPECT_FLOAT_EQ((*value)[3], 0.4f);
}

TEST_F(BaseEntityTest, HasField) {
    BaseEntity entity("test");
    entity.setField("name", std::string("Test"));
    
    EXPECT_TRUE(entity.hasField("name"));
    EXPECT_FALSE(entity.hasField("nonexistent"));
}

TEST_F(BaseEntityTest, GetAllFields) {
    BaseEntity::FieldMap fields;
    fields["name"] = std::string("Alice");
    fields["age"] = int64_t(30);
    fields["score"] = 95.5;
    
    BaseEntity entity("test", fields);
    
    auto all_fields = entity.getAllFields();
    EXPECT_EQ(all_fields.size(), 3);
    EXPECT_TRUE(all_fields.find("name") != all_fields.end());
    EXPECT_TRUE(all_fields.find("age") != all_fields.end());
    EXPECT_TRUE(all_fields.find("score") != all_fields.end());
}

// ===== JSON Tests =====

TEST_F(BaseEntityTest, FromJsonSimple) {
    std::string json = R"({"name":"Alice","age":30,"active":true})";
    BaseEntity entity = BaseEntity::fromJson("user_1", json);
    
    EXPECT_EQ(entity.getPrimaryKey(), "user_1");
    EXPECT_EQ(entity.getFieldAsString("name").value(), "Alice");
    EXPECT_EQ(entity.getFieldAsInt("age").value(), 30);
    EXPECT_TRUE(entity.getFieldAsBool("active").value());
}

TEST_F(BaseEntityTest, FromJsonWithVector) {
    std::string json = R"({"id":"doc_1","embedding":[0.1,0.2,0.3]})";
    BaseEntity entity = BaseEntity::fromJson("doc_1", json);
    
    auto vec = entity.getFieldAsVector("embedding");
    ASSERT_TRUE(vec.has_value());
    EXPECT_EQ(vec->size(), 3);
    EXPECT_FLOAT_EQ((*vec)[0], 0.1f);
    EXPECT_FLOAT_EQ((*vec)[2], 0.3f);
}

TEST_F(BaseEntityTest, ToJson) {
    BaseEntity::FieldMap fields;
    fields["name"] = std::string("Bob");
    fields["age"] = int64_t(25);
    fields["active"] = true;
    
    BaseEntity entity("test", fields);
    std::string json = entity.toJson();
    
    // Check that JSON contains expected fields
    EXPECT_TRUE(json.find("\"name\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"Bob\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"age\"") != std::string::npos);
    EXPECT_TRUE(json.find("25") != std::string::npos);
    EXPECT_TRUE(json.find("\"active\"") != std::string::npos);
    EXPECT_TRUE(json.find("true") != std::string::npos);
}

// ===== Serialization Tests =====

TEST_F(BaseEntityTest, SerializeDeserializeRoundtrip) {
    BaseEntity::FieldMap fields;
    fields["name"] = std::string("Charlie");
    fields["age"] = int64_t(35);
    fields["score"] = 88.5;
    
    BaseEntity entity1("test", fields);
    
    // Serialize
    auto blob = entity1.serialize();
    EXPECT_FALSE(blob.empty());
    
    // Deserialize
    BaseEntity entity2 = BaseEntity::deserialize("test", blob);
    
    EXPECT_EQ(entity2.getFieldAsString("name").value(), "Charlie");
    EXPECT_EQ(entity2.getFieldAsInt("age").value(), 35);
    EXPECT_DOUBLE_EQ(entity2.getFieldAsDouble("score").value(), 88.5);
}

TEST_F(BaseEntityTest, SerializeWithVector) {
    BaseEntity entity("test");
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    entity.setField("embedding", embedding);
    entity.setField("name", std::string("VectorDoc"));
    
    auto blob = entity.serialize();
    BaseEntity entity2 = BaseEntity::deserialize("test", blob);
    
    auto vec = entity2.getFieldAsVector("embedding");
    ASSERT_TRUE(vec.has_value());
    EXPECT_EQ(vec->size(), 5);
    EXPECT_FLOAT_EQ((*vec)[4], 5.0f);
}

// ===== Index Support Tests =====

TEST_F(BaseEntityTest, ExtractField) {
    BaseEntity::FieldMap fields;
    fields["name"] = std::string("Dave");
    fields["email"] = std::string("dave@example.com");
    
    BaseEntity entity("test", fields);
    
    auto name = entity.extractField("name");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Dave");
    
    auto email = entity.extractField("email");
    ASSERT_TRUE(email.has_value());
    EXPECT_EQ(*email, "dave@example.com");
}

TEST_F(BaseEntityTest, ExtractAllFields) {
    BaseEntity::FieldMap fields;
    fields["name"] = std::string("Eve");
    fields["age"] = int64_t(28);
    fields["city"] = std::string("Berlin");
    
    BaseEntity entity("test", fields);
    
    auto attrs = entity.extractAllFields();
    EXPECT_EQ(attrs.size(), 3);
    EXPECT_EQ(attrs["name"], "Eve");
    EXPECT_EQ(attrs["age"], "28");
    EXPECT_EQ(attrs["city"], "Berlin");
}

TEST_F(BaseEntityTest, ExtractFieldsWithPrefix) {
    BaseEntity::FieldMap fields;
    fields["meta_author"] = std::string("Alice");
    fields["meta_date"] = std::string("2025-10-26");
    fields["title"] = std::string("Test");
    
    BaseEntity entity("test", fields);
    
    auto meta_fields = entity.extractFieldsWithPrefix("meta_");
    EXPECT_EQ(meta_fields.size(), 2);
    EXPECT_EQ(meta_fields["meta_author"], "Alice");
    EXPECT_EQ(meta_fields["meta_date"], "2025-10-26");
}

// ===== Clear and Empty Tests =====

TEST_F(BaseEntityTest, Clear) {
    BaseEntity entity("test");
    entity.setField("name", std::string("Test"));
    
    EXPECT_FALSE(entity.isEmpty());
    
    entity.clear();
    EXPECT_TRUE(entity.isEmpty());
    EXPECT_TRUE(entity.getPrimaryKey().empty());
}

TEST_F(BaseEntityTest, BlobOperations) {
    BaseEntity entity("test");
    
    std::vector<uint8_t> test_blob = {1, 2, 3, 4, 5};
    entity.setBlob(test_blob);
    
    const auto& blob = entity.getBlob();
    EXPECT_EQ(blob.size(), 5);
    EXPECT_EQ(blob[0], 1);
    EXPECT_EQ(blob[4], 5);
}
