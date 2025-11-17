#include "query/let_evaluator.h"
#include "utils/logger.h"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace themis {
namespace query {

bool LetEvaluator::evaluateLet(const LetNode& node, const nlohmann::json& currentDoc) {
    try {
        auto value = evaluateExpression(node.expression, currentDoc);
        bindings_[node.variable] = std::move(value);
        return true;
    } catch (const std::exception& e) {
        THEMIS_ERROR("LET evaluation failed for variable '{}': {}", node.variable, e.what());
        return false;
    }
}

std::optional<nlohmann::json> LetEvaluator::resolveVariable(const std::string& varName) const {
    auto it = bindings_.find(varName);
    if (it != bindings_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool LetEvaluator::hasVariable(const std::string& varName) const {
    return bindings_.find(varName) != bindings_.end();
}

void LetEvaluator::clear() {
    bindings_.clear();
}

nlohmann::json LetEvaluator::evaluateExpression(
    const std::shared_ptr<Expression>& expr,
    const nlohmann::json& currentDoc
) const {
    if (!expr) {
        return nlohmann::json(nullptr);
    }

    // Literal (number, string, bool, null, array, object)
    if (auto lit = dynamic_cast<Expression::LiteralExpression*>(expr.get())) {
        return evaluateLiteral(lit);
    }

    // Field Access (doc.age, doc.address.city)
    if (auto fa = dynamic_cast<Expression::FieldAccessExpression*>(expr.get())) {
        return evaluateFieldAccess(fa, currentDoc);
    }

    // Binary Operation (+, -, *, /, %, ==, !=, <, >, <=, >=, AND, OR)
    if (auto binOp = dynamic_cast<Expression::BinaryOpExpression*>(expr.get())) {
        return evaluateBinaryOp(binOp, currentDoc);
    }

    // Unary Operation (-, NOT)
    if (auto unaryOp = dynamic_cast<Expression::UnaryOpExpression*>(expr.get())) {
        return evaluateUnaryOp(unaryOp, currentDoc);
    }

    // Function Call (LENGTH, CONCAT, SUBSTRING, UPPER, LOWER, etc.)
    if (auto funcCall = dynamic_cast<Expression::FunctionCallExpression*>(expr.get())) {
        return evaluateFunctionCall(funcCall, currentDoc);
    }

    throw std::runtime_error("Unknown expression type in LET evaluator");
}

nlohmann::json LetEvaluator::evaluateLiteral(const Expression::LiteralExpression* lit) const {
    return lit->value;
}

nlohmann::json LetEvaluator::evaluateFieldAccess(
    const Expression::FieldAccessExpression* fieldAccess,
    const nlohmann::json& currentDoc
) const {
    // Check if it's a reference to a LET variable
    if (fieldAccess->path.size() == 1) {
        auto varValue = resolveVariable(fieldAccess->path[0]);
        if (varValue.has_value()) {
            return varValue.value();
        }
    }

    // Check if it's a doc.field access
    if (fieldAccess->path.empty()) {
        return nlohmann::json(nullptr);
    }

    // Special case: "doc" refers to currentDoc
    if (fieldAccess->path[0] == "doc") {
        if (fieldAccess->path.size() == 1) {
            return currentDoc;
        }
        // doc.field.subfield
        std::vector<std::string> docPath(fieldAccess->path.begin() + 1, fieldAccess->path.end());
        return getNestedValue(currentDoc, docPath);
    }

    // Variable access with nested fields (e.g., x.name where x is a LET variable)
    auto varValue = resolveVariable(fieldAccess->path[0]);
    if (varValue.has_value()) {
        if (fieldAccess->path.size() == 1) {
            return varValue.value();
        }
        std::vector<std::string> nestedPath(fieldAccess->path.begin() + 1, fieldAccess->path.end());
        return getNestedValue(varValue.value(), nestedPath);
    }

    // Try as direct field access on currentDoc (fallback)
    return getNestedValue(currentDoc, fieldAccess->path);
}

nlohmann::json LetEvaluator::getNestedValue(
    const nlohmann::json& obj,
    const std::vector<std::string>& path
) const {
    nlohmann::json current = obj;
    for (const auto& key : path) {
        if (current.is_object() && current.contains(key)) {
            current = current[key];
        } else if (current.is_array()) {
            // Try to parse key as array index
            try {
                size_t idx = std::stoull(key);
                if (idx < current.size()) {
                    current = current[idx];
                } else {
                    return nlohmann::json(nullptr);
                }
            } catch (...) {
                return nlohmann::json(nullptr);
            }
        } else {
            return nlohmann::json(nullptr);
        }
    }
    return current;
}

nlohmann::json LetEvaluator::evaluateBinaryOp(
    const Expression::BinaryOpExpression* binOp,
    const nlohmann::json& currentDoc
) const {
    auto left = evaluateExpression(binOp->left, currentDoc);
    auto right = evaluateExpression(binOp->right, currentDoc);

    const std::string& op = binOp->op;

    // Arithmetic operations
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        return applyArithmeticOp(op, left, right);
    }

    // Comparison operations
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        return applyComparisonOp(op, left, right);
    }

    // Logical operations
    if (op == "AND" || op == "OR") {
        return applyLogicalOp(op, left, right);
    }

    throw std::runtime_error("Unknown binary operator: " + op);
}

nlohmann::json LetEvaluator::applyArithmeticOp(
    const std::string& op,
    const nlohmann::json& left,
    const nlohmann::json& right
) const {
    // String concatenation for +
    if (op == "+" && (left.is_string() || right.is_string())) {
        std::string leftStr = left.is_string() ? left.get<std::string>() : left.dump();
        std::string rightStr = right.is_string() ? right.get<std::string>() : right.dump();
        return leftStr + rightStr;
    }

    double leftNum = toNumber(left);
    double rightNum = toNumber(right);

    if (op == "+") return leftNum + rightNum;
    if (op == "-") return leftNum - rightNum;
    if (op == "*") return leftNum * rightNum;
    if (op == "/") {
        if (rightNum == 0.0) {
            throw std::runtime_error("Division by zero");
        }
        return leftNum / rightNum;
    }
    if (op == "%") {
        if (rightNum == 0.0) {
            throw std::runtime_error("Modulo by zero");
        }
        return std::fmod(leftNum, rightNum);
    }

    throw std::runtime_error("Unknown arithmetic operator: " + op);
}

nlohmann::json LetEvaluator::applyComparisonOp(
    const std::string& op,
    const nlohmann::json& left,
    const nlohmann::json& right
) const {
    if (op == "==") return left == right;
    if (op == "!=") return left != right;

    // Numeric comparisons
    if (left.is_number() && right.is_number()) {
        double leftNum = left.get<double>();
        double rightNum = right.get<double>();
        if (op == "<") return leftNum < rightNum;
        if (op == ">") return leftNum > rightNum;
        if (op == "<=") return leftNum <= rightNum;
        if (op == ">=") return leftNum >= rightNum;
    }

    // String comparisons
    if (left.is_string() && right.is_string()) {
        std::string leftStr = left.get<std::string>();
        std::string rightStr = right.get<std::string>();
        if (op == "<") return leftStr < rightStr;
        if (op == ">") return leftStr > rightStr;
        if (op == "<=") return leftStr <= rightStr;
        if (op == ">=") return leftStr >= rightStr;
    }

    // Type mismatch or unsupported comparison
    return false;
}

nlohmann::json LetEvaluator::applyLogicalOp(
    const std::string& op,
    const nlohmann::json& left,
    const nlohmann::json& right
) const {
    bool leftBool = toBool(left);
    bool rightBool = toBool(right);

    if (op == "AND") return leftBool && rightBool;
    if (op == "OR") return leftBool || rightBool;

    throw std::runtime_error("Unknown logical operator: " + op);
}

nlohmann::json LetEvaluator::evaluateUnaryOp(
    const Expression::UnaryOpExpression* unaryOp,
    const nlohmann::json& currentDoc
) const {
    auto operand = evaluateExpression(unaryOp->operand, currentDoc);

    if (unaryOp->op == "-") {
        return -toNumber(operand);
    }

    if (unaryOp->op == "NOT") {
        return !toBool(operand);
    }

    throw std::runtime_error("Unknown unary operator: " + unaryOp->op);
}

nlohmann::json LetEvaluator::evaluateFunctionCall(
    const Expression::FunctionCallExpression* funcCall,
    const nlohmann::json& currentDoc
) const {
    const std::string& funcName = funcCall->functionName;
    const auto& args = funcCall->arguments;

    // LENGTH(value) - returns length of string, array, or object
    if (funcName == "LENGTH") {
        if (args.size() != 1) {
            throw std::runtime_error("LENGTH expects 1 argument");
        }
        auto val = evaluateExpression(args[0], currentDoc);
        if (val.is_string()) return val.get<std::string>().length();
        if (val.is_array()) return val.size();
        if (val.is_object()) return val.size();
        return 0;
    }

    // CONCAT(...) - concatenate strings
    if (funcName == "CONCAT") {
        std::string result;
        for (const auto& arg : args) {
            auto val = evaluateExpression(arg, currentDoc);
            if (val.is_string()) {
                result += val.get<std::string>();
            } else {
                result += val.dump();
            }
        }
        return result;
    }

    // SUBSTRING(str, start, length)
    if (funcName == "SUBSTRING") {
        if (args.size() < 2 || args.size() > 3) {
            throw std::runtime_error("SUBSTRING expects 2 or 3 arguments");
        }
        auto str = evaluateExpression(args[0], currentDoc);
        auto start = evaluateExpression(args[1], currentDoc);
        
        if (!str.is_string()) {
            throw std::runtime_error("SUBSTRING expects string as first argument");
        }
        
        std::string strVal = str.get<std::string>();
        size_t startIdx = static_cast<size_t>(toNumber(start));
        
        if (startIdx >= strVal.length()) {
            return "";
        }
        
        if (args.size() == 3) {
            auto length = evaluateExpression(args[2], currentDoc);
            size_t lengthVal = static_cast<size_t>(toNumber(length));
            return strVal.substr(startIdx, lengthVal);
        }
        
        return strVal.substr(startIdx);
    }

    // UPPER(str) - convert to uppercase
    if (funcName == "UPPER") {
        if (args.size() != 1) {
            throw std::runtime_error("UPPER expects 1 argument");
        }
        auto val = evaluateExpression(args[0], currentDoc);
        if (!val.is_string()) {
            throw std::runtime_error("UPPER expects string argument");
        }
        std::string str = val.get<std::string>();
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        return str;
    }

    // LOWER(str) - convert to lowercase
    if (funcName == "LOWER") {
        if (args.size() != 1) {
            throw std::runtime_error("LOWER expects 1 argument");
        }
        auto val = evaluateExpression(args[0], currentDoc);
        if (!val.is_string()) {
            throw std::runtime_error("LOWER expects string argument");
        }
        std::string str = val.get<std::string>();
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return str;
    }

    // ABS(num) - absolute value
    if (funcName == "ABS") {
        if (args.size() != 1) {
            throw std::runtime_error("ABS expects 1 argument");
        }
        auto val = evaluateExpression(args[0], currentDoc);
        return std::abs(toNumber(val));
    }

    // CEIL(num) - ceiling
    if (funcName == "CEIL") {
        if (args.size() != 1) {
            throw std::runtime_error("CEIL expects 1 argument");
        }
        auto val = evaluateExpression(args[0], currentDoc);
        return std::ceil(toNumber(val));
    }

    // FLOOR(num) - floor
    if (funcName == "FLOOR") {
        if (args.size() != 1) {
            throw std::runtime_error("FLOOR expects 1 argument");
        }
        auto val = evaluateExpression(args[0], currentDoc);
        return std::floor(toNumber(val));
    }

    // ROUND(num) - round
    if (funcName == "ROUND") {
        if (args.size() != 1) {
            throw std::runtime_error("ROUND expects 1 argument");
        }
        auto val = evaluateExpression(args[0], currentDoc);
        return std::round(toNumber(val));
    }

    // MIN(...) - minimum value
    if (funcName == "MIN") {
        if (args.empty()) {
            throw std::runtime_error("MIN expects at least 1 argument");
        }
        double minVal = toNumber(evaluateExpression(args[0], currentDoc));
        for (size_t i = 1; i < args.size(); ++i) {
            double val = toNumber(evaluateExpression(args[i], currentDoc));
            minVal = std::min(minVal, val);
        }
        return minVal;
    }

    // MAX(...) - maximum value
    if (funcName == "MAX") {
        if (args.empty()) {
            throw std::runtime_error("MAX expects at least 1 argument");
        }
        double maxVal = toNumber(evaluateExpression(args[0], currentDoc));
        for (size_t i = 1; i < args.size(); ++i) {
            double val = toNumber(evaluateExpression(args[i], currentDoc));
            maxVal = std::max(maxVal, val);
        }
        return maxVal;
    }

    throw std::runtime_error("Unknown function: " + funcName);
}

bool LetEvaluator::toBool(const nlohmann::json& value) const {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number()) {
        return value.get<double>() != 0.0;
    }
    if (value.is_string()) {
        return !value.get<std::string>().empty();
    }
    if (value.is_null()) {
        return false;
    }
    if (value.is_array() || value.is_object()) {
        return !value.empty();
    }
    return false;
}

double LetEvaluator::toNumber(const nlohmann::json& value) const {
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1.0 : 0.0;
    }
    if (value.is_string()) {
        try {
            return std::stod(value.get<std::string>());
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

} // namespace query
} // namespace themis
