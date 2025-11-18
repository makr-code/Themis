#include <gtest/gtest.h>
#include "query/let_evaluator.h"
#include "query/aql_parser.h"
#include <nlohmann/json.hpp>
#include <cmath>

using namespace themis;
using namespace themis::query;
using json = nlohmann::json;

class STFunctionsTest : public ::testing::Test {
protected:
    LetEvaluator evaluator;

    void SetUp() override {
        evaluator.clear();
    }

    bool approxEqual(double a, double b, double epsilon = 1e-6) {
        return std::fabs(a - b) < epsilon;
    }

    // Helper: Call function with args
    json callFunction(const std::string& funcName, const std::vector<json>& args) {
        LetNode letNode;
        letNode.variable = "result";
        
        auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
        funcCall->functionName = funcName;
        
        for (const auto& arg : args) {
            funcCall->arguments.push_back(
                std::make_shared<Expression::LiteralExpression>(arg)
            );
        }
        
        letNode.expression = funcCall;
        
        json emptyDoc = json::object();
        if (!evaluator.evaluateLet(letNode, emptyDoc)) {
            return json::object({{"error", "evaluation_failed"}});
        }
        
        auto result = evaluator.resolveVariable("result");
        return result.has_value() ? result.value() : json();
    }
};

// ============================================================================
// CONSTRUCTORS (3/3)
// ============================================================================

TEST_F(STFunctionsTest, ST_Point_Creates2DPoint) {
    json result = callFunction("ST_Point", {13.405, 52.52});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "Point");
    ASSERT_TRUE(result["coordinates"].is_array());
    EXPECT_EQ(result["coordinates"].size(), 2);
    EXPECT_DOUBLE_EQ(result["coordinates"][0], 13.405);
    EXPECT_DOUBLE_EQ(result["coordinates"][1], 52.52);
}

TEST_F(STFunctionsTest, ST_Point_NegativeCoordinates) {
    json result = callFunction("ST_Point", {-74.006, 40.7128}); // NYC
    
    EXPECT_EQ(result["type"], "Point");
    EXPECT_DOUBLE_EQ(result["coordinates"][0], -74.006);
    EXPECT_DOUBLE_EQ(result["coordinates"][1], 40.7128);
}

TEST_F(STFunctionsTest, ST_GeomFromGeoJSON_ValidPoint) {
    std::string geojson = R"({"type":"Point","coordinates":[13.405,52.52]})";
    json result = callFunction("ST_GeomFromGeoJSON", {geojson});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "Point");
    EXPECT_DOUBLE_EQ(result["coordinates"][0], 13.405);
    EXPECT_DOUBLE_EQ(result["coordinates"][1], 52.52);
}

TEST_F(STFunctionsTest, ST_GeomFromGeoJSON_LineString) {
    std::string geojson = R"({"type":"LineString","coordinates":[[0,0],[1,1],[2,1]]})";
    json result = callFunction("ST_GeomFromGeoJSON", {geojson});
    
    EXPECT_EQ(result["type"], "LineString");
    ASSERT_EQ(result["coordinates"].size(), 3);
    EXPECT_DOUBLE_EQ(result["coordinates"][0][0], 0.0);
    EXPECT_DOUBLE_EQ(result["coordinates"][2][1], 1.0);
}

TEST_F(STFunctionsTest, ST_GeomFromGeoJSON_InvalidJSON) {
    std::string invalid = "not a json";
    json result = callFunction("ST_GeomFromGeoJSON", {invalid});
    
    EXPECT_TRUE(result.is_null() || result.contains("error"));
}

TEST_F(STFunctionsTest, ST_GeomFromText_Point2D) {
    json result = callFunction("ST_GeomFromText", {"POINT(13.405 52.52)"});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "Point");
    EXPECT_DOUBLE_EQ(result["coordinates"][0], 13.405);
    EXPECT_DOUBLE_EQ(result["coordinates"][1], 52.52);
}

TEST_F(STFunctionsTest, ST_GeomFromText_Point3D) {
    json result = callFunction("ST_GeomFromText", {"POINT(13.405 52.52 35.0)"});
    
    EXPECT_EQ(result["type"], "Point");
    ASSERT_EQ(result["coordinates"].size(), 3);
    EXPECT_DOUBLE_EQ(result["coordinates"][0], 13.405);
    EXPECT_DOUBLE_EQ(result["coordinates"][1], 52.52);
    EXPECT_DOUBLE_EQ(result["coordinates"][2], 35.0);
}

TEST_F(STFunctionsTest, ST_GeomFromText_LineString) {
    json result = callFunction("ST_GeomFromText", {"LINESTRING(0 0, 1 1, 2 1, 2 2)"});
    
    EXPECT_EQ(result["type"], "LineString");
    ASSERT_EQ(result["coordinates"].size(), 4);
    EXPECT_DOUBLE_EQ(result["coordinates"][0][0], 0.0);
    EXPECT_DOUBLE_EQ(result["coordinates"][3][1], 2.0);
}

TEST_F(STFunctionsTest, ST_GeomFromText_Polygon) {
    json result = callFunction("ST_GeomFromText", {"POLYGON((0 0, 4 0, 4 4, 0 4, 0 0))"});
    
    EXPECT_EQ(result["type"], "Polygon");
    ASSERT_TRUE(result["coordinates"].is_array());
    ASSERT_GE(result["coordinates"].size(), 1); // Outer ring
    EXPECT_EQ(result["coordinates"][0].size(), 5); // 5 points (closed ring)
}

TEST_F(STFunctionsTest, ST_GeomFromText_InvalidWKT) {
    json result = callFunction("ST_GeomFromText", {"INVALID(1 2)"});
    
    EXPECT_TRUE(result.is_null() || result.contains("error"));
}

TEST_F(STFunctionsTest, ST_GeomFromText_EmptyString) {
    json result = callFunction("ST_GeomFromText", {""});
    
    EXPECT_TRUE(result.is_null());
}

// ============================================================================
// CONVERTERS (2/2)
// ============================================================================

TEST_F(STFunctionsTest, ST_AsGeoJSON_Point) {
    json point = {
        {"type", "Point"},
        {"coordinates", {13.405, 52.52}}
    };
    
    json result = callFunction("ST_AsGeoJSON", {point});
    
    ASSERT_TRUE(result.is_string());
    std::string geojson = result.get<std::string>();
    
    // Parse back to verify
    json parsed = json::parse(geojson);
    EXPECT_EQ(parsed["type"], "Point");
    EXPECT_DOUBLE_EQ(parsed["coordinates"][0], 13.405);
}

TEST_F(STFunctionsTest, ST_AsGeoJSON_LineString) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0}, {1.0, 1.0}}}
    };
    
    json result = callFunction("ST_AsGeoJSON", {line});
    
    ASSERT_TRUE(result.is_string());
    json parsed = json::parse(result.get<std::string>());
    EXPECT_EQ(parsed["type"], "LineString");
    EXPECT_EQ(parsed["coordinates"].size(), 2);
}

TEST_F(STFunctionsTest, ST_AsText_Point2D) {
    json point = {
        {"type", "Point"},
        {"coordinates", {13.405, 52.52}}
    };
    
    json result = callFunction("ST_AsText", {point});
    
    ASSERT_TRUE(result.is_string());
    std::string wkt = result.get<std::string>();
    EXPECT_EQ(wkt, "POINT(13.405 52.52)");
}

TEST_F(STFunctionsTest, ST_AsText_Point3D) {
    json point = {
        {"type", "Point"},
        {"coordinates", {13.405, 52.52, 35.0}}
    };
    
    json result = callFunction("ST_AsText", {point});
    
    ASSERT_TRUE(result.is_string());
    std::string wkt = result.get<std::string>();
    EXPECT_EQ(wkt, "POINT(13.405 52.52 35)");
}

TEST_F(STFunctionsTest, ST_AsText_LineString) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0}, {1.0, 1.0}, {2.0, 1.0}}}
    };
    
    json result = callFunction("ST_AsText", {line});
    
    ASSERT_TRUE(result.is_string());
    std::string wkt = result.get<std::string>();
    EXPECT_EQ(wkt, "LINESTRING(0 0, 1 1, 2 1)");
}

TEST_F(STFunctionsTest, ST_AsText_Polygon) {
    json polygon = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {0.0, 0.0}}}}
    };
    
    json result = callFunction("ST_AsText", {polygon});
    
    ASSERT_TRUE(result.is_string());
    std::string wkt = result.get<std::string>();
    EXPECT_EQ(wkt, "POLYGON((0 0, 4 0, 4 4, 0 4, 0 0))");
}

TEST_F(STFunctionsTest, ST_AsText_InvalidGeometry) {
    json invalid = {{"type", "Unknown"}};
    
    json result = callFunction("ST_AsText", {invalid});
    
    EXPECT_TRUE(result.is_null());
}

// ============================================================================
// PREDICATES (3/3)
// ============================================================================

TEST_F(STFunctionsTest, ST_Intersects_SamePoint) {
    json p1 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    json p2 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    
    json result = callFunction("ST_Intersects", {p1, p2});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_Intersects_DifferentPoints) {
    json p1 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    json p2 = {{"type", "Point"}, {"coordinates", {2.35, 48.86}}};
    
    json result = callFunction("ST_Intersects", {p1, p2});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_Intersects_NearbyPoints) {
    json p1 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    json p2 = {{"type", "Point"}, {"coordinates", {13.405001, 52.52}}};
    
    json result = callFunction("ST_Intersects", {p1, p2});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>()); // Within epsilon
}

TEST_F(STFunctionsTest, ST_Within_PointInBoundingBox) {
    json point = {{"type", "Point"}, {"coordinates", {2.0, 2.0}}};
    json bbox = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {0.0, 0.0}}}}
    };
    
    json result = callFunction("ST_Within", {point, bbox});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_Within_PointOutsideBoundingBox) {
    json point = {{"type", "Point"}, {"coordinates", {5.0, 5.0}}};
    json bbox = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {0.0, 0.0}}}}
    };
    
    json result = callFunction("ST_Within", {point, bbox});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_Within_EdgeCase) {
    json point = {{"type", "Point"}, {"coordinates", {4.0, 4.0}}}; // On corner
    json bbox = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {0.0, 0.0}}}}
    };
    
    json result = callFunction("ST_Within", {point, bbox});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>()); // Inclusive
}

TEST_F(STFunctionsTest, ST_Contains_PointInPolygon) {
    json bbox = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {0.0, 0.0}}}}
    };
    json point = {{"type", "Point"}, {"coordinates", {2.0, 2.0}}};
    
    json result = callFunction("ST_Contains", {bbox, point});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_Contains_PointOutside) {
    json bbox = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {0.0, 0.0}}}}
    };
    json point = {{"type", "Point"}, {"coordinates", {10.0, 10.0}}};
    
    json result = callFunction("ST_Contains", {bbox, point});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

// ============================================================================
// DISTANCE (3/3)
// ============================================================================

TEST_F(STFunctionsTest, ST_Distance_SamePoint) {
    json p1 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    json p2 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    
    json result = callFunction("ST_Distance", {p1, p2});
    
    ASSERT_TRUE(result.is_number());
    EXPECT_DOUBLE_EQ(result.get<double>(), 0.0);
}

TEST_F(STFunctionsTest, ST_Distance_BerlinParis) {
    json berlin = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    json paris = {{"type", "Point"}, {"coordinates", {2.35, 48.86}}};
    
    json result = callFunction("ST_Distance", {berlin, paris});
    
    ASSERT_TRUE(result.is_number());
    double distance = result.get<double>();
    
    // Expected: ~14.87 degrees (Euclidean in WGS84 coordinates)
    EXPECT_TRUE(approxEqual(distance, 14.87, 0.1));
}

TEST_F(STFunctionsTest, ST_Distance_SimpleCalculation) {
    json p1 = {{"type", "Point"}, {"coordinates", {0.0, 0.0}}};
    json p2 = {{"type", "Point"}, {"coordinates", {3.0, 4.0}}};
    
    json result = callFunction("ST_Distance", {p1, p2});
    
    ASSERT_TRUE(result.is_number());
    // Distance should be 5.0 (3-4-5 triangle)
    EXPECT_DOUBLE_EQ(result.get<double>(), 5.0);
}

TEST_F(STFunctionsTest, ST_DWithin_Nearby) {
    json p1 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    json p2 = {{"type", "Point"}, {"coordinates", {13.406, 52.521}}};
    
    json result = callFunction("ST_DWithin", {p1, p2, 0.01});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_DWithin_TooFar) {
    json p1 = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    json p2 = {{"type", "Point"}, {"coordinates", {2.35, 48.86}}}; // Paris
    
    json result = callFunction("ST_DWithin", {p1, p2, 1.0});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>()); // >14 degrees apart
}

TEST_F(STFunctionsTest, ST_DWithin_ExactDistance) {
    json p1 = {{"type", "Point"}, {"coordinates", {0.0, 0.0}}};
    json p2 = {{"type", "Point"}, {"coordinates", {3.0, 4.0}}};
    
    json result = callFunction("ST_DWithin", {p1, p2, 5.0});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>()); // Exactly 5.0 units
}

TEST_F(STFunctionsTest, ST_3DDistance_2DPoints) {
    json p1 = {{"type", "Point"}, {"coordinates", {0.0, 0.0}}};
    json p2 = {{"type", "Point"}, {"coordinates", {3.0, 4.0}}};
    
    json result = callFunction("ST_3DDistance", {p1, p2});
    
    ASSERT_TRUE(result.is_number());
    // Should fall back to 2D: sqrt(9+16) = 5.0
    EXPECT_DOUBLE_EQ(result.get<double>(), 5.0);
}

TEST_F(STFunctionsTest, ST_3DDistance_3DPoints) {
    json p1 = {{"type", "Point"}, {"coordinates", {0.0, 0.0, 0.0}}};
    json p2 = {{"type", "Point"}, {"coordinates", {1.0, 1.0, 1.0}}};
    
    json result = callFunction("ST_3DDistance", {p1, p2});
    
    ASSERT_TRUE(result.is_number());
    // Distance: sqrt(1+1+1) = sqrt(3) ≈ 1.732
    EXPECT_TRUE(approxEqual(result.get<double>(), 1.732050808, 1e-6));
}

TEST_F(STFunctionsTest, ST_3DDistance_Mixed2D3D) {
    json p1 = {{"type", "Point"}, {"coordinates", {0.0, 0.0}}};    // 2D (Z=0)
    json p2 = {{"type", "Point"}, {"coordinates", {0.0, 0.0, 10.0}}}; // 3D
    
    json result = callFunction("ST_3DDistance", {p1, p2});
    
    ASSERT_TRUE(result.is_number());
    // Distance: only Z differs by 10.0
    EXPECT_DOUBLE_EQ(result.get<double>(), 10.0);
}

// ============================================================================
// 3D SUPPORT (5/7)
// ============================================================================

TEST_F(STFunctionsTest, ST_HasZ_2DPoint) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    
    json result = callFunction("ST_HasZ", {point});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_HasZ_3DPoint) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52, 35.0}}};
    
    json result = callFunction("ST_HasZ", {point});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_HasZ_LineString2D) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0}, {1.0, 1.0}}}
    };
    
    json result = callFunction("ST_HasZ", {line});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_HasZ_LineString3D) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}}}
    };
    
    json result = callFunction("ST_HasZ", {line});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_Z_3DPoint) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52, 35.0}}};
    
    json result = callFunction("ST_Z", {point});
    
    ASSERT_TRUE(result.is_number());
    EXPECT_DOUBLE_EQ(result.get<double>(), 35.0);
}

TEST_F(STFunctionsTest, ST_Z_2DPoint) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    
    json result = callFunction("ST_Z", {point});
    
    EXPECT_TRUE(result.is_null());
}

TEST_F(STFunctionsTest, ST_Z_InvalidGeometry) {
    json invalid = {{"type", "LineString"}};
    
    json result = callFunction("ST_Z", {invalid});
    
    EXPECT_TRUE(result.is_null());
}

TEST_F(STFunctionsTest, ST_ZMin_3DLineString) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0, 10.0}, {1.0, 1.0, 5.0}, {2.0, 2.0, 20.0}}}
    };
    
    json result = callFunction("ST_ZMin", {line});
    
    ASSERT_TRUE(result.is_number());
    EXPECT_DOUBLE_EQ(result.get<double>(), 5.0);
}

TEST_F(STFunctionsTest, ST_ZMin_2DGeometry) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0}, {1.0, 1.0}}}
    };
    
    json result = callFunction("ST_ZMin", {line});
    
    EXPECT_TRUE(result.is_null());
}

TEST_F(STFunctionsTest, ST_ZMin_3DPoint) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52, 35.0}}};
    
    json result = callFunction("ST_ZMin", {point});
    
    ASSERT_TRUE(result.is_number());
    EXPECT_DOUBLE_EQ(result.get<double>(), 35.0);
}

TEST_F(STFunctionsTest, ST_ZMax_3DLineString) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0, 10.0}, {1.0, 1.0, 5.0}, {2.0, 2.0, 20.0}}}
    };
    
    json result = callFunction("ST_ZMax", {line});
    
    ASSERT_TRUE(result.is_number());
    EXPECT_DOUBLE_EQ(result.get<double>(), 20.0);
}

TEST_F(STFunctionsTest, ST_ZMax_2DGeometry) {
    json polygon = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}, {0.0, 0.0}}}}
    };
    
    json result = callFunction("ST_ZMax", {polygon});
    
    EXPECT_TRUE(result.is_null());
}

TEST_F(STFunctionsTest, ST_ZMax_NegativeZ) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0, -10.0}, {1.0, 1.0, -5.0}, {2.0, 2.0, -20.0}}}
    };
    
    json result = callFunction("ST_ZMax", {line});
    
    ASSERT_TRUE(result.is_number());
    EXPECT_DOUBLE_EQ(result.get<double>(), -5.0);
}

TEST_F(STFunctionsTest, ST_Force2D_3DPoint) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52, 35.0}}};
    
    json result = callFunction("ST_Force2D", {point});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "Point");
    ASSERT_EQ(result["coordinates"].size(), 2);
    EXPECT_DOUBLE_EQ(result["coordinates"][0], 13.405);
    EXPECT_DOUBLE_EQ(result["coordinates"][1], 52.52);
}

TEST_F(STFunctionsTest, ST_Force2D_2DPoint) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    
    json result = callFunction("ST_Force2D", {point});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "Point");
    ASSERT_EQ(result["coordinates"].size(), 2);
    EXPECT_DOUBLE_EQ(result["coordinates"][0], 13.405);
}

TEST_F(STFunctionsTest, ST_Force2D_3DLineString) {
    json line = {
        {"type", "LineString"},
        {"coordinates", {{0.0, 0.0, 1.0}, {1.0, 1.0, 2.0}, {2.0, 1.0, 3.0}}}
    };
    
    json result = callFunction("ST_Force2D", {line});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "LineString");
    ASSERT_EQ(result["coordinates"].size(), 3);
    
    for (const auto& coord : result["coordinates"]) {
        EXPECT_EQ(coord.size(), 2); // All 2D
    }
    
    EXPECT_DOUBLE_EQ(result["coordinates"][2][0], 2.0);
    EXPECT_DOUBLE_EQ(result["coordinates"][2][1], 1.0);
}

TEST_F(STFunctionsTest, ST_Force2D_3DPolygon) {
    json polygon = {
        {"type", "Polygon"},
        {"coordinates", {{{0.0, 0.0, 5.0}, {4.0, 0.0, 10.0}, {4.0, 4.0, 15.0}, {0.0, 4.0, 20.0}, {0.0, 0.0, 5.0}}}}
    };
    
    json result = callFunction("ST_Force2D", {polygon});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "Polygon");
    ASSERT_EQ(result["coordinates"].size(), 1); // One ring
    ASSERT_EQ(result["coordinates"][0].size(), 5); // 5 points
    
    for (const auto& coord : result["coordinates"][0]) {
        EXPECT_EQ(coord.size(), 2); // All 2D
    }
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

TEST_F(STFunctionsTest, ST_Point_MissingArguments) {
    json result = callFunction("ST_Point", {13.405}); // Only 1 arg
    
    EXPECT_TRUE(result.is_null() || result.contains("error"));
}

TEST_F(STFunctionsTest, ST_Distance_WrongArgumentType) {
    json point = {{"type", "Point"}, {"coordinates", {13.405, 52.52}}};
    
    json result = callFunction("ST_Distance", {point, "not a geometry"});
    
    EXPECT_TRUE(result.is_null() || result.contains("error"));
}

TEST_F(STFunctionsTest, ST_AsText_Null) {
    json result = callFunction("ST_AsText", {json()});
    
    EXPECT_TRUE(result.is_null());
}

TEST_F(STFunctionsTest, ST_HasZ_InvalidGeometry) {
    json invalid = {{"type", "Point"}}; // Missing coordinates
    
    json result = callFunction("ST_HasZ", {invalid});
    
    EXPECT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

// ============================================================================
// ST_ZBetween
// ============================================================================

TEST_F(STFunctionsTest, ST_ZBetween_PointInRange) {
    json p = {{"type", "Point"}, {"coordinates", {1.0, 2.0, 5.0}}};
    json result = callFunction("ST_ZBetween", {p, 4.0, 6.0});
    ASSERT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_ZBetween_PointOutOfRange) {
    json p = {{"type", "Point"}, {"coordinates", {1.0, 2.0, 3.9}}};
    json result = callFunction("ST_ZBetween", {p, 4.0, 6.0});
    ASSERT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_ZBetween_LineStringAnyInRange) {
    json line = {{"type", "LineString"}, {"coordinates", {{0.0, 0.0, 1.0}, {1.0, 1.0, 5.0}, {2.0, 2.0, 10.0}}}};
    json result = callFunction("ST_ZBetween", {line, 4.0, 6.0});
    ASSERT_TRUE(result.is_boolean());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(STFunctionsTest, ST_ZBetween_PolygonNoZ) {
    json polygon = { {"type", "Polygon"}, {"coordinates", {{{0.0,0.0},{4.0,0.0},{4.0,4.0},{0.0,4.0},{0.0,0.0}}}} };
    json result = callFunction("ST_ZBetween", {polygon, -10.0, 10.0});
    ASSERT_TRUE(result.is_boolean());
    EXPECT_FALSE(result.get<bool>());
}

// ============================================================================
// ST_Buffer (MVP) & ST_Union (MVP)
// ============================================================================

TEST_F(STFunctionsTest, ST_Buffer_PointSquare)
{
    json point = callFunction("ST_Point", {1.0, 2.0});
    json buffered = callFunction("ST_Buffer", {point, 0.5});

    ASSERT_TRUE(buffered.is_object());
    EXPECT_EQ(buffered["type"], "Polygon");
    ASSERT_TRUE(buffered["coordinates"].is_array());
    ASSERT_FALSE(buffered["coordinates"].empty());
    const auto& ring = buffered["coordinates"][0];
    ASSERT_EQ(ring.size(), 5); // closed square
    EXPECT_DOUBLE_EQ(ring[0][0], 0.5); EXPECT_DOUBLE_EQ(ring[0][1], 1.5);
    EXPECT_DOUBLE_EQ(ring[1][0], 1.5); EXPECT_DOUBLE_EQ(ring[1][1], 1.5);
    EXPECT_DOUBLE_EQ(ring[2][0], 1.5); EXPECT_DOUBLE_EQ(ring[2][1], 2.5);
    EXPECT_DOUBLE_EQ(ring[3][0], 0.5); EXPECT_DOUBLE_EQ(ring[3][1], 2.5);
    EXPECT_DOUBLE_EQ(ring[4][0], 0.5); EXPECT_DOUBLE_EQ(ring[4][1], 1.5);
}

TEST_F(STFunctionsTest, ST_Buffer_PolygonExpandMBR)
{
    json poly = callFunction("ST_GeomFromText", {"POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))"});
    json buffered = callFunction("ST_Buffer", {poly, 1.0});

    ASSERT_TRUE(buffered.is_object());
    EXPECT_EQ(buffered["type"], "Polygon");
    const auto& ring = buffered["coordinates"][0];
    ASSERT_EQ(ring.size(), 5);
    EXPECT_DOUBLE_EQ(ring[0][0], -1.0); EXPECT_DOUBLE_EQ(ring[0][1], -1.0);
    EXPECT_DOUBLE_EQ(ring[1][0],  3.0); EXPECT_DOUBLE_EQ(ring[1][1], -1.0);
    EXPECT_DOUBLE_EQ(ring[2][0],  3.0); EXPECT_DOUBLE_EQ(ring[2][1],  3.0);
    EXPECT_DOUBLE_EQ(ring[3][0], -1.0); EXPECT_DOUBLE_EQ(ring[3][1],  3.0);
    EXPECT_DOUBLE_EQ(ring[4][0], -1.0); EXPECT_DOUBLE_EQ(ring[4][1], -1.0);
}

TEST_F(STFunctionsTest, ST_Union_PointPolygonMBR)
{
    json p = callFunction("ST_Point", {0.0, 0.0});
    json q = callFunction("ST_GeomFromText", {"POLYGON((1 1, 2 1, 2 2, 1 2, 1 1))"});
    json uni = callFunction("ST_Union", {p, q});

    ASSERT_TRUE(uni.is_object());
    EXPECT_EQ(uni["type"], "Polygon");
    const auto& ring = uni["coordinates"][0];
    ASSERT_EQ(ring.size(), 5);
    EXPECT_DOUBLE_EQ(ring[0][0], 0.0); EXPECT_DOUBLE_EQ(ring[0][1], 0.0);
    EXPECT_DOUBLE_EQ(ring[1][0], 2.0); EXPECT_DOUBLE_EQ(ring[1][1], 0.0);
    EXPECT_DOUBLE_EQ(ring[2][0], 2.0); EXPECT_DOUBLE_EQ(ring[2][1], 2.0);
    EXPECT_DOUBLE_EQ(ring[3][0], 0.0); EXPECT_DOUBLE_EQ(ring[3][1], 2.0);
    EXPECT_DOUBLE_EQ(ring[4][0], 0.0); EXPECT_DOUBLE_EQ(ring[4][1], 0.0);
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

TEST_F(STFunctionsTest, Integration_WKT_RoundTrip) {
    // WKT → GeoJSON → WKT
    std::string original = "POINT(13.405 52.52)";
    
    json geojson = callFunction("ST_GeomFromText", {original});
    json wkt = callFunction("ST_AsText", {geojson});
    
    ASSERT_TRUE(wkt.is_string());
    EXPECT_EQ(wkt.get<std::string>(), original);
}

TEST_F(STFunctionsTest, Integration_GeoJSON_RoundTrip) {
    // GeoJSON → WKT → GeoJSON
    std::string original = R"({"type":"Point","coordinates":[13.405,52.52]})";
    
    json geojson1 = callFunction("ST_GeomFromGeoJSON", {original});
    json wkt = callFunction("ST_AsText", {geojson1});
    json geojson2 = callFunction("ST_GeomFromText", {wkt});
    
    EXPECT_EQ(geojson1, geojson2);
}

TEST_F(STFunctionsTest, Integration_3D_Pipeline) {
    // Create 3D point → Check Z → Force 2D → Verify
    json point3d = callFunction("ST_GeomFromText", {"POINT(1 2 3)"});
    
    json hasZ = callFunction("ST_HasZ", {point3d});
    EXPECT_TRUE(hasZ.get<bool>());
    
    json zValue = callFunction("ST_Z", {point3d});
    EXPECT_DOUBLE_EQ(zValue.get<double>(), 3.0);
    
    json point2d = callFunction("ST_Force2D", {point3d});
    
    json hasZ2 = callFunction("ST_HasZ", {point2d});
    EXPECT_FALSE(hasZ2.get<bool>());
    
    EXPECT_EQ(point2d["coordinates"].size(), 2);
}

TEST_F(STFunctionsTest, Integration_ProximitySearch) {
    // Create center point
    json center = callFunction("ST_Point", {13.405, 52.52});
    
    // Test nearby points
    json nearby = callFunction("ST_Point", {13.406, 52.521});
    json far = callFunction("ST_Point", {2.35, 48.86});
    
    json isNearby = callFunction("ST_DWithin", {center, nearby, 0.01});
    json isFar = callFunction("ST_DWithin", {center, far, 1.0});
    
    EXPECT_TRUE(isNearby.get<bool>());
    EXPECT_FALSE(isFar.get<bool>());
}

TEST_F(STFunctionsTest, Integration_BoundingBoxCheck) {
    // Create bounding box
    json bbox = callFunction("ST_GeomFromText", {"POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))"});
    
    // Test points
    json inside = callFunction("ST_Point", {5.0, 5.0});
    json outside = callFunction("ST_Point", {15.0, 15.0});
    
    json containsInside = callFunction("ST_Contains", {bbox, inside});
    json containsOutside = callFunction("ST_Contains", {bbox, outside});
    
    EXPECT_TRUE(containsInside.get<bool>());
    EXPECT_FALSE(containsOutside.get<bool>());
}

// Run all tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
