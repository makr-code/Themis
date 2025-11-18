#include "index/adaptive_index.h"
#include <rocksdb/db.h>
#include <rocksdb/utilities/transaction_db.h>
#include <rocksdb/iterator.h>
#include <algorithm>
#include <set>
#include <cmath>

namespace themis {

// ===== QueryPatternTracker Implementation =====

QueryPatternTracker::QueryPatternTracker() = default;

std::string QueryPatternTracker::makeKey(const std::string& collection,
                                        const std::string& field,
                                        const std::string& operation) const {
    return collection + ":" + field + ":" + operation;
}

int64_t QueryPatternTracker::getCurrentTimeMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void QueryPatternTracker::recordPattern(const std::string& collection,
                                       const std::string& field,
                                       const std::string& operation,
                                       int64_t execution_time_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = makeKey(collection, field, operation);
    auto& pattern = patterns_[key];
    
    pattern.collection = collection;
    pattern.field = field;
    pattern.operation = operation;
    pattern.count++;
    pattern.total_time_ms += execution_time_ms;
    pattern.last_seen_ms = getCurrentTimeMs();
}

std::vector<QueryPatternTracker::QueryPattern> 
QueryPatternTracker::getPatterns(const std::string& collection) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<QueryPattern> result;
    for (const auto& [key, pattern] : patterns_) {
        if (collection.empty() || pattern.collection == collection) {
            result.push_back(pattern);
        }
    }
    
    // Sort by count (descending)
    std::sort(result.begin(), result.end(), 
             [](const QueryPattern& a, const QueryPattern& b) {
                 return a.count > b.count;
             });
    
    return result;
}

std::vector<QueryPatternTracker::QueryPattern> 
QueryPatternTracker::getTopPatterns(size_t limit) const {
    auto patterns = getPatterns("");
    if (patterns.size() > limit) {
        patterns.resize(limit);
    }
    return patterns;
}

void QueryPatternTracker::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    patterns_.clear();
}

size_t QueryPatternTracker::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return patterns_.size();
}

nlohmann::json QueryPatternTracker::QueryPattern::toJson() const {
    return nlohmann::json{
        {"collection", collection},
        {"field", field},
        {"operation", operation},
        {"count", count},
        {"total_time_ms", total_time_ms},
        {"avg_time_ms", count > 0 ? total_time_ms / count : 0},
        {"last_seen_ms", last_seen_ms}
    };
}

QueryPatternTracker::QueryPattern 
QueryPatternTracker::QueryPattern::fromJson(const nlohmann::json& j) {
    QueryPattern p;
    p.collection = j.value("collection", "");
    p.field = j.value("field", "");
    p.operation = j.value("operation", "");
    p.count = j.value("count", 0);
    p.total_time_ms = j.value("total_time_ms", 0);
    p.last_seen_ms = j.value("last_seen_ms", 0);
    return p;
}

// ===== SelectivityAnalyzer Implementation =====

SelectivityAnalyzer::SelectivityAnalyzer(rocksdb::TransactionDB* db)
    : db_(db) {
    if (!db_) {
        throw std::invalid_argument("SelectivityAnalyzer: db cannot be null");
    }
}

SelectivityAnalyzer::SelectivityStats 
SelectivityAnalyzer::analyze(const std::string& collection,
                            const std::string& field,
                            size_t sample_size) {
    SelectivityStats stats;
    stats.collection = collection;
    stats.field = field;
    
    // Build prefix for collection
    std::string prefix = "d:" + collection + ":";
    
    rocksdb::ReadOptions read_opts;
    rocksdb::Iterator* it = db_->NewIterator(read_opts);
    
    std::set<std::string> unique_values;
    std::map<std::string, int> value_counts;
    int64_t total = 0;
    int64_t sampled = 0;
    
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
        if (sample_size > 0 && sampled >= static_cast<int64_t>(sample_size)) {
            break;
        }
        
        try {
            nlohmann::json doc = nlohmann::json::parse(it->value().ToString());
            
            if (doc.contains(field)) {
                std::string value = doc[field].dump();  // Serialize value
                unique_values.insert(value);
                value_counts[value]++;
            } else {
                stats.null_count++;
            }
            
            total++;
            sampled++;
        } catch (const std::exception& e) {
            (void)e;
            // Skip invalid JSON
            continue;
        }
    }
    
    delete it;
    
    stats.total_documents = total;
    stats.unique_values = static_cast<int64_t>(unique_values.size());
    
    if (total > 0) {
        stats.selectivity = static_cast<double>(stats.unique_values) / total;
    }
    
    stats.distribution = determineDistribution(value_counts, total);
    
    return stats;
}

double SelectivityAnalyzer::calculateIndexBenefit(const SelectivityStats& stats) const {
    if (stats.total_documents == 0) {
        return 0.0;
    }
    
    // Factors:
    // 1. Selectivity (higher = better for range/hash index)
    // 2. Null ratio (lower = better)
    // 3. Distribution (uniform > skewed > sparse)
    
    double selectivity_score = stats.selectivity;
    
    double null_ratio = static_cast<double>(stats.null_count) / stats.total_documents;
    double null_score = 1.0 - null_ratio;
    
    double distribution_score = 0.5;
    if (stats.distribution == "uniform") {
        distribution_score = 1.0;
    } else if (stats.distribution == "skewed") {
        distribution_score = 0.7;
    } else if (stats.distribution == "sparse") {
        distribution_score = 0.3;
    }
    
    // Weighted average
    double benefit = (selectivity_score * 0.5) + 
                    (null_score * 0.3) + 
                    (distribution_score * 0.2);
    
    return std::clamp(benefit, 0.0, 1.0);
}

std::string SelectivityAnalyzer::determineDistribution(
    const std::map<std::string, int>& value_counts,
    int64_t total) const {
    
    if (value_counts.empty()) {
        return "sparse";
    }
    
    // Calculate variance
    double mean = static_cast<double>(total) / value_counts.size();
    double variance = 0.0;
    
    for (const auto& [value, count] : value_counts) {
        double diff = count - mean;
        variance += diff * diff;
    }
    variance /= value_counts.size();
    
    double std_dev = std::sqrt(variance);
    double cv = std_dev / mean;  // Coefficient of variation
    
    // Classification based on coefficient of variation
    if (cv < 0.3) {
        return "uniform";
    } else if (cv < 1.0) {
        return "skewed";
    } else {
        return "sparse";
    }
}

nlohmann::json SelectivityAnalyzer::SelectivityStats::toJson() const {
    return nlohmann::json{
        {"collection", collection},
        {"field", field},
        {"total_documents", total_documents},
        {"unique_values", unique_values},
        {"null_count", null_count},
        {"selectivity", selectivity},
        {"distribution", distribution}
    };
}

SelectivityAnalyzer::SelectivityStats 
SelectivityAnalyzer::SelectivityStats::fromJson(const nlohmann::json& j) {
    SelectivityStats s;
    s.collection = j.value("collection", "");
    s.field = j.value("field", "");
    s.total_documents = j.value("total_documents", 0);
    s.unique_values = j.value("unique_values", 0);
    s.null_count = j.value("null_count", 0);
    s.selectivity = j.value("selectivity", 0.0);
    s.distribution = j.value("distribution", "");
    return s;
}

// ===== IndexSuggestionEngine Implementation =====

IndexSuggestionEngine::IndexSuggestionEngine(QueryPatternTracker* tracker,
                                            SelectivityAnalyzer* analyzer)
    : tracker_(tracker), analyzer_(analyzer) {
    if (!tracker_) {
        throw std::invalid_argument("IndexSuggestionEngine: tracker cannot be null");
    }
    if (!analyzer_) {
        throw std::invalid_argument("IndexSuggestionEngine: analyzer cannot be null");
    }
}

std::vector<IndexSuggestionEngine::IndexSuggestion> 
IndexSuggestionEngine::generateSuggestions(const std::string& collection,
                                          double min_score,
                                          size_t limit) {
    auto patterns = tracker_->getPatterns(collection);
    std::vector<IndexSuggestion> suggestions;
    
    for (const auto& pattern : patterns) {
        // Skip if index already exists
        if (indexExists(pattern.collection, pattern.field)) {
            continue;
        }
        
        // Analyze selectivity
        auto stats = analyzer_->analyze(pattern.collection, pattern.field, 1000);
        
        // Calculate score
        double score = calculateScore(pattern, stats);
        
        if (score < min_score) {
            continue;
        }
        
        IndexSuggestion suggestion;
        suggestion.collection = pattern.collection;
        suggestion.field = pattern.field;
        suggestion.index_type = recommendIndexType(pattern, stats);
        suggestion.score = score;
        suggestion.reason = generateReason(pattern, stats, suggestion.index_type);
        suggestion.queries_affected = static_cast<int64_t>(pattern.count);
        suggestion.estimated_speedup_ms = static_cast<int64_t>(pattern.total_time_ms * 0.5);  // Estimate 50% speedup
        
        suggestion.metadata = nlohmann::json{
            {"pattern", pattern.toJson()},
            {"selectivity", stats.toJson()}
        };
        
        suggestions.push_back(suggestion);
    }
    
    // Sort by score (descending)
    std::sort(suggestions.begin(), suggestions.end(),
             [](const IndexSuggestion& a, const IndexSuggestion& b) {
                 return a.score > b.score;
             });
    
    if (suggestions.size() > limit) {
        suggestions.resize(limit);
    }
    
    return suggestions;
}

bool IndexSuggestionEngine::indexExists(const std::string& collection,
                                       const std::string& field) const {
    (void)collection; (void)field;
    // TODO: Check actual index registry
    // For now, assume no indexes exist (always suggest)
    return false;
}

double IndexSuggestionEngine::calculateScore(
    const QueryPatternTracker::QueryPattern& pattern,
    const SelectivityAnalyzer::SelectivityStats& stats) const {
    
    // Factors:
    // 1. Query frequency (higher = better)
    // 2. Average query time (higher = more benefit)
    // 3. Selectivity benefit (from analyzer)
    
    // Normalize frequency (log scale to prevent dominance)
    double freq_score = std::log10(pattern.count + 1) / 5.0;  // Cap at ~100k queries
    freq_score = std::clamp(freq_score, 0.0, 1.0);
    
    // Normalize avg time (assume 100ms = high)
    double avg_time = pattern.count > 0 ? 
        static_cast<double>(pattern.total_time_ms) / pattern.count : 0.0;
    double time_score = std::min(avg_time / 100.0, 1.0);
    
    // Selectivity benefit
    double selectivity_score = analyzer_->calculateIndexBenefit(stats);
    
    // Weighted average
    double score = (freq_score * 0.4) + 
                  (time_score * 0.3) + 
                  (selectivity_score * 0.3);
    
    return std::clamp(score, 0.0, 1.0);
}

std::string IndexSuggestionEngine::recommendIndexType(
    const QueryPatternTracker::QueryPattern& pattern,
    const SelectivityAnalyzer::SelectivityStats& stats) const {
    
    if (pattern.operation == "range") {
        return "range";
    } else if (pattern.operation == "eq") {
        // High selectivity = hash, low selectivity = range
        return stats.selectivity > 0.5 ? "hash" : "range";
    } else if (pattern.operation == "in") {
        return "hash";
    } else if (pattern.operation == "join") {
        return "hash";
    }
    
    return "range";  // Default
}

std::string IndexSuggestionEngine::generateReason(
    const QueryPatternTracker::QueryPattern& pattern,
    const SelectivityAnalyzer::SelectivityStats& stats,
    const std::string& index_type) const {
    
    std::string reason = "Field '" + pattern.field + "' in collection '" + 
                        pattern.collection + "' is frequently used in " + 
                        pattern.operation + " operations (" + 
                        std::to_string(pattern.count) + " queries). ";
    
    if (stats.selectivity > 0.7) {
        reason += "High selectivity (" + 
                 std::to_string(static_cast<int>(stats.selectivity * 100)) + 
                 "%) makes " + index_type + " index very effective.";
    } else if (stats.selectivity > 0.3) {
        reason += "Moderate selectivity suggests " + index_type + " index would help.";
    } else {
        reason += "Low selectivity, but high query frequency justifies index.";
    }
    
    return reason;
}

nlohmann::json IndexSuggestionEngine::IndexSuggestion::toJson() const {
    return nlohmann::json{
        {"collection", collection},
        {"field", field},
        {"index_type", index_type},
        {"score", score},
        {"reason", reason},
        {"queries_affected", queries_affected},
        {"estimated_speedup_ms", estimated_speedup_ms},
        {"metadata", metadata}
    };
}

IndexSuggestionEngine::IndexSuggestion 
IndexSuggestionEngine::IndexSuggestion::fromJson(const nlohmann::json& j) {
    IndexSuggestion s;
    s.collection = j.value("collection", "");
    s.field = j.value("field", "");
    s.index_type = j.value("index_type", "");
    s.score = j.value("score", 0.0);
    s.reason = j.value("reason", "");
    s.queries_affected = j.value("queries_affected", 0);
    s.estimated_speedup_ms = j.value("estimated_speedup_ms", 0);
    s.metadata = j.value("metadata", nlohmann::json::object());
    return s;
}

// ===== AdaptiveIndexManager Implementation =====

AdaptiveIndexManager::AdaptiveIndexManager(rocksdb::TransactionDB* db)
    : db_(db), tracker_(), analyzer_(db), engine_(&tracker_, &analyzer_) {
    if (!db_) {
        throw std::invalid_argument("AdaptiveIndexManager: db cannot be null");
    }
}

std::vector<IndexSuggestionEngine::IndexSuggestion> 
AdaptiveIndexManager::getSuggestions(const std::string& collection,
                                    double min_score,
                                    size_t limit) {
    return engine_.generateSuggestions(collection, min_score, limit);
}

std::vector<QueryPatternTracker::QueryPattern> 
AdaptiveIndexManager::getPatterns(const std::string& collection) {
    return tracker_.getPatterns(collection);
}

} // namespace themis
