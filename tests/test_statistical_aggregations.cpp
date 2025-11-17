#include <gtest/gtest.h>
#include "query/statistical_aggregator.h"
#include <nlohmann/json.hpp>
#include <cmath>

using namespace themis::query;
using json = nlohmann::json;

class StatisticalAggregatorTest : public ::testing::Test {
protected:
    // Helper: Vergleicht Doubles mit Epsilon
    bool doubleEquals(double a, double b, double epsilon = 0.0001) {
        return std::abs(a - b) < epsilon;
    }
};

// ============================================================================
// PERCENTILE Tests
// ============================================================================

TEST_F(StatisticalAggregatorTest, PercentileBasic) {
    std::vector<double> values = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    
    // 50th percentile (median) = 55
    auto p50 = StatisticalAggregator::calculatePercentile(values, 50.0);
    ASSERT_FALSE(p50.is_null());
    EXPECT_TRUE(doubleEquals(p50.get<double>(), 55.0));
    
    // 25th percentile (Q1) = 32.5
    auto p25 = StatisticalAggregator::calculatePercentile(values, 25.0);
    ASSERT_FALSE(p25.is_null());
    EXPECT_TRUE(doubleEquals(p25.get<double>(), 32.5));
    
    // 75th percentile (Q3) = 77.5
    auto p75 = StatisticalAggregator::calculatePercentile(values, 75.0);
    ASSERT_FALSE(p75.is_null());
    EXPECT_TRUE(doubleEquals(p75.get<double>(), 77.5));
    
    // 95th percentile = 95.5
    auto p95 = StatisticalAggregator::calculatePercentile(values, 95.0);
    ASSERT_FALSE(p95.is_null());
    EXPECT_TRUE(doubleEquals(p95.get<double>(), 95.5));
    
    // 0th percentile (min) = 10
    auto p0 = StatisticalAggregator::calculatePercentile(values, 0.0);
    ASSERT_FALSE(p0.is_null());
    EXPECT_TRUE(doubleEquals(p0.get<double>(), 10.0));
    
    // 100th percentile (max) = 100
    auto p100 = StatisticalAggregator::calculatePercentile(values, 100.0);
    ASSERT_FALSE(p100.is_null());
    EXPECT_TRUE(doubleEquals(p100.get<double>(), 100.0));
}

TEST_F(StatisticalAggregatorTest, PercentileEmpty) {
    std::vector<double> values = {};
    
    auto p50 = StatisticalAggregator::calculatePercentile(values, 50.0);
    EXPECT_TRUE(p50.is_null());
}

TEST_F(StatisticalAggregatorTest, PercentileSingleValue) {
    std::vector<double> values = {42.0};
    
    auto p50 = StatisticalAggregator::calculatePercentile(values, 50.0);
    ASSERT_FALSE(p50.is_null());
    EXPECT_TRUE(doubleEquals(p50.get<double>(), 42.0));
}

TEST_F(StatisticalAggregatorTest, PercentileInvalidRange) {
    std::vector<double> values = {10, 20, 30};
    
    // percentile < 0
    auto pNeg = StatisticalAggregator::calculatePercentile(values, -10.0);
    EXPECT_TRUE(pNeg.is_null());
    
    // percentile > 100
    auto pOver = StatisticalAggregator::calculatePercentile(values, 150.0);
    EXPECT_TRUE(pOver.is_null());
}

// ============================================================================
// MEDIAN Tests
// ============================================================================

TEST_F(StatisticalAggregatorTest, MedianOddCount) {
    std::vector<double> values = {10, 20, 30, 40, 50};
    
    auto median = StatisticalAggregator::calculateMedian(values);
    ASSERT_FALSE(median.is_null());
    EXPECT_TRUE(doubleEquals(median.get<double>(), 30.0));
}

TEST_F(StatisticalAggregatorTest, MedianEvenCount) {
    std::vector<double> values = {10, 20, 30, 40};
    
    auto median = StatisticalAggregator::calculateMedian(values);
    ASSERT_FALSE(median.is_null());
    EXPECT_TRUE(doubleEquals(median.get<double>(), 25.0));  // (20 + 30) / 2
}

TEST_F(StatisticalAggregatorTest, MedianUnsorted) {
    std::vector<double> values = {50, 10, 30, 20, 40};
    
    auto median = StatisticalAggregator::calculateMedian(values);
    ASSERT_FALSE(median.is_null());
    EXPECT_TRUE(doubleEquals(median.get<double>(), 30.0));
}

// ============================================================================
// VARIANCE Tests
// ============================================================================

TEST_F(StatisticalAggregatorTest, VarianceSample) {
    // Values: [2, 4, 4, 4, 5, 5, 7, 9]
    // Mean = 5
    // Sample Variance = sum((x - mean)^2) / (n - 1)
    //                 = (9 + 1 + 1 + 1 + 0 + 0 + 4 + 16) / 7
    //                 = 32 / 7 ≈ 4.571
    std::vector<double> values = {2, 4, 4, 4, 5, 5, 7, 9};
    
    auto variance = StatisticalAggregator::calculateVariance(values);
    ASSERT_FALSE(variance.is_null());
    EXPECT_TRUE(doubleEquals(variance.get<double>(), 32.0 / 7.0, 0.01));
}

TEST_F(StatisticalAggregatorTest, VariancePopulation) {
    // Values: [2, 4, 4, 4, 5, 5, 7, 9]
    // Mean = 5
    // Population Variance = sum((x - mean)^2) / n
    //                     = 32 / 8 = 4.0
    std::vector<double> values = {2, 4, 4, 4, 5, 5, 7, 9};
    
    auto variance = StatisticalAggregator::calculateVariancePop(values);
    ASSERT_FALSE(variance.is_null());
    EXPECT_TRUE(doubleEquals(variance.get<double>(), 4.0));
}

TEST_F(StatisticalAggregatorTest, VarianceInsufficientData) {
    std::vector<double> singleValue = {42.0};
    
    // Sample variance requires n >= 2
    auto varSample = StatisticalAggregator::calculateVariance(singleValue);
    EXPECT_TRUE(varSample.is_null());
    
    // Population variance of single value is 0
    auto varPop = StatisticalAggregator::calculateVariancePop(singleValue);
    ASSERT_FALSE(varPop.is_null());
    EXPECT_TRUE(doubleEquals(varPop.get<double>(), 0.0));
}

TEST_F(StatisticalAggregatorTest, VarianceConstantValues) {
    std::vector<double> values = {5.0, 5.0, 5.0, 5.0};
    
    // All values equal → variance = 0
    auto variance = StatisticalAggregator::calculateVariance(values);
    ASSERT_FALSE(variance.is_null());
    EXPECT_TRUE(doubleEquals(variance.get<double>(), 0.0));
}

// ============================================================================
// STANDARD DEVIATION Tests
// ============================================================================

TEST_F(StatisticalAggregatorTest, StdDevSample) {
    // Values: [2, 4, 4, 4, 5, 5, 7, 9]
    // Sample Variance ≈ 4.571
    // Sample StdDev = sqrt(4.571) ≈ 2.138
    std::vector<double> values = {2, 4, 4, 4, 5, 5, 7, 9};
    
    auto stddev = StatisticalAggregator::calculateStdDev(values);
    ASSERT_FALSE(stddev.is_null());
    EXPECT_TRUE(doubleEquals(stddev.get<double>(), std::sqrt(32.0 / 7.0), 0.01));
}

TEST_F(StatisticalAggregatorTest, StdDevPopulation) {
    // Values: [2, 4, 4, 4, 5, 5, 7, 9]
    // Population Variance = 4.0
    // Population StdDev = sqrt(4.0) = 2.0
    std::vector<double> values = {2, 4, 4, 4, 5, 5, 7, 9};
    
    auto stddev = StatisticalAggregator::calculateStdDevPop(values);
    ASSERT_FALSE(stddev.is_null());
    EXPECT_TRUE(doubleEquals(stddev.get<double>(), 2.0));
}

TEST_F(StatisticalAggregatorTest, StdDevEmpty) {
    std::vector<double> values = {};
    
    auto stddev = StatisticalAggregator::calculateStdDev(values);
    EXPECT_TRUE(stddev.is_null());
}

// ============================================================================
// RANGE Tests
// ============================================================================

TEST_F(StatisticalAggregatorTest, RangeBasic) {
    std::vector<double> values = {10, 50, 20, 80, 30};
    
    // Range = MAX - MIN = 80 - 10 = 70
    auto range = StatisticalAggregator::calculateRange(values);
    ASSERT_FALSE(range.is_null());
    EXPECT_TRUE(doubleEquals(range.get<double>(), 70.0));
}

TEST_F(StatisticalAggregatorTest, RangeEmpty) {
    std::vector<double> values = {};
    
    auto range = StatisticalAggregator::calculateRange(values);
    EXPECT_TRUE(range.is_null());
}

TEST_F(StatisticalAggregatorTest, RangeSingleValue) {
    std::vector<double> values = {42.0};
    
    // Range = 42 - 42 = 0
    auto range = StatisticalAggregator::calculateRange(values);
    ASSERT_FALSE(range.is_null());
    EXPECT_TRUE(doubleEquals(range.get<double>(), 0.0));
}

// ============================================================================
// IQR (Interquartile Range) Tests
// ============================================================================

TEST_F(StatisticalAggregatorTest, IQRBasic) {
    std::vector<double> values = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    
    // Q1 (25th) ≈ 32.5, Q3 (75th) ≈ 77.5
    // IQR = Q3 - Q1 = 77.5 - 32.5 = 45
    auto iqr = StatisticalAggregator::calculateIQR(values);
    ASSERT_FALSE(iqr.is_null());
    EXPECT_TRUE(doubleEquals(iqr.get<double>(), 45.0));
}

TEST_F(StatisticalAggregatorTest, IQRInsufficientData) {
    std::vector<double> values = {10, 20, 30};  // < 4 values
    
    auto iqr = StatisticalAggregator::calculateIQR(values);
    EXPECT_TRUE(iqr.is_null());
}

// ============================================================================
// MAD (Mean Absolute Deviation) Tests
// ============================================================================

TEST_F(StatisticalAggregatorTest, MADBasic) {
    // Values: [2, 4, 6, 8, 10]
    // Mean = 6
    // MAD = (|2-6| + |4-6| + |6-6| + |8-6| + |10-6|) / 5
    //     = (4 + 2 + 0 + 2 + 4) / 5 = 12 / 5 = 2.4
    std::vector<double> values = {2, 4, 6, 8, 10};
    
    auto mad = StatisticalAggregator::calculateMAD(values);
    ASSERT_FALSE(mad.is_null());
    EXPECT_TRUE(doubleEquals(mad.get<double>(), 2.4));
}

TEST_F(StatisticalAggregatorTest, MADConstantValues) {
    std::vector<double> values = {5.0, 5.0, 5.0, 5.0};
    
    // All values equal → MAD = 0
    auto mad = StatisticalAggregator::calculateMAD(values);
    ASSERT_FALSE(mad.is_null());
    EXPECT_TRUE(doubleEquals(mad.get<double>(), 0.0));
}

TEST_F(StatisticalAggregatorTest, MADEmpty) {
    std::vector<double> values = {};
    
    auto mad = StatisticalAggregator::calculateMAD(values);
    EXPECT_TRUE(mad.is_null());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(StatisticalAggregatorTest, NegativeValues) {
    std::vector<double> values = {-10, -5, 0, 5, 10};
    
    auto median = StatisticalAggregator::calculateMedian(values);
    ASSERT_FALSE(median.is_null());
    EXPECT_TRUE(doubleEquals(median.get<double>(), 0.0));
    
    auto variance = StatisticalAggregator::calculateVariance(values);
    ASSERT_FALSE(variance.is_null());
    EXPECT_GT(variance.get<double>(), 0.0);
}

TEST_F(StatisticalAggregatorTest, LargeDataset) {
    std::vector<double> values;
    for (int i = 1; i <= 1000; ++i) {
        values.push_back(static_cast<double>(i));
    }
    
    // Median of 1..1000 = 500.5
    auto median = StatisticalAggregator::calculateMedian(values);
    ASSERT_FALSE(median.is_null());
    EXPECT_TRUE(doubleEquals(median.get<double>(), 500.5));
    
    // Mean = 500.5
    auto variance = StatisticalAggregator::calculateVariance(values);
    ASSERT_FALSE(variance.is_null());
    EXPECT_GT(variance.get<double>(), 0.0);
}

TEST_F(StatisticalAggregatorTest, FloatingPointPrecision) {
    std::vector<double> values = {0.1, 0.2, 0.3, 0.4, 0.5};
    
    auto median = StatisticalAggregator::calculateMedian(values);
    ASSERT_FALSE(median.is_null());
    EXPECT_TRUE(doubleEquals(median.get<double>(), 0.3, 0.0001));
}
