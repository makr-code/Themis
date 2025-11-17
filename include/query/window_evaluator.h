#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include "query/aql_parser.h"

namespace themis {
namespace query {

/**
 * @brief Window Function Evaluator für AQL
 * 
 * Unterstützt SQL-ähnliche Window Functions:
 * - ROW_NUMBER(): Fortlaufende Nummer innerhalb Partition
 * - RANK(): Ranking mit Lücken bei Ties
 * - DENSE_RANK(): Ranking ohne Lücken
 * - LAG(expr, offset): Zugriff auf vorherige Row
 * - LEAD(expr, offset): Zugriff auf nächste Row
 * - FIRST_VALUE(expr): Erster Wert in Window
 * - LAST_VALUE(expr): Letzter Wert in Window
 * 
 * Partitionierung: PARTITION BY field1, field2, ...
 * Sortierung: ORDER BY field1 [ASC|DESC], field2 [ASC|DESC], ...
 * 
 * Beispiel AQL:
 * FOR doc IN sales
 *   WINDOW w AS (
 *     PARTITION BY doc.category
 *     ORDER BY doc.amount DESC
 *   )
 *   RETURN {
 *     product: doc.product,
 *     amount: doc.amount,
 *     rank: RANK() OVER w,
 *     row_num: ROW_NUMBER() OVER w,
 *     prev_amount: LAG(doc.amount, 1) OVER w
 *   }
 */

/**
 * @brief Window Function Type
 */
enum class WindowFunctionType {
    ROW_NUMBER,      // Sequential row number
    RANK,            // Rank with gaps
    DENSE_RANK,      // Rank without gaps
    LAG,             // Previous row value
    LEAD,            // Next row value
    FIRST_VALUE,     // First value in window
    LAST_VALUE,      // Last value in window
    NTH_VALUE        // N-th value in window (Phase 2)
};

/**
 * @brief Window Frame Type (ROWS vs RANGE)
 */
enum class WindowFrameType {
    ROWS,            // Physical rows (count-based)
    RANGE            // Logical range (value-based)
};

/**
 * @brief Window Frame Boundary
 */
struct WindowFrameBound {
    enum class BoundType {
        UNBOUNDED_PRECEDING,    // Start of partition
        UNBOUNDED_FOLLOWING,    // End of partition
        CURRENT_ROW,            // Current row
        PRECEDING,              // N rows/values before current
        FOLLOWING               // N rows/values after current
    };
    
    BoundType type;
    int64_t offset = 0;  // For PRECEDING/FOLLOWING
    
    static WindowFrameBound unboundedPreceding() {
        return {BoundType::UNBOUNDED_PRECEDING, 0};
    }
    
    static WindowFrameBound unboundedFollowing() {
        return {BoundType::UNBOUNDED_FOLLOWING, 0};
    }
    
    static WindowFrameBound currentRow() {
        return {BoundType::CURRENT_ROW, 0};
    }
    
    static WindowFrameBound preceding(int64_t n) {
        return {BoundType::PRECEDING, n};
    }
    
    static WindowFrameBound following(int64_t n) {
        return {BoundType::FOLLOWING, n};
    }
};

/**
 * @brief Window Frame Definition
 */
struct WindowFrame {
    WindowFrameType type = WindowFrameType::RANGE;
    WindowFrameBound start = WindowFrameBound::unboundedPreceding();
    WindowFrameBound end = WindowFrameBound::currentRow();
    
    // Default: RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    WindowFrame() = default;
    
    WindowFrame(WindowFrameType t, WindowFrameBound s, WindowFrameBound e)
        : type(t), start(s), end(e) {}
};

/**
 * @brief Window Specification
 */
struct WindowSpec {
    std::string name;  // Named window (e.g., "w" in WINDOW w AS (...))
    std::vector<std::shared_ptr<Expression>> partitionBy;  // PARTITION BY expressions
    std::vector<SortSpec> orderBy;                         // ORDER BY specifications
    WindowFrame frame;                                     // Frame definition
    
    nlohmann::json toJSON() const;
};

/**
 * @brief Window Function Call
 */
struct WindowFunctionCall {
    WindowFunctionType funcType;
    std::shared_ptr<Expression> argument;  // For LAG/LEAD/FIRST_VALUE/LAST_VALUE/NTH_VALUE
    int64_t offset = 1;                    // For LAG/LEAD (default: 1)
    std::shared_ptr<Expression> defaultValue;  // Default when out of bounds
    std::string windowName;                // Reference to named window (e.g., "w")
    
    nlohmann::json toJSON() const;
};

/**
 * @brief Window Evaluator Implementation
 */
class WindowEvaluator {
public:
    WindowEvaluator() = default;
    
    /**
     * @brief Evaluiert Window Functions für alle Rows
     * @param rows Die zu verarbeitenden Rows (JSON-Dokumente)
     * @param windowSpec Die Window-Spezifikation (PARTITION BY, ORDER BY, FRAME)
     * @param windowFunc Die Window Function Definition
     * @param forVariable Der FOR-Loop Variable Name (z.B. "doc")
     * @return Vector von evaluierten Werten (ein Wert pro Row)
     */
    std::vector<nlohmann::json> evaluate(
        const std::vector<nlohmann::json>& rows,
        const WindowSpec& windowSpec,
        const WindowFunctionCall& windowFunc,
        const std::string& forVariable
    );
    
private:
    /**
     * @brief Partitioniert Rows basierend auf PARTITION BY
     * @param rows Alle Rows
     * @param partitionBy PARTITION BY Expressions
     * @param forVariable FOR-Variable Name
     * @return Map von Partition-Key → Row-Indizes
     */
    std::vector<std::vector<size_t>> partitionRows(
        const std::vector<nlohmann::json>& rows,
        const std::vector<std::shared_ptr<Expression>>& partitionBy,
        const std::string& forVariable
    );
    
    /**
     * @brief Sortiert Rows innerhalb jeder Partition
     * @param rows Alle Rows
     * @param partition Row-Indizes der Partition
     * @param orderBy ORDER BY Specifications
     * @param forVariable FOR-Variable Name
     * @return Sortierte Row-Indizes
     */
    std::vector<size_t> sortPartition(
        const std::vector<nlohmann::json>& rows,
        const std::vector<size_t>& partition,
        const std::vector<SortSpec>& orderBy,
        const std::string& forVariable
    );
    
    /**
     * @brief Evaluiert ROW_NUMBER() für eine Partition
     * @param partitionSize Anzahl Rows in Partition
     * @return Row Numbers (1-based)
     */
    std::vector<nlohmann::json> evaluateRowNumber(size_t partitionSize);
    
    /**
     * @brief Evaluiert RANK() für eine Partition
     * @param rows Alle Rows
     * @param sortedIndices Sortierte Row-Indizes
     * @param orderBy ORDER BY Specifications
     * @param forVariable FOR-Variable Name
     * @return Ranks (1-based, mit Lücken bei Ties)
     */
    std::vector<nlohmann::json> evaluateRank(
        const std::vector<nlohmann::json>& rows,
        const std::vector<size_t>& sortedIndices,
        const std::vector<SortSpec>& orderBy,
        const std::string& forVariable
    );
    
    /**
     * @brief Evaluiert DENSE_RANK() für eine Partition
     * @param rows Alle Rows
     * @param sortedIndices Sortierte Row-Indizes
     * @param orderBy ORDER BY Specifications
     * @param forVariable FOR-Variable Name
     * @return Dense Ranks (1-based, keine Lücken)
     */
    std::vector<nlohmann::json> evaluateDenseRank(
        const std::vector<nlohmann::json>& rows,
        const std::vector<size_t>& sortedIndices,
        const std::vector<SortSpec>& orderBy,
        const std::string& forVariable
    );
    
    /**
     * @brief Evaluiert LAG(expr, offset) für eine Partition
     * @param rows Alle Rows
     * @param sortedIndices Sortierte Row-Indizes
     * @param argument Expression für Wert-Extraktion
     * @param offset Offset (default: 1)
     * @param defaultValue Default wenn out of bounds
     * @param forVariable FOR-Variable Name
     * @return LAG-Werte
     */
    std::vector<nlohmann::json> evaluateLag(
        const std::vector<nlohmann::json>& rows,
        const std::vector<size_t>& sortedIndices,
        const std::shared_ptr<Expression>& argument,
        int64_t offset,
        const std::shared_ptr<Expression>& defaultValue,
        const std::string& forVariable
    );
    
    /**
     * @brief Evaluiert LEAD(expr, offset) für eine Partition
     * @param rows Alle Rows
     * @param sortedIndices Sortierte Row-Indizes
     * @param argument Expression für Wert-Extraktion
     * @param offset Offset (default: 1)
     * @param defaultValue Default wenn out of bounds
     * @param forVariable FOR-Variable Name
     * @return LEAD-Werte
     */
    std::vector<nlohmann::json> evaluateLead(
        const std::vector<nlohmann::json>& rows,
        const std::vector<size_t>& sortedIndices,
        const std::shared_ptr<Expression>& argument,
        int64_t offset,
        const std::shared_ptr<Expression>& defaultValue,
        const std::string& forVariable
    );
    
    /**
     * @brief Evaluiert FIRST_VALUE(expr) für eine Partition
     * @param rows Alle Rows
     * @param sortedIndices Sortierte Row-Indizes
     * @param argument Expression für Wert-Extraktion
     * @param forVariable FOR-Variable Name
     * @return FIRST_VALUE für jede Row
     */
    std::vector<nlohmann::json> evaluateFirstValue(
        const std::vector<nlohmann::json>& rows,
        const std::vector<size_t>& sortedIndices,
        const std::shared_ptr<Expression>& argument,
        const std::string& forVariable
    );
    
    /**
     * @brief Evaluiert LAST_VALUE(expr) für eine Partition
     * @param rows Alle Rows
     * @param sortedIndices Sortierte Row-Indizes
     * @param argument Expression für Wert-Extraktion
     * @param frame Frame Definition
     * @param forVariable FOR-Variable Name
     * @return LAST_VALUE für jede Row (basierend auf Frame)
     */
    std::vector<nlohmann::json> evaluateLastValue(
        const std::vector<nlohmann::json>& rows,
        const std::vector<size_t>& sortedIndices,
        const std::shared_ptr<Expression>& argument,
        const WindowFrame& frame,
        const std::string& forVariable
    );
    
    /**
     * @brief Vergleicht zwei Rows basierend auf ORDER BY
     * @param row1 Erste Row
     * @param row2 Zweite Row
     * @param orderBy ORDER BY Specifications
     * @param forVariable FOR-Variable Name
     * @return <0 wenn row1 < row2, 0 wenn gleich, >0 wenn row1 > row2
     */
    int compareRows(
        const nlohmann::json& row1,
        const nlohmann::json& row2,
        const std::vector<SortSpec>& orderBy,
        const std::string& forVariable
    );
    
    /**
     * @brief Evaluiert Expression für eine Row
     * @param expr Expression
     * @param row Row (JSON-Dokument)
     * @param forVariable FOR-Variable Name
     * @return Evaluierter Wert
     */
    nlohmann::json evaluateExpression(
        const std::shared_ptr<Expression>& expr,
        const nlohmann::json& row,
        const std::string& forVariable
    );
    
    /**
     * @brief Erstellt Partition-Key aus PARTITION BY Expressions
     * @param row Row (JSON-Dokument)
     * @param partitionBy PARTITION BY Expressions
     * @param forVariable FOR-Variable Name
     * @return Partition-Key (String-Repräsentation)
     */
    std::string makePartitionKey(
        const nlohmann::json& row,
        const std::vector<std::shared_ptr<Expression>>& partitionBy,
        const std::string& forVariable
    );
};

} // namespace query
} // namespace themis
