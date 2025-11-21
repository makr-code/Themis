// Test: PostgreSQL Importer Datatype Mapping
// Tests correct mapping of PostgreSQL datatypes to ThemisDB types

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <variant>
#include <optional>

// Mock PostgreSQL importer
class PostgreSQLImporter {
public:
    using Value = std::variant<
        int64_t,
        double,
        std::string,
        std::vector<int64_t>,
        std::vector<std::string>,
        std::monostate  // for NULL
    >;
    
    struct Column {
        std::string name;
        std::string pg_type;
        Value value;
    };
    
    Value map_pg_type(const std::string& pg_type, const std::string& value_str) {
        if (value_str == "NULL") {
            return std::monostate{};
        }
        
        if (pg_type == "integer" || pg_type == "bigint") {
            return static_cast<int64_t>(std::stoll(value_str));
        } else if (pg_type == "real" || pg_type == "double precision") {
            return std::stod(value_str);
        } else if (pg_type == "varchar" || pg_type == "text" || pg_type == "timestamp") {
            return value_str;
        } else if (pg_type == "integer[]") {
            return parse_int_array(value_str);
        } else if (pg_type == "text[]") {
            return parse_text_array(value_str);
        } else if (pg_type == "jsonb") {
            return value_str;  // Store as string
        } else if (pg_type == "geometry") {
            return parse_geometry(value_str);
        }
        
        return std::monostate{};
    }
    
private:
    std::vector<int64_t> parse_int_array(const std::string& str) {
        std::vector<int64_t> result;
        // Simple parser: "{1,2,3}"
        size_t start = str.find('{');
        size_t end = str.find('}');
        if (start == std::string::npos || end == std::string::npos) {
            return result;
        }
        
        std::string content = str.substr(start + 1, end - start - 1);
        size_t pos = 0;
        while (pos < content.size()) {
            size_t comma = content.find(',', pos);
            if (comma == std::string::npos) comma = content.size();
            
            std::string num_str = content.substr(pos, comma - pos);
            if (!num_str.empty()) {
                result.push_back(std::stoll(num_str));
            }
            pos = comma + 1;
        }
        
        return result;
    }
    
    std::vector<std::string> parse_text_array(const std::string& str) {
        std::vector<std::string> result;
        // Simple parser: "{\"a\",\"b\",\"c\"}"
        size_t start = str.find('{');
        size_t end = str.find('}');
        if (start == std::string::npos || end == std::string::npos) {
            return result;
        }
        
        std::string content = str.substr(start + 1, end - start - 1);
        size_t pos = 0;
        while (pos < content.size()) {
            if (content[pos] == '"') {
                pos++;
                size_t quote_end = content.find('"', pos);
                if (quote_end != std::string::npos) {
                    result.push_back(content.substr(pos, quote_end - pos));
                    pos = quote_end + 1;
                }
            }
            pos++;
        }
        
        return result;
    }
    
    std::string parse_geometry(const std::string& wkt) {
        // WKT format: "POINT(1.0 2.0)"
        return wkt;
    }
};

// Test basic datatypes
TEST(PostgreSQLImporterTest, BasicDatatypes) {
    PostgreSQLImporter importer;
    
    // INTEGER
    auto int_val = importer.map_pg_type("integer", "42");
    ASSERT_TRUE(std::holds_alternative<int64_t>(int_val));
    EXPECT_EQ(std::get<int64_t>(int_val), 42);
    
    // BIGINT
    auto bigint_val = importer.map_pg_type("bigint", "9223372036854775807");
    ASSERT_TRUE(std::holds_alternative<int64_t>(bigint_val));
    EXPECT_EQ(std::get<int64_t>(bigint_val), 9223372036854775807LL);
    
    // DOUBLE PRECISION
    auto double_val = importer.map_pg_type("double precision", "3.14159");
    ASSERT_TRUE(std::holds_alternative<double>(double_val));
    EXPECT_NEAR(std::get<double>(double_val), 3.14159, 0.00001);
    
    // VARCHAR
    auto varchar_val = importer.map_pg_type("varchar", "hello world");
    ASSERT_TRUE(std::holds_alternative<std::string>(varchar_val));
    EXPECT_EQ(std::get<std::string>(varchar_val), "hello world");
    
    // TEXT
    auto text_val = importer.map_pg_type("text", "long text content");
    ASSERT_TRUE(std::holds_alternative<std::string>(text_val));
    EXPECT_EQ(std::get<std::string>(text_val), "long text content");
    
    // TIMESTAMP
    auto ts_val = importer.map_pg_type("timestamp", "2024-01-15 10:30:00");
    ASSERT_TRUE(std::holds_alternative<std::string>(ts_val));
    EXPECT_EQ(std::get<std::string>(ts_val), "2024-01-15 10:30:00");
}

// Test array types
TEST(PostgreSQLImporterTest, ArrayTypes) {
    PostgreSQLImporter importer;
    
    // INTEGER[]
    auto int_array = importer.map_pg_type("integer[]", "{1,2,3,4,5}");
    ASSERT_TRUE(std::holds_alternative<std::vector<int64_t>>(int_array));
    auto int_vec = std::get<std::vector<int64_t>>(int_array);
    EXPECT_EQ(int_vec.size(), 5);
    EXPECT_EQ(int_vec[0], 1);
    EXPECT_EQ(int_vec[4], 5);
    
    // TEXT[]
    auto text_array = importer.map_pg_type("text[]", "{\"apple\",\"banana\",\"cherry\"}");
    ASSERT_TRUE(std::holds_alternative<std::vector<std::string>>(text_array));
    auto text_vec = std::get<std::vector<std::string>>(text_array);
    EXPECT_EQ(text_vec.size(), 3);
    EXPECT_EQ(text_vec[0], "apple");
    EXPECT_EQ(text_vec[1], "banana");
    EXPECT_EQ(text_vec[2], "cherry");
}

// Test JSONB type
TEST(PostgreSQLImporterTest, JSONBType) {
    PostgreSQLImporter importer;
    
    std::string json_str = "{\"name\":\"Alice\",\"age\":30,\"city\":\"NYC\"}";
    auto jsonb_val = importer.map_pg_type("jsonb", json_str);
    ASSERT_TRUE(std::holds_alternative<std::string>(jsonb_val));
    EXPECT_EQ(std::get<std::string>(jsonb_val), json_str);
}

// Test PostGIS geometry types
TEST(PostgreSQLImporterTest, PostGISGeometry) {
    PostgreSQLImporter importer;
    
    // POINT
    auto point = importer.map_pg_type("geometry", "POINT(1.0 2.0)");
    ASSERT_TRUE(std::holds_alternative<std::string>(point));
    EXPECT_EQ(std::get<std::string>(point), "POINT(1.0 2.0)");
    
    // LINESTRING
    auto linestring = importer.map_pg_type("geometry", "LINESTRING(0 0, 1 1, 2 2)");
    ASSERT_TRUE(std::holds_alternative<std::string>(linestring));
    EXPECT_EQ(std::get<std::string>(linestring), "LINESTRING(0 0, 1 1, 2 2)");
    
    // POLYGON
    auto polygon = importer.map_pg_type("geometry", "POLYGON((0 0, 4 0, 4 4, 0 4, 0 0))");
    ASSERT_TRUE(std::holds_alternative<std::string>(polygon));
    EXPECT_EQ(std::get<std::string>(polygon), "POLYGON((0 0, 4 0, 4 4, 0 4, 0 0))");
}

// Test NULL value handling
TEST(PostgreSQLImporterTest, NullHandling) {
    PostgreSQLImporter importer;
    
    auto null_int = importer.map_pg_type("integer", "NULL");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(null_int));
    
    auto null_text = importer.map_pg_type("text", "NULL");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(null_text));
    
    auto null_array = importer.map_pg_type("integer[]", "NULL");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(null_array));
}

// Test type conversion edge cases
TEST(PostgreSQLImporterTest, TypeConversionEdgeCases) {
    PostgreSQLImporter importer;
    
    // Negative integers
    auto neg_int = importer.map_pg_type("integer", "-42");
    ASSERT_TRUE(std::holds_alternative<int64_t>(neg_int));
    EXPECT_EQ(std::get<int64_t>(neg_int), -42);
    
    // Scientific notation
    auto sci_double = importer.map_pg_type("double precision", "1.23e-4");
    ASSERT_TRUE(std::holds_alternative<double>(sci_double));
    EXPECT_NEAR(std::get<double>(sci_double), 0.000123, 0.0000001);
    
    // Empty string
    auto empty_str = importer.map_pg_type("text", "");
    ASSERT_TRUE(std::holds_alternative<std::string>(empty_str));
    EXPECT_EQ(std::get<std::string>(empty_str), "");
    
    // Empty array
    auto empty_array = importer.map_pg_type("integer[]", "{}");
    ASSERT_TRUE(std::holds_alternative<std::vector<int64_t>>(empty_array));
    EXPECT_EQ(std::get<std::vector<int64_t>>(empty_array).size(), 0);
}

// Test large object handling
TEST(PostgreSQLImporterTest, LargeObjectHandling) {
    PostgreSQLImporter importer;
    
    // Large text (10KB)
    std::string large_text(10 * 1024, 'A');
    auto large_val = importer.map_pg_type("text", large_text);
    ASSERT_TRUE(std::holds_alternative<std::string>(large_val));
    EXPECT_EQ(std::get<std::string>(large_val).size(), 10 * 1024);
    
    // Large array (1000 elements)
    std::string large_array = "{";
    for (int i = 0; i < 1000; ++i) {
        if (i > 0) large_array += ",";
        large_array += std::to_string(i);
    }
    large_array += "}";
    
    auto large_arr_val = importer.map_pg_type("integer[]", large_array);
    ASSERT_TRUE(std::holds_alternative<std::vector<int64_t>>(large_arr_val));
    EXPECT_EQ(std::get<std::vector<int64_t>>(large_arr_val).size(), 1000);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
