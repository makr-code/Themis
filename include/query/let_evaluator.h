#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include "query/aql_parser.h"

namespace themis {
namespace query {

/**
 * @brief LET Evaluator für AQL Variable Bindings
 * 
 * Verwaltet LET-Deklarationen und evaluiert Expressions zu JSON-Werten.
 * Unterstützt:
 * - Einfache Expressions (Literale, Field Access)
 * - Arithmetische Operationen (+, -, *, /, %)
 * - String-Operationen (CONCAT, SUBSTRING, etc.)
 * - Nested Object/Array Access
 * - Referenzen zu vorherigen LETs
 * - Funktionsaufrufe (LENGTH, UPPER, LOWER, etc.)
 * 
 * Beispiel:
 * LET x = doc.age
 * LET y = x * 2
 * LET fullName = CONCAT(doc.firstName, " ", doc.lastName)
 */
class LetEvaluator {
public:
    LetEvaluator() = default;

    /**
     * @brief Evaluiert einen LET-Node und speichert das Binding
     * @param node Der LET-Node mit Variable und Expression
     * @param currentDoc Das aktuelle Dokument (für doc.field Zugriffe)
     * @return true wenn erfolgreich, false bei Errors (undefined vars, etc.)
     */
    bool evaluateLet(const LetNode& node, const nlohmann::json& currentDoc);

    /**
     * @brief Löst eine Variable zu ihrem JSON-Wert auf
     * @param varName Der Name der Variable (ohne $-Prefix)
     * @return Der JSON-Wert oder std::nullopt wenn Variable nicht existiert
     */
    std::optional<nlohmann::json> resolveVariable(const std::string& varName) const;

    /**
     * @brief Prüft ob eine Variable existiert
     * @param varName Der Name der Variable
     * @return true wenn Variable gebunden ist
     */
    bool hasVariable(const std::string& varName) const;

    /**
     * @brief Löscht alle Variable Bindings (für neue Query Iteration)
     */
    void clear();

    /**
     * @brief Evaluiert eine Expression zu einem JSON-Wert
     * @param expr Die zu evaluierende Expression
     * @param currentDoc Das aktuelle Dokument (für doc.field Zugriffe)
     * @return Der evaluierte JSON-Wert
     * @throws std::runtime_error bei Evaluation-Errors
     */
    nlohmann::json evaluateExpression(
        const std::shared_ptr<Expression>& expr,
        const nlohmann::json& currentDoc
    ) const;

    /**
     * @brief Alle aktuellen Bindings abrufen (für Debugging/Logging)
     * @return Map von Variable → JSON-Wert
     */
    const std::unordered_map<std::string, nlohmann::json>& getBindings() const {
        return bindings_;
    }

private:
    // Variable bindings: Variable Name → JSON Value
    std::unordered_map<std::string, nlohmann::json> bindings_;

    // Helper: Evaluiert Field Access (z.B. doc.age, doc.address.city)
    nlohmann::json evaluateFieldAccess(
        const Expression::FieldAccessExpression* fieldAccess,
        const nlohmann::json& currentDoc
    ) const;

    // Helper: Evaluiert Binary Operations (+, -, *, /, %, ==, !=, <, >, etc.)
    nlohmann::json evaluateBinaryOp(
        const Expression::BinaryOpExpression* binOp,
        const nlohmann::json& currentDoc
    ) const;

    // Helper: Evaluiert Unary Operations (-, NOT)
    nlohmann::json evaluateUnaryOp(
        const Expression::UnaryOpExpression* unaryOp,
        const nlohmann::json& currentDoc
    ) const;

    // Helper: Evaluiert Function Calls (LENGTH, CONCAT, SUBSTRING, etc.)
    nlohmann::json evaluateFunctionCall(
        const Expression::FunctionCallExpression* funcCall,
        const nlohmann::json& currentDoc
    ) const;

    // Helper: Evaluiert Array/Object Literal
    nlohmann::json evaluateLiteral(const Expression::LiteralExpression* lit) const;

    // Helper: Holt Wert aus nested JSON object (z.B. ["address", "city"])
    nlohmann::json getNestedValue(
        const nlohmann::json& obj,
        const std::vector<std::string>& path
    ) const;

    // Helper: Arithmetische Operation auf zwei JSON-Werten
    nlohmann::json applyArithmeticOp(
        const std::string& op,
        const nlohmann::json& left,
        const nlohmann::json& right
    ) const;

    // Helper: Vergleichsoperation auf zwei JSON-Werten
    nlohmann::json applyComparisonOp(
        const std::string& op,
        const nlohmann::json& left,
        const nlohmann::json& right
    ) const;

    // Helper: Logische Operation (AND, OR)
    nlohmann::json applyLogicalOp(
        const std::string& op,
        const nlohmann::json& left,
        const nlohmann::json& right
    ) const;

    // Helper: Konvertiert JSON zu bool für Conditions
    bool toBool(const nlohmann::json& value) const;

    // Helper: Konvertiert JSON zu number für Arithmetik
    double toNumber(const nlohmann::json& value) const;
};

} // namespace query
} // namespace themis
