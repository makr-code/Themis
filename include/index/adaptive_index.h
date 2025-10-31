#ifndef THEMIS_ADAPTIVE_INDEX_H
#define THEMIS_ADAPTIVE_INDEX_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>
#include "storage/rocksdb_wrapper.h"

namespace themis {

/**
 * @brief Tracks query patterns to identify indexing opportunities
 * 
 * Thread-safe storage of query execution statistics:
 * - Field access frequency
 * - Filter predicates (equality, range, IN)
 * - Join conditions
 * - Query execution times
 */
class QueryPatternTracker {
public:
    struct QueryPattern {
        std::string collection;
        std::string field;
        std::string operation;  // "eq", "range", "in", "join"
        int64_t count = 0;
        int64_t total_time_ms = 0;
        int64_t last_seen_ms = 0;
        
        nlohmann::json toJson() const;
        static QueryPattern fromJson(const nlohmann::json& j);
    };
    
    explicit QueryPatternTracker();
    ~QueryPatternTracker() = default;
    
    /**
     * @brief Record a query pattern observation
     * @param collection Collection/table name
     * @param field Field name used in filter/join
     * @param operation Type of operation (eq, range, in, join)
     * @param execution_time_ms Query execution time
     */
    void recordPattern(const std::string& collection,
                      const std::string& field,
                      const std::string& operation,
                      int64_t execution_time_ms);
    
    /**
     * @brief Get all tracked patterns for a collection
     * @param collection Collection name (empty = all collections)
     * @return Vector of query patterns sorted by frequency
     */
    std::vector<QueryPattern> getPatterns(const std::string& collection = "") const;
    
    /**
     * @brief Get top N most frequent patterns
     * @param limit Maximum number of patterns to return
     * @return Top patterns by count
     */
    std::vector<QueryPattern> getTopPatterns(size_t limit = 10) const;
    
    /**
     * @brief Clear all tracked patterns
     */
    void clear();
    
    /**
     * @brief Get total number of tracked patterns
     */
    size_t size() const;

private:
    mutable std::mutex mutex_;
    // Key: "collection:field:operation"
    std::map<std::string, QueryPattern> patterns_;
    
    std::string makeKey(const std::string& collection,
                       const std::string& field,
                       const std::string& operation) const;
    
    int64_t getCurrentTimeMs() const;
};

/**
 * @brief Analyzes data selectivity to estimate index effectiveness
 * 
 * Performs sampling and statistics to determine:
 * - Cardinality (number of unique values)
 * - Distribution (uniform, skewed, sparse)
 * - Null ratio
 */
class SelectivityAnalyzer {
public:
    struct SelectivityStats {
        std::string collection;
        std::string field;
        int64_t total_documents = 0;
        int64_t unique_values = 0;
        int64_t null_count = 0;
        double selectivity = 0.0;  // unique_values / total_documents
        std::string distribution;   // "uniform", "skewed", "sparse"
        
        nlohmann::json toJson() const;
        static SelectivityStats fromJson(const nlohmann::json& j);
    };
    
    explicit SelectivityAnalyzer(rocksdb::TransactionDB* db);
    ~SelectivityAnalyzer() = default;
    
    /**
     * @brief Analyze selectivity of a field using sampling
     * @param collection Collection name
     * @param field Field name to analyze
     * @param sample_size Number of documents to sample (0 = full scan)
     * @return Selectivity statistics
     */
    SelectivityStats analyze(const std::string& collection,
                            const std::string& field,
                            size_t sample_size = 1000);
    
    /**
     * @brief Estimate index benefit score (0.0 - 1.0)
     * @param stats Selectivity statistics
     * @return Score where 1.0 = highly beneficial, 0.0 = not beneficial
     */
    double calculateIndexBenefit(const SelectivityStats& stats) const;

private:
    rocksdb::TransactionDB* db_;
    
    std::string determineDistribution(const std::map<std::string, int>& value_counts,
                                     int64_t total) const;
};

/**
 * @brief Generates index suggestions based on query patterns and selectivity
 */
class IndexSuggestionEngine {
public:
    struct IndexSuggestion {
        std::string collection;
        std::string field;
        std::string index_type;  // "range", "hash", "composite"
        double score = 0.0;      // 0.0 - 1.0 (higher = more beneficial)
        std::string reason;
        nlohmann::json metadata;
        
        // Estimated impact
        int64_t queries_affected = 0;
        int64_t estimated_speedup_ms = 0;
        
        nlohmann::json toJson() const;
        static IndexSuggestion fromJson(const nlohmann::json& j);
    };
    
    explicit IndexSuggestionEngine(QueryPatternTracker* tracker,
                                  SelectivityAnalyzer* analyzer);
    ~IndexSuggestionEngine() = default;
    
    /**
     * @brief Generate index suggestions
     * @param collection Collection to analyze (empty = all collections)
     * @param min_score Minimum score threshold (0.0 - 1.0)
     * @param limit Maximum number of suggestions
     * @return Suggestions sorted by score (descending)
     */
    std::vector<IndexSuggestion> generateSuggestions(
        const std::string& collection = "",
        double min_score = 0.5,
        size_t limit = 10);
    
    /**
     * @brief Check if an index already exists
     * @param collection Collection name
     * @param field Field name
     * @return True if index exists
     */
    bool indexExists(const std::string& collection,
                    const std::string& field) const;

private:
    QueryPatternTracker* tracker_;
    SelectivityAnalyzer* analyzer_;
    
    double calculateScore(const QueryPatternTracker::QueryPattern& pattern,
                         const SelectivityAnalyzer::SelectivityStats& stats) const;
    
    std::string recommendIndexType(const QueryPatternTracker::QueryPattern& pattern,
                                   const SelectivityAnalyzer::SelectivityStats& stats) const;
    
    std::string generateReason(const QueryPatternTracker::QueryPattern& pattern,
                              const SelectivityAnalyzer::SelectivityStats& stats,
                              const std::string& index_type) const;
};

/**
 * @brief Main facade for adaptive indexing functionality
 */
class AdaptiveIndexManager {
public:
    explicit AdaptiveIndexManager(rocksdb::TransactionDB* db);
    ~AdaptiveIndexManager() = default;
    
    // Component access
    QueryPatternTracker* getPatternTracker() { return &tracker_; }
    SelectivityAnalyzer* getSelectivityAnalyzer() { return &analyzer_; }
    IndexSuggestionEngine* getSuggestionEngine() { return &engine_; }
    
    /**
     * @brief Get index suggestions (convenience method)
     */
    std::vector<IndexSuggestionEngine::IndexSuggestion> getSuggestions(
        const std::string& collection = "",
        double min_score = 0.5,
        size_t limit = 10);
    
    /**
     * @brief Get query patterns (convenience method)
     */
    std::vector<QueryPatternTracker::QueryPattern> getPatterns(
        const std::string& collection = "");

private:
    rocksdb::TransactionDB* db_;
    QueryPatternTracker tracker_;
    SelectivityAnalyzer analyzer_;
    IndexSuggestionEngine engine_;
};

} // namespace themis

#endif // THEMIS_ADAPTIVE_INDEX_H
