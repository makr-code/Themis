#include "query/window_evaluator.h"
#include "query/let_evaluator.h"

#ifdef _MSC_VER
#pragma warning(disable: 4100)  // unreferenced formal parameter
#pragma warning(disable: 4101)  // unreferenced local variable
#endif

#include <algorithm>
#include <map>
#include <sstream>
#include <cmath>

namespace themis {
namespace query {

// ============================================================================
// WindowSpec/WindowFunctionCall JSON Serialization
// ============================================================================

nlohmann::json WindowSpec::toJSON() const {
    nlohmann::json j;
    j["name"] = name;
    
    nlohmann::json partBy = nlohmann::json::array();
    for (const auto& expr : partitionBy) {
        if (expr) partBy.push_back(expr->toJSON());
    }
    j["partition_by"] = partBy;
    
    nlohmann::json ordBy = nlohmann::json::array();
    for (const auto& spec : orderBy) {
        ordBy.push_back(spec.toJSON());
    }
    j["order_by"] = ordBy;
    
    return j;
}

nlohmann::json WindowFunctionCall::toJSON() const {
    nlohmann::json j;
    
    std::string funcName;
    switch (funcType) {
        case WindowFunctionType::ROW_NUMBER: funcName = "ROW_NUMBER"; break;
        case WindowFunctionType::RANK: funcName = "RANK"; break;
        case WindowFunctionType::DENSE_RANK: funcName = "DENSE_RANK"; break;
        case WindowFunctionType::LAG: funcName = "LAG"; break;
        case WindowFunctionType::LEAD: funcName = "LEAD"; break;
        case WindowFunctionType::FIRST_VALUE: funcName = "FIRST_VALUE"; break;
        case WindowFunctionType::LAST_VALUE: funcName = "LAST_VALUE"; break;
        case WindowFunctionType::NTH_VALUE: funcName = "NTH_VALUE"; break;
    }
    j["function"] = funcName;
    
    if (argument) j["argument"] = argument->toJSON();
    if (offset != 1) j["offset"] = offset;
    if (defaultValue) j["default_value"] = defaultValue->toJSON();
    j["window"] = windowName;
    
    return j;
}

// ============================================================================
// WindowEvaluator Implementation
// ============================================================================

std::vector<nlohmann::json> WindowEvaluator::evaluate(
    const std::vector<nlohmann::json>& rows,
    const WindowSpec& windowSpec,
    const WindowFunctionCall& windowFunc,
    const std::string& forVariable
) {
    if (rows.empty()) {
        return {};
    }
    
    // 1. Partitionierung
    auto partitions = partitionRows(rows, windowSpec.partitionBy, forVariable);
    
    // 2. Initialisiere Result-Vector mit null-Werten
    std::vector<nlohmann::json> results(rows.size(), nullptr);
    
    // 3. Evaluiere jede Partition
    for (const auto& partition : partitions) {
        if (partition.empty()) continue;
        
        // 3.1 Sortiere Partition
        auto sortedIndices = sortPartition(rows, partition, windowSpec.orderBy, forVariable);
        
        // 3.2 Evaluiere Window Function
        std::vector<nlohmann::json> partitionResults;
        
        switch (windowFunc.funcType) {
            case WindowFunctionType::ROW_NUMBER:
                partitionResults = evaluateRowNumber(sortedIndices.size());
                break;
            
            case WindowFunctionType::RANK:
                partitionResults = evaluateRank(rows, sortedIndices, windowSpec.orderBy, forVariable);
                break;
            
            case WindowFunctionType::DENSE_RANK:
                partitionResults = evaluateDenseRank(rows, sortedIndices, windowSpec.orderBy, forVariable);
                break;
            
            case WindowFunctionType::LAG:
                partitionResults = evaluateLag(rows, sortedIndices, windowFunc.argument, 
                                                windowFunc.offset, windowFunc.defaultValue, forVariable);
                break;
            
            case WindowFunctionType::LEAD:
                partitionResults = evaluateLead(rows, sortedIndices, windowFunc.argument,
                                                 windowFunc.offset, windowFunc.defaultValue, forVariable);
                break;
            
            case WindowFunctionType::FIRST_VALUE:
                partitionResults = evaluateFirstValue(rows, sortedIndices, windowFunc.argument, forVariable);
                break;
            
            case WindowFunctionType::LAST_VALUE:
                partitionResults = evaluateLastValue(rows, sortedIndices, windowFunc.argument,
                                                       windowSpec.frame, forVariable);
                break;
            
            default:
                // Fallback: nulls
                partitionResults.resize(sortedIndices.size(), nullptr);
                break;
        }
        
        // 3.3 Mapping zurück zu Original-Indizes
        for (size_t i = 0; i < sortedIndices.size(); ++i) {
            size_t originalIdx = sortedIndices[i];
            results[originalIdx] = partitionResults[i];
        }
    }
    
    return results;
}

// ============================================================================
// Partitionierung
// ============================================================================

std::vector<std::vector<size_t>> WindowEvaluator::partitionRows(
    const std::vector<nlohmann::json>& rows,
    const std::vector<std::shared_ptr<Expression>>& partitionBy,
    const std::string& forVariable
) {
    if (partitionBy.empty()) {
        // Keine Partitionierung → alle Rows in einer Partition
        std::vector<size_t> allIndices(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            allIndices[i] = i;
        }
        return {allIndices};
    }
    
    // Gruppiere nach Partition-Key
    std::map<std::string, std::vector<size_t>> partitionMap;
    
    for (size_t i = 0; i < rows.size(); ++i) {
        std::string key = makePartitionKey(rows[i], partitionBy, forVariable);
        partitionMap[key].push_back(i);
    }
    
    // Konvertiere zu Vector
    std::vector<std::vector<size_t>> result;
    result.reserve(partitionMap.size());
    
    for (auto& [key, indices] : partitionMap) {
        result.push_back(std::move(indices));
    }
    
    return result;
}

std::string WindowEvaluator::makePartitionKey(
    const nlohmann::json& row,
    const std::vector<std::shared_ptr<Expression>>& partitionBy,
    const std::string& forVariable
) {
    std::ostringstream oss;
    
    for (size_t i = 0; i < partitionBy.size(); ++i) {
        if (i > 0) oss << "|";
        
        auto val = evaluateExpression(partitionBy[i], row, forVariable);
        oss << val.dump();
    }
    
    return oss.str();
}

// ============================================================================
// Sortierung
// ============================================================================

std::vector<size_t> WindowEvaluator::sortPartition(
    const std::vector<nlohmann::json>& rows,
    const std::vector<size_t>& partition,
    const std::vector<SortSpec>& orderBy,
    const std::string& forVariable
) {
    std::vector<size_t> sorted = partition;
    
    if (orderBy.empty()) {
        // Keine Sortierung → ursprüngliche Reihenfolge beibehalten
        return sorted;
    }
    
    // Sortiere basierend auf ORDER BY
    std::sort(sorted.begin(), sorted.end(), [&](size_t idxA, size_t idxB) {
        const auto& rowA = rows[idxA];
        const auto& rowB = rows[idxB];
        
        int cmp = compareRows(rowA, rowB, orderBy, forVariable);
        return cmp < 0;
    });
    
    return sorted;
}

int WindowEvaluator::compareRows(
    const nlohmann::json& row1,
    const nlohmann::json& row2,
    const std::vector<SortSpec>& orderBy,
    const std::string& forVariable
) {
    for (const auto& spec : orderBy) {
        auto val1 = evaluateExpression(spec.expression, row1, forVariable);
        auto val2 = evaluateExpression(spec.expression, row2, forVariable);
        
        // Vergleich
        int cmp = 0;
        
        if (val1.is_null() && val2.is_null()) {
            cmp = 0;
        } else if (val1.is_null()) {
            cmp = -1;  // nulls first
        } else if (val2.is_null()) {
            cmp = 1;
        } else if (val1.is_number() && val2.is_number()) {
            double d1 = val1.get<double>();
            double d2 = val2.get<double>();
            if (d1 < d2) cmp = -1;
            else if (d1 > d2) cmp = 1;
            else cmp = 0;
        } else if (val1.is_string() && val2.is_string()) {
            std::string s1 = val1.get<std::string>();
            std::string s2 = val2.get<std::string>();
            cmp = s1.compare(s2);
        } else if (val1.is_boolean() && val2.is_boolean()) {
            bool b1 = val1.get<bool>();
            bool b2 = val2.get<bool>();
            if (b1 == b2) cmp = 0;
            else if (b1 < b2) cmp = -1;
            else cmp = 1;
        } else {
            // Fallback: String-Vergleich
            std::string s1 = val1.dump();
            std::string s2 = val2.dump();
            cmp = s1.compare(s2);
        }
        
        if (cmp != 0) {
            return spec.ascending ? cmp : -cmp;
        }
    }
    
    return 0;  // Alle Felder sind gleich
}

// ============================================================================
// Window Function Evaluations
// ============================================================================

std::vector<nlohmann::json> WindowEvaluator::evaluateRowNumber(size_t partitionSize) {
    std::vector<nlohmann::json> results;
    results.reserve(partitionSize);
    
    for (size_t i = 0; i < partitionSize; ++i) {
        results.push_back(static_cast<int64_t>(i + 1));  // 1-based
    }
    
    return results;
}

std::vector<nlohmann::json> WindowEvaluator::evaluateRank(
    const std::vector<nlohmann::json>& rows,
    const std::vector<size_t>& sortedIndices,
    const std::vector<SortSpec>& orderBy,
    const std::string& forVariable
) {
    std::vector<nlohmann::json> results;
    results.reserve(sortedIndices.size());
    
    if (sortedIndices.empty()) return results;
    
    int64_t currentRank = 1;
    int64_t rowNumber = 1;
    
    for (size_t i = 0; i < sortedIndices.size(); ++i) {
        if (i > 0) {
            // Vergleiche mit vorheriger Row
            const auto& prevRow = rows[sortedIndices[i - 1]];
            const auto& currRow = rows[sortedIndices[i]];
            
            int cmp = compareRows(prevRow, currRow, orderBy, forVariable);
            
            if (cmp != 0) {
                // Unterschiedliche Werte → neue Rank
                currentRank = rowNumber;
            }
            // Sonst: gleiche Werte → gleiche Rank (Tie)
        }
        
        results.push_back(currentRank);
        rowNumber++;
    }
    
    return results;
}

std::vector<nlohmann::json> WindowEvaluator::evaluateDenseRank(
    const std::vector<nlohmann::json>& rows,
    const std::vector<size_t>& sortedIndices,
    const std::vector<SortSpec>& orderBy,
    const std::string& forVariable
) {
    std::vector<nlohmann::json> results;
    results.reserve(sortedIndices.size());
    
    if (sortedIndices.empty()) return results;
    
    int64_t currentDenseRank = 1;
    
    for (size_t i = 0; i < sortedIndices.size(); ++i) {
        if (i > 0) {
            // Vergleiche mit vorheriger Row
            const auto& prevRow = rows[sortedIndices[i - 1]];
            const auto& currRow = rows[sortedIndices[i]];
            
            int cmp = compareRows(prevRow, currRow, orderBy, forVariable);
            
            if (cmp != 0) {
                // Unterschiedliche Werte → increment dense rank (keine Lücken)
                currentDenseRank++;
            }
        }
        
        results.push_back(currentDenseRank);
    }
    
    return results;
}

std::vector<nlohmann::json> WindowEvaluator::evaluateLag(
    const std::vector<nlohmann::json>& rows,
    const std::vector<size_t>& sortedIndices,
    const std::shared_ptr<Expression>& argument,
    int64_t offset,
    const std::shared_ptr<Expression>& defaultValue,
    const std::string& forVariable
) {
    std::vector<nlohmann::json> results;
    results.reserve(sortedIndices.size());
    
    nlohmann::json defaultVal = nullptr;
    if (defaultValue) {
        // Evaluiere default (ohne Row-Context)
        LetEvaluator evaluator;
        defaultVal = evaluator.evaluateExpression(defaultValue, nlohmann::json());
    }
    
    for (size_t i = 0; i < sortedIndices.size(); ++i) {
        int64_t lagIdx = static_cast<int64_t>(i) - offset;
        
        if (lagIdx < 0) {
            // Out of bounds → default
            results.push_back(defaultVal);
        } else {
            // Zugriff auf vorherige Row
            size_t prevRowIdx = sortedIndices[static_cast<size_t>(lagIdx)];
            const auto& prevRow = rows[prevRowIdx];
            
            if (argument) {
                auto val = evaluateExpression(argument, prevRow, forVariable);
                results.push_back(val);
            } else {
                results.push_back(nullptr);
            }
        }
    }
    
    return results;
}

std::vector<nlohmann::json> WindowEvaluator::evaluateLead(
    const std::vector<nlohmann::json>& rows,
    const std::vector<size_t>& sortedIndices,
    const std::shared_ptr<Expression>& argument,
    int64_t offset,
    const std::shared_ptr<Expression>& defaultValue,
    const std::string& forVariable
) {
    std::vector<nlohmann::json> results;
    results.reserve(sortedIndices.size());
    
    nlohmann::json defaultVal = nullptr;
    if (defaultValue) {
        LetEvaluator evaluator;
        defaultVal = evaluator.evaluateExpression(defaultValue, nlohmann::json());
    }
    
    for (size_t i = 0; i < sortedIndices.size(); ++i) {
        int64_t leadIdx = static_cast<int64_t>(i) + offset;
        
        if (leadIdx >= static_cast<int64_t>(sortedIndices.size())) {
            // Out of bounds → default
            results.push_back(defaultVal);
        } else {
            // Zugriff auf nächste Row
            size_t nextRowIdx = sortedIndices[static_cast<size_t>(leadIdx)];
            const auto& nextRow = rows[nextRowIdx];
            
            if (argument) {
                auto val = evaluateExpression(argument, nextRow, forVariable);
                results.push_back(val);
            } else {
                results.push_back(nullptr);
            }
        }
    }
    
    return results;
}

std::vector<nlohmann::json> WindowEvaluator::evaluateFirstValue(
    const std::vector<nlohmann::json>& rows,
    const std::vector<size_t>& sortedIndices,
    const std::shared_ptr<Expression>& argument,
    const std::string& forVariable
) {
    std::vector<nlohmann::json> results;
    results.reserve(sortedIndices.size());
    
    if (sortedIndices.empty()) return results;
    
    // FIRST_VALUE ist der Wert der ersten Row in der Partition
    size_t firstRowIdx = sortedIndices[0];
    const auto& firstRow = rows[firstRowIdx];
    
    nlohmann::json firstVal = nullptr;
    if (argument) {
        firstVal = evaluateExpression(argument, firstRow, forVariable);
    }
    
    // Alle Rows bekommen den gleichen Wert
    for (size_t i = 0; i < sortedIndices.size(); ++i) {
        results.push_back(firstVal);
    }
    
    return results;
}

std::vector<nlohmann::json> WindowEvaluator::evaluateLastValue(
    const std::vector<nlohmann::json>& rows,
    const std::vector<size_t>& sortedIndices,
    const std::shared_ptr<Expression>& argument,
    const WindowFrame& frame,
    const std::string& forVariable
) {
    std::vector<nlohmann::json> results;
    results.reserve(sortedIndices.size());
    
    if (sortedIndices.empty()) return results;
    
    // LAST_VALUE hängt von Frame ab
    // Default Frame: RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    // → LAST_VALUE ist der Wert der aktuellen Row (nicht der letzten Row der Partition!)
    
    for (size_t i = 0; i < sortedIndices.size(); ++i) {
        size_t lastRowIdx;
        
        // Frame-End bestimmen
        if (frame.end.type == WindowFrameBound::BoundType::CURRENT_ROW) {
            // CURRENT ROW → i
            lastRowIdx = sortedIndices[i];
        } else if (frame.end.type == WindowFrameBound::BoundType::UNBOUNDED_FOLLOWING) {
            // UNBOUNDED FOLLOWING → letzte Row der Partition
            lastRowIdx = sortedIndices.back();
        } else if (frame.end.type == WindowFrameBound::BoundType::FOLLOWING) {
            // N FOLLOWING
            int64_t followIdx = static_cast<int64_t>(i) + frame.end.offset;
            if (followIdx >= static_cast<int64_t>(sortedIndices.size())) {
                followIdx = static_cast<int64_t>(sortedIndices.size()) - 1;
            }
            lastRowIdx = sortedIndices[static_cast<size_t>(followIdx)];
        } else {
            // Fallback: Current row
            lastRowIdx = sortedIndices[i];
        }
        
        const auto& lastRow = rows[lastRowIdx];
        
        nlohmann::json lastVal = nullptr;
        if (argument) {
            lastVal = evaluateExpression(argument, lastRow, forVariable);
        }
        
        results.push_back(lastVal);
    }
    
    return results;
}

// ============================================================================
// Expression Evaluation
// ============================================================================

nlohmann::json WindowEvaluator::evaluateExpression(
    const std::shared_ptr<Expression>& expr,
    const nlohmann::json& row,
    const std::string& forVariable
) {
    if (!expr) return nullptr;
    
    // Nutze LetEvaluator für Expression-Evaluation
    // (LetEvaluator kann bereits Expressions evaluieren)
    LetEvaluator evaluator;
    
    try {
        return evaluator.evaluateExpression(expr, row);
    } catch (const std::exception& e) {
        // Fallback: null
        return nullptr;
    }
}

} // namespace query
} // namespace themis
