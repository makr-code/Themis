#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace themis {
namespace query {

/**
 * @brief Advanced Statistical Aggregation Functions für AQL
 * 
 * Erweitert die Standard-Aggregationen (SUM, AVG, MIN, MAX, COUNT) um:
 * - PERCENTILE(expr, p): p-th Percentile (p = 0..100)
 * - MEDIAN(expr): 50th Percentile
 * - STDDEV(expr): Standard Deviation (Sample)
 * - STDDEV_POP(expr): Standard Deviation (Population)
 * - VARIANCE(expr): Variance (Sample)
 * - VAR_POP(expr): Variance (Population)
 * 
 * Beispiel AQL:
 * FOR order IN orders
 *   COLLECT status = order.status
 *   AGGREGATE
 *     median_amount = MEDIAN(order.amount),
 *     p95_amount = PERCENTILE(order.amount, 95),
 *     stddev_amount = STDDEV(order.amount),
 *     variance_amount = VARIANCE(order.amount)
 *   RETURN {status, median_amount, p95_amount, stddev_amount, variance_amount}
 */

/**
 * @brief Statistical Aggregator
 */
class StatisticalAggregator {
public:
    StatisticalAggregator() = default;
    
    /**
     * @brief Berechnet Percentile (Nearest Rank Method)
     * @param values Sorted numeric values
     * @param percentile Percentile (0..100)
     * @return Percentile value oder null wenn empty
     */
    static nlohmann::json calculatePercentile(
        std::vector<double> values,
        double percentile
    );
    
    /**
     * @brief Berechnet Median (50th Percentile)
     * @param values Sorted numeric values
     * @return Median value oder null wenn empty
     */
    static nlohmann::json calculateMedian(std::vector<double> values);
    
    /**
     * @brief Berechnet Sample Standard Deviation
     * @param values Numeric values
     * @return Standard deviation oder null wenn < 2 values
     */
    static nlohmann::json calculateStdDev(const std::vector<double>& values);
    
    /**
     * @brief Berechnet Population Standard Deviation
     * @param values Numeric values
     * @return Population standard deviation oder null wenn empty
     */
    static nlohmann::json calculateStdDevPop(const std::vector<double>& values);
    
    /**
     * @brief Berechnet Sample Variance
     * @param values Numeric values
     * @return Variance oder null wenn < 2 values
     */
    static nlohmann::json calculateVariance(const std::vector<double>& values);
    
    /**
     * @brief Berechnet Population Variance
     * @param values Numeric values
     * @return Population variance oder null wenn empty
     */
    static nlohmann::json calculateVariancePop(const std::vector<double>& values);
    
    /**
     * @brief Berechnet Range (MAX - MIN)
     * @param values Numeric values
     * @return Range oder null wenn empty
     */
    static nlohmann::json calculateRange(const std::vector<double>& values);
    
    /**
     * @brief Berechnet Interquartile Range (IQR = Q3 - Q1)
     * @param values Sorted numeric values
     * @return IQR oder null wenn < 4 values
     */
    static nlohmann::json calculateIQR(std::vector<double> values);
    
    /**
     * @brief Berechnet Mean Absolute Deviation
     * @param values Numeric values
     * @return MAD oder null wenn empty
     */
    static nlohmann::json calculateMAD(const std::vector<double>& values);
    
private:
    /**
     * @brief Berechnet Mean (Average)
     * @param values Numeric values
     * @return Mean oder 0.0 wenn empty
     */
    static double calculateMean(const std::vector<double>& values);
    
    /**
     * @brief Extrahiert numerische Werte aus JSON-Array
     * @param jsonValues JSON values (mixed types)
     * @return Numeric values (non-numeric values skipped)
     */
    static std::vector<double> extractNumericValues(
        const std::vector<nlohmann::json>& jsonValues
    );
};

} // namespace query
} // namespace themis
