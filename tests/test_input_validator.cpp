#include <gtest/gtest.h>
#include "utils/input_validator.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using namespace themis::utils;
using json = nlohmann::json;

class InputValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary schema directory
        schema_dir_ = "test_schemas_temp";
        std::filesystem::create_directories(schema_dir_);
        
        // Create a minimal AQL request schema stub
        json aql_schema = {
            {"type", "object"},
            {"required", json::array({"query"})},
            {"properties", {
                {"query", {{"type", "string"}}},
                {"bindVars", {{"type", "object"}}}
            }}
        };
        std::ofstream aql_out(schema_dir_ + "/aql_request.json");
        aql_out << aql_schema.dump(2);
        aql_out.close();
        
        // Create a test schema for validation
        json test_schema = {
            {"type", "object"},
            {"required", json::array({"field1", "field2"})},
            {"properties", {
                {"field1", {{"type", "string"}}},
                {"field2", {{"type", "number"}}},
                {"field3", {{"type", "boolean"}}}
            }}
        };
        std::ofstream test_out(schema_dir_ + "/test_schema.json");
        test_out << test_schema.dump(2);
        test_out.close();
        
        validator_ = std::make_unique<InputValidator>(schema_dir_);
    }
    
    void TearDown() override {
        // Clean up temporary schema directory
        std::filesystem::remove_all(schema_dir_);
    }
    
    std::string schema_dir_;
    std::unique_ptr<InputValidator> validator_;
};

// ============================================================================
// Path Segment Validation Tests
// ============================================================================

TEST_F(InputValidatorTest, ValidPathSegment) {
    EXPECT_TRUE(validator_->validatePathSegment("valid_key"));
    EXPECT_TRUE(validator_->validatePathSegment("key123"));
    EXPECT_TRUE(validator_->validatePathSegment("my-key"));
    EXPECT_TRUE(validator_->validatePathSegment("key_with_underscore"));
}

TEST_F(InputValidatorTest, RejectPathTraversal) {
    EXPECT_FALSE(validator_->validatePathSegment("../etc/passwd"));
    EXPECT_FALSE(validator_->validatePathSegment(".."));
    EXPECT_FALSE(validator_->validatePathSegment("key/../other"));
    EXPECT_FALSE(validator_->validatePathSegment("some..key"));
}

TEST_F(InputValidatorTest, RejectPathSeparators) {
    EXPECT_FALSE(validator_->validatePathSegment("path/to/file"));
    EXPECT_FALSE(validator_->validatePathSegment("windows\\path"));
    EXPECT_FALSE(validator_->validatePathSegment("/absolute/path"));
}

TEST_F(InputValidatorTest, RejectEncodedTraversal) {
    EXPECT_FALSE(validator_->validatePathSegment("%2e%2e/etc/passwd"));
    EXPECT_FALSE(validator_->validatePathSegment("key%2e%2e"));
    EXPECT_FALSE(validator_->validatePathSegment("%2E%2E"));
}

TEST_F(InputValidatorTest, RejectControlCharacters) {
    EXPECT_FALSE(validator_->validatePathSegment("key\x00value"));
    EXPECT_FALSE(validator_->validatePathSegment("key\nvalue"));
    EXPECT_FALSE(validator_->validatePathSegment("key\x7fvalue"));
}

TEST_F(InputValidatorTest, RejectEmpty) {
    EXPECT_FALSE(validator_->validatePathSegment(""));
}

TEST_F(InputValidatorTest, RejectTooLong) {
    std::string long_key(2000, 'x');
    EXPECT_FALSE(validator_->validatePathSegment(long_key));
}

// ============================================================================
// AQL Request Validation Tests
// ============================================================================

TEST_F(InputValidatorTest, ValidAqlRequest) {
    json valid = {
        {"query", "FOR doc IN collection RETURN doc"}
    };
    auto err = validator_->validateAqlRequest(valid);
    EXPECT_FALSE(err.has_value());
}

TEST_F(InputValidatorTest, ValidAqlRequestWithBindVars) {
    json valid = {
        {"query", "FOR doc IN collection FILTER doc.x == @value RETURN doc"},
        {"bindVars", {{"value", 42}}}
    };
    auto err = validator_->validateAqlRequest(valid);
    EXPECT_FALSE(err.has_value());
}

TEST_F(InputValidatorTest, RejectNonObjectAql) {
    json invalid = json::array({"invalid"});
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("must be a JSON object"), std::string::npos);
}

TEST_F(InputValidatorTest, RejectMissingQuery) {
    json invalid = {
        {"bindVars", {{"x", 1}}}
    };
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("requires string field 'query'"), std::string::npos);
}

TEST_F(InputValidatorTest, RejectNonStringQuery) {
    json invalid = {
        {"query", 123}
    };
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
}

TEST_F(InputValidatorTest, RejectEmptyQuery) {
    json invalid = {
        {"query", ""}
    };
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("must not be empty"), std::string::npos);
}

TEST_F(InputValidatorTest, RejectTooLargeQuery) {
    std::string huge_query(150000, 'x');
    json invalid = {
        {"query", huge_query}
    };
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("too large"), std::string::npos);
}

TEST_F(InputValidatorTest, RejectQueryWithControlChars) {
    json invalid = {
        {"query", "FOR doc IN collection\x00RETURN doc"}
    };
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("control characters"), std::string::npos);
}

TEST_F(InputValidatorTest, RejectMultipleStatements) {
    json invalid = {
        {"query", "FOR doc IN c1 RETURN doc;; DROP TABLE users"}
    };
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("multiple statement separator"), std::string::npos);
}

TEST_F(InputValidatorTest, RejectForbiddenTokens) {
    std::vector<std::string> forbidden = {
        "DROP TABLE users",
        "TRUNCATE collection",
        "ALTER TABLE x",
        "GRANT ALL",
        "REVOKE SELECT",
        "CREATE TABLE test",
        "INSERT INTO collection",
        "UPDATE collection SET",
        "DELETE FROM collection"
    };
    
    for (const auto& stmt : forbidden) {
        json invalid = {{"query", stmt}};
        auto err = validator_->validateAqlRequest(invalid);
        EXPECT_TRUE(err.has_value()) << "Should reject: " << stmt;
        EXPECT_NE(err->find("forbidden token"), std::string::npos);
    }
}

TEST_F(InputValidatorTest, AllowValidAqlOperators) {
    std::vector<std::string> allowed = {
        "FOR doc IN collection RETURN doc",
        "FOR doc IN c FILTER doc.x > 10 RETURN doc",
        "FOR doc IN c SORT doc.name LIMIT 100 RETURN doc",
        "FOR doc IN c COLLECT x = doc.category RETURN {category: x}",
        "FOR v,e,p IN 1..3 OUTBOUND 'users/123' edges RETURN p"
    };
    
    for (const auto& query : allowed) {
        json valid = {{"query", query}};
        auto err = validator_->validateAqlRequest(valid);
        EXPECT_FALSE(err.has_value()) << "Should allow: " << query;
    }
}

TEST_F(InputValidatorTest, RejectInvalidBindVarsType) {
    json invalid = {
        {"query", "FOR doc IN c FILTER doc.x == @val RETURN doc"},
        {"bindVars", "not_an_object"}
    };
    auto err = validator_->validateAqlRequest(invalid);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("must be an object"), std::string::npos);
}

// ============================================================================
// JSON Schema Stub Validation Tests
// ============================================================================

TEST_F(InputValidatorTest, ValidJsonStub) {
    json valid = {
        {"field1", "string_value"},
        {"field2", 42},
        {"field3", true}
    };
    auto err = validator_->validateJsonStub(valid, "test_schema");
    EXPECT_FALSE(err.has_value());
}

TEST_F(InputValidatorTest, ValidJsonStubMissingOptional) {
    json valid = {
        {"field1", "string_value"},
        {"field2", 123}
        // field3 is optional
    };
    auto err = validator_->validateJsonStub(valid, "test_schema");
    EXPECT_FALSE(err.has_value());
}

TEST_F(InputValidatorTest, RejectMissingRequiredField) {
    json invalid = {
        {"field1", "string_value"}
        // missing field2
    };
    auto err = validator_->validateJsonStub(invalid, "test_schema");
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("missing required field"), std::string::npos);
}

TEST_F(InputValidatorTest, RejectWrongFieldType) {
    json invalid = {
        {"field1", 123}, // should be string
        {"field2", 42}
    };
    auto err = validator_->validateJsonStub(invalid, "test_schema");
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("must be string"), std::string::npos);
}

TEST_F(InputValidatorTest, NonExistentSchema) {
    json any = {{"key", "value"}};
    auto err = validator_->validateJsonStub(any, "nonexistent_schema");
    EXPECT_FALSE(err.has_value()); // No schema = accept
}

TEST_F(InputValidatorTest, ValidateNonObjectPayload) {
    json invalid = json::array({1, 2, 3});
    auto err = validator_->validateJsonStub(invalid, "test_schema");
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(err->find("must be object"), std::string::npos);
}

// ============================================================================
// Sanitization Tests
// ============================================================================

TEST_F(InputValidatorTest, SanitizeForLogs) {
    std::string input = "normal text\x00\x01\x1F" "control" "\x7F" "chars";
    std::string sanitized = validator_->sanitizeForLogs(input);
    
    // Control characters should be removed
    EXPECT_EQ(sanitized, "normal textcontrolchars");
}

TEST_F(InputValidatorTest, SanitizeForLogsTruncates) {
    std::string input(1000, 'x');
    std::string sanitized = validator_->sanitizeForLogs(input, 100);
    
    EXPECT_EQ(sanitized.size(), 100);
}

TEST_F(InputValidatorTest, SanitizeForLogsEmpty) {
    std::string sanitized = validator_->sanitizeForLogs("");
    EXPECT_TRUE(sanitized.empty());
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(InputValidatorTest, SchemaDir) {
    EXPECT_EQ(validator_->schemaDir(), schema_dir_);
}
