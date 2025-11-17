#include "query/statistical_aggregator.h"
#include <numeric>
#include <cmath>
#include <limits>

namespace themis {
namespace query {

// ============================================================================
// Percentile Calculation
// ============================================================================

nlohmann::json StatisticalAggregator::calculatePercentile(
    std::vector<double> values,
    double percentile
) {
    if (values.empty()) {
        return nullptr;
    }
    
    if (percentile < 0.0 || percentile > 100.0) {
        return nullptr;  // Invalid percentile
    }
    
    // Sort values
    std::sort(values.begin(), values.end());
    
    if (values.size() == 1) {
        return values[0];
    }
    
    // Nearest Rank Method
    // Rank = (percentile / 100) * (N - 1)
    double rank = (percentile / 100.0) * (values.size() - 1);
    size_t lowerIndex = static_cast<size_t>(std::floor(rank));
    size_t upperIndex = static_cast<size_t>(std::ceil(rank));
    
    if (lowerIndex == upperIndex) {
        return values[lowerIndex];
    }
    
    // Linear interpolation
    double weight = rank - lowerIndex;
    double result = values[lowerIndex] * (1.0 - weight) + values[upperIndex] * weight;
    
    return result;
}

nlohmann::json StatisticalAggregator::calculateMedian(std::vector<double> values) {
    return calculatePercentile(std::move(values), 50.0);
}

// ============================================================================
// Standard Deviation & Variance
// ============================================================================

double StatisticalAggregator::calculateMean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / values.size();
}

nlohmann::json StatisticalAggregator::calculateVariance(const std::vector<double>& values) {
    if (values.size() < 2) {
        return nullptr;  // Variance requires at least 2 values
    }
    
    double mean = calculateMean(values);
    
    // Sample variance: sum((x - mean)^2) / (n - 1)
    double sumSquaredDiffs = 0.0;
    for (double val : values) {
        double diff = val - mean;
        sumSquaredDiffs += diff * diff;
    }
    
    return sumSquaredDiffs / (values.size() - 1);
}

nlohmann::json StatisticalAggregator::calculateVariancePop(const std::vector<double>& values) {
    if (values.empty()) {
        return nullptr;
    }
    
    if (values.size() == 1) {
        return 0.0;  // Population variance of single value is 0
    }
    
    double mean = calculateMean(values);
    
    // Population variance: sum((x - mean)^2) / n
    double sumSquaredDiffs = 0.0;
    for (double val : values) {
        double diff = val - mean;
        sumSquaredDiffs += diff * diff;
    }
    
    return sumSquaredDiffs / values.size();
}

nlohmann::json StatisticalAggregator::calculateStdDev(const std::vector<double>& values) {
    auto variance = calculateVariance(values);
    
    if (variance.is_null()) {
        return nullptr;
    }
    
    return std::sqrt(variance.get<double>());
}

nlohmann::json StatisticalAggregator::calculateStdDevPop(const std::vector<double>& values) {
    auto variance = calculateVariancePop(values);
    
    if (variance.is_null()) {
        return nullptr;
    }
    
    return std::sqrt(variance.get<double>());
}

// ============================================================================
// Additional Statistical Measures
// ============================================================================

nlohmann::json StatisticalAggregator::calculateRange(const std::vector<double>& values) {
    if (values.empty()) {
        return nullptr;
    }
    
    auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
    return *maxIt - *minIt;
}

nlohmann::json StatisticalAggregator::calculateIQR(std::vector<double> values) {
    if (values.size() < 4) {
        return nullptr;  // IQR requires at least 4 values
    }
    
    auto q1 = calculatePercentile(values, 25.0);
    auto q3 = calculatePercentile(values, 75.0);
    
    if (q1.is_null() || q3.is_null()) {
        return nullptr;
    }
    
    return q3.get<double>() - q1.get<double>();
}

nlohmann::json StatisticalAggregator::calculateMAD(const std::vector<double>& values) {
    if (values.empty()) {
        return nullptr;
    }
    
    double mean = calculateMean(values);
    
    // Mean Absolute Deviation: sum(|x - mean|) / n
    double sumAbsDiffs = 0.0;
    for (double val : values) {
        sumAbsDiffs += std::abs(val - mean);
    }
    
    return sumAbsDiffs / values.size();
}

// ============================================================================
// Helper Functions
// ============================================================================

std::vector<double> StatisticalAggregator::extractNumericValues(
    const std::vector<nlohmann::json>& jsonValues
) {
    std::vector<double> result;
    result.reserve(jsonValues.size());
    
    for (const auto& val : jsonValues) {
        if (val.is_number()) {
            result.push_back(val.get<double>());
        }
        // Skip non-numeric values (null, string, bool, etc.)
    }
    
    return result;
}

} // namespace query
} // namespace themis
