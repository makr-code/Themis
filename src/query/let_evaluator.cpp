#include "query/let_evaluator.h"
#include "utils/logger.h"
#include "utils/geo/ewkb.h"
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

    // ================= SPATIAL FUNCTIONS (ST_*) =================
    
    // ST_Point(x, y) - Create a 2D Point geometry
    // Returns: GeoJSON object {"type": "Point", "coordinates": [x, y]}
    if (funcName == "ST_Point") {
        if (args.size() != 2) {
            throw std::runtime_error("ST_Point expects 2 arguments: ST_Point(x, y)");
        }
        double x = toNumber(evaluateExpression(args[0], currentDoc));
        double y = toNumber(evaluateExpression(args[1], currentDoc));
        
        nlohmann::json geojson;
        geojson["type"] = "Point";
        geojson["coordinates"] = {x, y};
        return geojson;
    }

    // ST_AsGeoJSON(geometry) - Convert geometry to GeoJSON string
    // Input: GeoJSON object or EWKB binary string
    // Output: GeoJSON string representation
    if (funcName == "ST_AsGeoJSON") {
        if (args.size() != 1) {
            throw std::runtime_error("ST_AsGeoJSON expects 1 argument");
        }
        auto geom = evaluateExpression(args[0], currentDoc);
        
        // If already GeoJSON object, convert to string
        if (geom.is_object() && geom.contains("type") && geom.contains("coordinates")) {
            return geom.dump();
        }
        
        // If EWKB binary (stored as base64 string or byte array)
        if (geom.is_string()) {
            std::string ewkbStr = geom.get<std::string>();
            std::vector<uint8_t> ewkb(ewkbStr.begin(), ewkbStr.end());
            
            try {
                auto geomInfo = utils::geo::parseEWKB(ewkb);
                nlohmann::json geojson;
                
                // Map GeometryType to GeoJSON type string
                switch (geomInfo.type) {
                    case utils::geo::GeometryType::Point:
                    case utils::geo::GeometryType::PointZ:
                        geojson["type"] = "Point";
                        if (!geomInfo.coordinates.empty()) {
                            const auto& c = geomInfo.coordinates[0];
                            geojson["coordinates"] = {c.x, c.y};
                            if (geomInfo.type == utils::geo::GeometryType::PointZ) {
                                geojson["coordinates"].push_back(c.z);
                            }
                        }
                        break;
                    case utils::geo::GeometryType::LineString:
                    case utils::geo::GeometryType::LineStringZ:
                        geojson["type"] = "LineString";
                        geojson["coordinates"] = nlohmann::json::array();
                        for (const auto& c : geomInfo.coordinates) {
                            if (geomInfo.type == utils::geo::GeometryType::LineStringZ) {
                                geojson["coordinates"].push_back({c.x, c.y, c.z});
                            } else {
                                geojson["coordinates"].push_back({c.x, c.y});
                            }
                        }
                        break;
                    case utils::geo::GeometryType::Polygon:
                    case utils::geo::GeometryType::PolygonZ:
                        geojson["type"] = "Polygon";
                        geojson["coordinates"] = nlohmann::json::array();
                        // Note: Polygon rings not fully parsed in current EWKB parser
                        // This is a simplified version
                        {
                            nlohmann::json ring = nlohmann::json::array();
                            for (const auto& c : geomInfo.coordinates) {
                                if (geomInfo.type == utils::geo::GeometryType::PolygonZ) {
                                    ring.push_back({c.x, c.y, c.z});
                                } else {
                                    ring.push_back({c.x, c.y});
                                }
                            }
                            geojson["coordinates"].push_back(ring);
                        }
                        break;
                    default:
                        throw std::runtime_error("ST_AsGeoJSON: Unsupported geometry type");
                }
                
                return geojson.dump();
            } catch (const std::exception& e) {
                throw std::runtime_error("ST_AsGeoJSON: Failed to parse EWKB: " + std::string(e.what()));
            }
        }
        
        throw std::runtime_error("ST_AsGeoJSON: Argument must be GeoJSON object or EWKB binary");
    }

    // ST_Distance(geom1, geom2) - Euclidean distance between two geometries
    // Returns: Distance in coordinate system units (typically meters for geographic data)
    if (funcName == "ST_Distance") {
        if (args.size() != 2) {
            throw std::runtime_error("ST_Distance expects 2 arguments: ST_Distance(geom1, geom2)");
        }
        
        auto g1 = evaluateExpression(args[0], currentDoc);
        auto g2 = evaluateExpression(args[1], currentDoc);
        
        // Helper to extract Point coordinates from GeoJSON
        auto extractPoint = [](const nlohmann::json& geojson) -> std::pair<double, double> {
            if (geojson.is_object() && geojson.contains("type") && geojson["type"] == "Point") {
                if (geojson.contains("coordinates") && geojson["coordinates"].size() >= 2) {
                    double x = geojson["coordinates"][0].get<double>();
                    double y = geojson["coordinates"][1].get<double>();
                    return {x, y};
                }
            }
            throw std::runtime_error("ST_Distance: Expected Point geometry");
        };
        
        auto [x1, y1] = extractPoint(g1);
        auto [x2, y2] = extractPoint(g2);
        
        // Euclidean distance
        double dx = x2 - x1;
        double dy = y2 - y1;
        double distance = std::sqrt(dx * dx + dy * dy);
        
        return distance;
    }

    // ST_Intersects(geom1, geom2) - Test if two geometries spatially intersect
    // Returns: Boolean true if geometries intersect
    if (funcName == "ST_Intersects") {
        if (args.size() != 2) {
            throw std::runtime_error("ST_Intersects expects 2 arguments: ST_Intersects(geom1, geom2)");
        }
        
        auto g1 = evaluateExpression(args[0], currentDoc);
        auto g2 = evaluateExpression(args[1], currentDoc);
        
        // For now, implement Point-Point intersection (same location within epsilon)
        // Full implementation would use Boost.Geometry with all geometry types
        auto extractPoint = [](const nlohmann::json& geojson) -> std::pair<double, double> {
            if (geojson.is_object() && geojson.contains("type") && geojson["type"] == "Point") {
                if (geojson.contains("coordinates") && geojson["coordinates"].size() >= 2) {
                    double x = geojson["coordinates"][0].get<double>();
                    double y = geojson["coordinates"][1].get<double>();
                    return {x, y};
                }
            }
            throw std::runtime_error("ST_Intersects: Expected Point geometry (full geometry support coming)");
        };
        
        auto [x1, y1] = extractPoint(g1);
        auto [x2, y2] = extractPoint(g2);
        
        const double epsilon = 1e-9;
        bool intersects = (std::abs(x1 - x2) < epsilon && std::abs(y1 - y2) < epsilon);
        
        return intersects;
    }

    // ST_Within(geom1, geom2) - Test if geom1 is completely inside geom2
    // Returns: Boolean true if geom1 is within geom2
    if (funcName == "ST_Within") {
        if (args.size() != 2) {
            throw std::runtime_error("ST_Within expects 2 arguments: ST_Within(geom1, geom2)");
        }
        
        auto g1 = evaluateExpression(args[0], currentDoc);
        auto g2 = evaluateExpression(args[1], currentDoc);
        
        // Simplified implementation: Check if Point g1 is within Polygon g2 using MBR
        // Full implementation would use Boost.Geometry within()
        
        auto extractPoint = [](const nlohmann::json& geojson) -> std::pair<double, double> {
            if (geojson.is_object() && geojson.contains("type") && geojson["type"] == "Point") {
                if (geojson.contains("coordinates") && geojson["coordinates"].size() >= 2) {
                    double x = geojson["coordinates"][0].get<double>();
                    double y = geojson["coordinates"][1].get<double>();
                    return {x, y};
                }
            }
            throw std::runtime_error("ST_Within: Expected Point geometry");
        };
        
        auto extractMBR = [](const nlohmann::json& geojson) -> utils::geo::MBR {
            // Extract MBR from Polygon or use Point as degenerate MBR
            if (geojson.is_object() && geojson.contains("type")) {
                std::string type = geojson["type"];
                if (type == "Point" && geojson.contains("coordinates") && geojson["coordinates"].size() >= 2) {
                    double x = geojson["coordinates"][0].get<double>();
                    double y = geojson["coordinates"][1].get<double>();
                    return utils::geo::MBR{x, y, x, y};
                }
                if (type == "Polygon" && geojson.contains("coordinates")) {
                    // Compute MBR from polygon exterior ring
                    const auto& rings = geojson["coordinates"];
                    if (rings.is_array() && !rings.empty()) {
                        const auto& exteriorRing = rings[0];
                        if (exteriorRing.is_array() && !exteriorRing.empty()) {
                            double minx = std::numeric_limits<double>::max();
                            double miny = std::numeric_limits<double>::max();
                            double maxx = std::numeric_limits<double>::lowest();
                            double maxy = std::numeric_limits<double>::lowest();
                            
                            for (const auto& coord : exteriorRing) {
                                if (coord.is_array() && coord.size() >= 2) {
                                    double x = coord[0].get<double>();
                                    double y = coord[1].get<double>();
                                    minx = std::min(minx, x);
                                    miny = std::min(miny, y);
                                    maxx = std::max(maxx, x);
                                    maxy = std::max(maxy, y);
                                }
                            }
                            
                            return utils::geo::MBR{minx, miny, maxx, maxy};
                        }
                    }
                }
            }
            throw std::runtime_error("ST_Within: Could not extract MBR from geometry");
        };
        
        auto [px, py] = extractPoint(g1);
        auto mbr = extractMBR(g2);
        
        // Check if point is within MBR (simplified within test)
        bool within = (px >= mbr.minx && px <= mbr.maxx && py >= mbr.miny && py <= mbr.maxy);
        
        return within;
    }

    // ST_GeomFromGeoJSON(json_string) - Parse GeoJSON string to geometry object
    // Returns: GeoJSON object (same as ST_Point returns)
    if (funcName == "ST_GeomFromGeoJSON") {
        if (args.size() != 1) {
            throw std::runtime_error("ST_GeomFromGeoJSON expects 1 argument: ST_GeomFromGeoJSON(json_string)");
        }
        
        auto jsonArg = evaluateExpression(args[0], currentDoc);
        
        // If already a GeoJSON object, return as-is
        if (jsonArg.is_object() && jsonArg.contains("type") && jsonArg.contains("coordinates")) {
            return jsonArg;
        }
        
        // If string, parse it
        if (jsonArg.is_string()) {
            std::string jsonStr = jsonArg.get<std::string>();
            try {
                nlohmann::json geojson = nlohmann::json::parse(jsonStr);
                
                // Validate GeoJSON structure
                if (!geojson.is_object() || !geojson.contains("type") || !geojson.contains("coordinates")) {
                    throw std::runtime_error("Invalid GeoJSON: must have 'type' and 'coordinates'");
                }
                
                return geojson;
            } catch (const std::exception& e) {
                throw std::runtime_error("ST_GeomFromGeoJSON: Failed to parse JSON: " + std::string(e.what()));
            }
        }
        
        throw std::runtime_error("ST_GeomFromGeoJSON: Argument must be GeoJSON object or JSON string");
    }

    // ST_Contains(g1, g2) - Test if g1 completely contains g2
    // Returns: Boolean true if g1 contains g2 (inverse of ST_Within)
    if (funcName == "ST_Contains") {
        if (args.size() != 2) {
            throw std::runtime_error("ST_Contains expects 2 arguments: ST_Contains(geom1, geom2)");
        }
        
        auto g1 = evaluateExpression(args[0], currentDoc);
        auto g2 = evaluateExpression(args[1], currentDoc);
        
        // Simplified: MBR containment check
        // g1 contains g2 if g2's MBR is completely inside g1's MBR
        
        auto extractMBR = [](const nlohmann::json& geojson) -> utils::geo::MBR {
            if (geojson.is_object() && geojson.contains("type")) {
                std::string type = geojson["type"];
                if (type == "Point" && geojson.contains("coordinates") && geojson["coordinates"].size() >= 2) {
                    double x = geojson["coordinates"][0].get<double>();
                    double y = geojson["coordinates"][1].get<double>();
                    return utils::geo::MBR{x, y, x, y};
                }
                if (type == "Polygon" && geojson.contains("coordinates")) {
                    const auto& rings = geojson["coordinates"];
                    if (rings.is_array() && !rings.empty()) {
                        const auto& exteriorRing = rings[0];
                        if (exteriorRing.is_array() && !exteriorRing.empty()) {
                            double minx = std::numeric_limits<double>::max();
                            double miny = std::numeric_limits<double>::max();
                            double maxx = std::numeric_limits<double>::lowest();
                            double maxy = std::numeric_limits<double>::lowest();
                            
                            for (const auto& coord : exteriorRing) {
                                if (coord.is_array() && coord.size() >= 2) {
                                    double x = coord[0].get<double>();
                                    double y = coord[1].get<double>();
                                    minx = std::min(minx, x);
                                    miny = std::min(miny, y);
                                    maxx = std::max(maxx, x);
                                    maxy = std::max(maxy, y);
                                }
                            }
                            
                            return utils::geo::MBR{minx, miny, maxx, maxy};
                        }
                    }
                }
            }
            throw std::runtime_error("ST_Contains: Could not extract MBR from geometry");
        };
        
        auto mbr1 = extractMBR(g1);
        auto mbr2 = extractMBR(g2);
        
        // MBR containment: g1 contains g2 if g2's bounds are within g1's bounds
        bool contains = (mbr2.minx >= mbr1.minx && mbr2.maxx <= mbr1.maxx &&
                        mbr2.miny >= mbr1.miny && mbr2.maxy <= mbr1.maxy);
        
        return contains;
    }

    // ST_DWithin(g1, g2, distance) - Check if geometries are within distance
    // Returns: Boolean true if distance between g1 and g2 <= distance
    if (funcName == "ST_DWithin") {
        if (args.size() != 3) {
            throw std::runtime_error("ST_DWithin expects 3 arguments: ST_DWithin(geom1, geom2, distance)");
        }
        
        auto g1 = evaluateExpression(args[0], currentDoc);
        auto g2 = evaluateExpression(args[1], currentDoc);
        auto distArg = evaluateExpression(args[2], currentDoc);
        
        double maxDistance = toNumber(distArg);
        
        // Extract Point coordinates (simplified for Point-Point distance)
        auto extractPoint = [](const nlohmann::json& geojson) -> std::pair<double, double> {
            if (geojson.is_object() && geojson.contains("type") && geojson["type"] == "Point") {
                if (geojson.contains("coordinates") && geojson["coordinates"].size() >= 2) {
                    double x = geojson["coordinates"][0].get<double>();
                    double y = geojson["coordinates"][1].get<double>();
                    return {x, y};
                }
            }
            throw std::runtime_error("ST_DWithin: Expected Point geometry");
        };
        
        auto [x1, y1] = extractPoint(g1);
        auto [x2, y2] = extractPoint(g2);
        
        // Euclidean distance
        double dx = x2 - x1;
        double dy = y2 - y1;
        double distance = std::sqrt(dx * dx + dy * dy);
        
        return distance <= maxDistance;
    }

    // ST_HasZ(geom) - Check if geometry has Z coordinate
    // Returns: Boolean true if geometry is 3D
    if (funcName == "ST_HasZ") {
        if (args.size() != 1) {
            throw std::runtime_error("ST_HasZ expects 1 argument");
        }
        
        auto geom = evaluateExpression(args[0], currentDoc);
        
        if (geom.is_object() && geom.contains("type") && geom.contains("coordinates")) {
            const auto& coords = geom["coordinates"];
            std::string type = geom["type"];
            
            if (type == "Point" && coords.is_array() && coords.size() >= 3) {
                return true;
            }
            if ((type == "LineString" || type == "MultiPoint") && coords.is_array() && !coords.empty()) {
                if (coords[0].is_array() && coords[0].size() >= 3) {
                    return true;
                }
            }
            if (type == "Polygon" && coords.is_array() && !coords.empty()) {
                const auto& ring = coords[0];
                if (ring.is_array() && !ring.empty() && ring[0].is_array() && ring[0].size() >= 3) {
                    return true;
                }
            }
        }
        
        return false;
    }

    // ST_Z(point) - Extract Z coordinate from Point
    // Returns: Z value or null if no Z
    if (funcName == "ST_Z") {
        if (args.size() != 1) {
            throw std::runtime_error("ST_Z expects 1 argument");
        }
        
        auto geom = evaluateExpression(args[0], currentDoc);
        
        if (geom.is_object() && geom.contains("type") && geom["type"] == "Point") {
            if (geom.contains("coordinates") && geom["coordinates"].is_array() && geom["coordinates"].size() >= 3) {
                return geom["coordinates"][2];
            }
        }
        
        return nlohmann::json(nullptr);
    }

    // ST_ZMin(geom) - Extract minimum Z value from geometry
    // Returns: Minimum Z or null if 2D
    if (funcName == "ST_ZMin") {
        if (args.size() != 1) {
            throw std::runtime_error("ST_ZMin expects 1 argument");
        }
        
        auto geom = evaluateExpression(args[0], currentDoc);
        
        if (!geom.is_object() || !geom.contains("type") || !geom.contains("coordinates")) {
            return nlohmann::json(nullptr);
        }
        
        std::string type = geom["type"];
        const auto& coords = geom["coordinates"];
        double zmin = std::numeric_limits<double>::max();
        bool hasZ = false;
        
        if (type == "Point" && coords.is_array() && coords.size() >= 3) {
            return coords[2];
        }
        
        if ((type == "LineString" || type == "MultiPoint") && coords.is_array()) {
            for (const auto& pt : coords) {
                if (pt.is_array() && pt.size() >= 3) {
                    double z = pt[2].get<double>();
                    zmin = std::min(zmin, z);
                    hasZ = true;
                }
            }
        }
        
        if (type == "Polygon" && coords.is_array()) {
            for (const auto& ring : coords) {
                if (ring.is_array()) {
                    for (const auto& pt : ring) {
                        if (pt.is_array() && pt.size() >= 3) {
                            double z = pt[2].get<double>();
                            zmin = std::min(zmin, z);
                            hasZ = true;
                        }
                    }
                }
            }
        }
        
        return hasZ ? nlohmann::json(zmin) : nlohmann::json(nullptr);
    }

    // ST_ZMax(geom) - Extract maximum Z value from geometry
    // Returns: Maximum Z or null if 2D
    if (funcName == "ST_ZMax") {
        if (args.size() != 1) {
            throw std::runtime_error("ST_ZMax expects 1 argument");
        }
        
        auto geom = evaluateExpression(args[0], currentDoc);
        
        if (!geom.is_object() || !geom.contains("type") || !geom.contains("coordinates")) {
            return nlohmann::json(nullptr);
        }
        
        std::string type = geom["type"];
        const auto& coords = geom["coordinates"];
        double zmax = std::numeric_limits<double>::lowest();
        bool hasZ = false;
        
        if (type == "Point" && coords.is_array() && coords.size() >= 3) {
            return coords[2];
        }
        
        if ((type == "LineString" || type == "MultiPoint") && coords.is_array()) {
            for (const auto& pt : coords) {
                if (pt.is_array() && pt.size() >= 3) {
                    double z = pt[2].get<double>();
                    zmax = std::max(zmax, z);
                    hasZ = true;
                }
            }
        }
        
        if (type == "Polygon" && coords.is_array()) {
            for (const auto& ring : coords) {
                if (ring.is_array()) {
                    for (const auto& pt : ring) {
                        if (pt.is_array() && pt.size() >= 3) {
                            double z = pt[2].get<double>();
                            zmax = std::max(zmax, z);
                            hasZ = true;
                        }
                    }
                }
            }
        }
        
        return hasZ ? nlohmann::json(zmax) : nlohmann::json(nullptr);
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
