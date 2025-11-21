// Test: Gorilla Codec Edge Cases
// Validates Gorilla compression handles special values and edge cases correctly

#include <gtest/gtest.h>
#include "timeseries/gorilla.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace themis::timeseries;

class GorillaCodecEdgeCasesTest : public ::testing::Test {
protected:
    // Helper to compress and decompress, returning decompressed values
    std::vector<std::pair<int64_t, double>> compressAndDecompress(
        const std::vector<std::pair<int64_t, double>>& input) {
        
        GorillaEncoder encoder;
        for (const auto& [timestamp, value] : input) {
            encoder.addPoint(timestamp, value);
        }
        
        auto compressed = encoder.finish();
        
        GorillaDecoder decoder(compressed);
        std::vector<std::pair<int64_t, double>> output;
        
        while (decoder.hasNext()) {
            output.push_back(decoder.next());
        }
        
        return output;
    }
};

// ===== Special Value Tests =====

TEST_F(GorillaCodecEdgeCasesTest, HandlesZeroValues) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    for (int i = 0; i < 100; i++) {
        input.push_back({timestamp + i * 1000, 0.0});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_EQ(output[i].second, 0.0);
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesNegativeValues) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    for (int i = 0; i < 50; i++) {
        input.push_back({timestamp + i * 1000, -static_cast<double>(i)});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesInfinity) {
    std::vector<std::pair<int64_t, double>> input = {
        {1700000000000, std::numeric_limits<double>::infinity()},
        {1700000001000, -std::numeric_limits<double>::infinity()},
        {1700000002000, std::numeric_limits<double>::infinity()},
        {1700000003000, 123.45},
        {1700000004000, std::numeric_limits<double>::infinity()}
    };
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        if (std::isinf(input[i].second)) {
            EXPECT_TRUE(std::isinf(output[i].second));
            EXPECT_EQ(std::signbit(input[i].second), std::signbit(output[i].second));
        } else {
            EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
        }
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesNaN) {
    std::vector<std::pair<int64_t, double>> input = {
        {1700000000000, 100.0},
        {1700000001000, std::numeric_limits<double>::quiet_NaN()},
        {1700000002000, 200.0},
        {1700000003000, std::numeric_limits<double>::signaling_NaN()},
        {1700000004000, 300.0}
    };
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        if (std::isnan(input[i].second)) {
            EXPECT_TRUE(std::isnan(output[i].second)) 
                << "NaN not preserved at index " << i;
        } else {
            EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
        }
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesVerySmallValues) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    for (int i = 0; i < 50; i++) {
        input.push_back({timestamp + i * 1000, std::numeric_limits<double>::min() * i});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesVeryLargeValues) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    for (int i = 1; i <= 50; i++) {
        input.push_back({timestamp + i * 1000, std::numeric_limits<double>::max() / i});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

// ===== Timestamp Edge Cases =====

TEST_F(GorillaCodecEdgeCasesTest, HandlesOutOfOrderTimestamps) {
    // Note: Gorilla typically expects ordered timestamps
    // This tests behavior when that assumption is violated
    std::vector<std::pair<int64_t, double>> input = {
        {1700000000000, 1.0},
        {1700000001000, 2.0},
        {1700000000500, 1.5},  // Out of order
        {1700000002000, 3.0}
    };
    
    GorillaEncoder encoder;
    for (const auto& [timestamp, value] : input) {
        encoder.addPoint(timestamp, value);
    }
    
    auto compressed = encoder.finish();
    EXPECT_GT(compressed.size(), 0);
    
    // Decoder should still work, though ordering may not be preserved
    GorillaDecoder decoder(compressed);
    int count = 0;
    while (decoder.hasNext()) {
        decoder.next();
        count++;
    }
    
    EXPECT_EQ(count, 4);
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesDuplicateTimestamps) {
    std::vector<std::pair<int64_t, double>> input = {
        {1700000000000, 1.0},
        {1700000000000, 2.0},  // Duplicate timestamp, different value
        {1700000001000, 3.0},
        {1700000001000, 4.0}   // Another duplicate
    };
    
    GorillaEncoder encoder;
    for (const auto& [timestamp, value] : input) {
        encoder.addPoint(timestamp, value);
    }
    
    auto compressed = encoder.finish();
    
    GorillaDecoder decoder(compressed);
    std::vector<std::pair<int64_t, double>> output;
    
    while (decoder.hasNext()) {
        output.push_back(decoder.next());
    }
    
    // Should preserve all points
    EXPECT_EQ(output.size(), input.size());
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesLargeTimestampGaps) {
    std::vector<std::pair<int64_t, double>> input = {
        {1000000000000, 1.0},
        {1000000001000, 2.0},
        {2000000000000, 3.0},  // 1 billion ms gap (11+ days)
        {2000000001000, 4.0}
    };
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesRegularIntervals) {
    // Most common case - regular 1 second intervals
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    for (int i = 0; i < 1000; i++) {
        input.push_back({timestamp + i * 1000, static_cast<double>(i)});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

// ===== Value Pattern Edge Cases =====

TEST_F(GorillaCodecEdgeCasesTest, HandlesConstantValues) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    const double constant_value = 42.42;
    
    for (int i = 0; i < 100; i++) {
        input.push_back({timestamp + i * 1000, constant_value});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, constant_value);
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesAlternatingValues) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    for (int i = 0; i < 100; i++) {
        double value = (i % 2 == 0) ? 1.0 : -1.0;
        input.push_back({timestamp + i * 1000, value});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesRandomJumps) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    std::vector<double> values = {1.0, 1000.0, 0.001, -500.0, 0.0, 999999.9, -0.00001};
    
    for (int i = 0; i < 100; i++) {
        double value = values[i % values.size()];
        input.push_back({timestamp + i * 1000, value});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

// ===== Empty and Single Point Tests =====

TEST_F(GorillaCodecEdgeCasesTest, HandlesEmptyInput) {
    GorillaEncoder encoder;
    auto compressed = encoder.finish();
    
    GorillaDecoder decoder(compressed);
    EXPECT_FALSE(decoder.hasNext());
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesSinglePoint) {
    std::vector<std::pair<int64_t, double>> input = {
        {1700000000000, 42.0}
    };
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), 1);
    EXPECT_EQ(output[0].first, input[0].first);
    EXPECT_DOUBLE_EQ(output[0].second, input[0].second);
}

TEST_F(GorillaCodecEdgeCasesTest, HandlesTwoPoints) {
    std::vector<std::pair<int64_t, double>> input = {
        {1700000000000, 10.0},
        {1700000001000, 20.0}
    };
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), 2);
    for (size_t i = 0; i < 2; i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

// ===== Precision Tests =====

TEST_F(GorillaCodecEdgeCasesTest, PreservesDoublePrecision) {
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    
    // Values that test precision
    std::vector<double> precise_values = {
        3.141592653589793,
        2.718281828459045,
        1.618033988749895,
        0.123456789012345,
        123456789.987654321
    };
    
    for (size_t i = 0; i < precise_values.size(); i++) {
        input.push_back({timestamp + i * 1000, precise_values[i]});
    }
    
    auto output = compressAndDecompress(input);
    
    ASSERT_EQ(output.size(), input.size());
    for (size_t i = 0; i < output.size(); i++) {
        EXPECT_EQ(output[i].first, input[i].first);
        EXPECT_DOUBLE_EQ(output[i].second, input[i].second);
    }
}

// ===== Compression Ratio Tests =====

TEST_F(GorillaCodecEdgeCasesTest, CompressesRealisticSensorData) {
    // Simulate realistic temperature sensor data
    std::vector<std::pair<int64_t, double>> input;
    
    int64_t timestamp = 1700000000000;
    double temperature = 20.0; // Start at 20°C
    
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.1); // ±0.1°C noise
    
    for (int i = 0; i < 1000; i++) {
        temperature += noise(rng);
        input.push_back({timestamp + i * 1000, temperature});
    }
    
    GorillaEncoder encoder;
    for (const auto& [ts, val] : input) {
        encoder.addPoint(ts, val);
    }
    
    auto compressed = encoder.finish();
    
    size_t raw_size = input.size() * (sizeof(int64_t) + sizeof(double));
    size_t compressed_size = compressed.size();
    double compression_ratio = static_cast<double>(raw_size) / compressed_size;
    
    // Gorilla should achieve good compression on this type of data
    EXPECT_GT(compression_ratio, 2.0) 
        << "Compression ratio should be > 2.0 for realistic sensor data";
    
    // Verify decompression works
    GorillaDecoder decoder(compressed);
    int count = 0;
    while (decoder.hasNext()) {
        decoder.next();
        count++;
    }
    
    EXPECT_EQ(count, 1000);
}
