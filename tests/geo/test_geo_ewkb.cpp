#include <gtest/gtest.h>
#include "utils/geo/ewkb.h"
#include <cmath>

using namespace themis::geo;

class EWKBTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    
    bool approxEqual(double a, double b, double epsilon = 1e-9) {
        return std::fabs(a - b) < epsilon;
    }
};

// Test: Point 2D parsing
TEST_F(EWKBTest, ParsePoint2D) {
    // Create EWKB for Point(13.4, 52.5) - Berlin
    std::vector<uint8_t> ewkb = {
        0x01,  // Little Endian
        0x01, 0x00, 0x00, 0x00,  // Type: Point
        // X: 13.4
        0xCD, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x2A, 0x40,
        // Y: 52.5
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x4A, 0x40
    };
    
    auto geom = EWKBParser::parse(ewkb);
    
    EXPECT_EQ(geom.type, GeometryType::Point);
    EXPECT_FALSE(geom.has_z);
    EXPECT_EQ(geom.coords.size(), 1);
    EXPECT_TRUE(approxEqual(geom.coords[0].x, 13.4));
    EXPECT_TRUE(approxEqual(geom.coords[0].y, 52.5));
    EXPECT_FALSE(geom.coords[0].hasZ());
}

// Test: Point 3D parsing
TEST_F(EWKBTest, ParsePoint3D) {
    GeometryInfo geom(GeometryType::PointZ);
    geom.has_z = true;
    geom.coords.emplace_back(13.4, 52.5, 100.5);  // Berlin with elevation
    
    auto ewkb = EWKBParser::serialize(geom);
    auto parsed = EWKBParser::parse(ewkb);
    
    EXPECT_EQ(parsed.type, GeometryType::PointZ);
    EXPECT_TRUE(parsed.has_z);
    EXPECT_EQ(parsed.coords.size(), 1);
    EXPECT_TRUE(approxEqual(parsed.coords[0].x, 13.4));
    EXPECT_TRUE(approxEqual(parsed.coords[0].y, 52.5));
    EXPECT_TRUE(parsed.coords[0].hasZ());
    EXPECT_TRUE(approxEqual(parsed.coords[0].getZ(), 100.5));
}

// Test: LineString parsing
TEST_F(EWKBTest, ParseLineString) {
    GeometryInfo geom(GeometryType::LineString);
    geom.coords.emplace_back(0.0, 0.0);
    geom.coords.emplace_back(1.0, 1.0);
    geom.coords.emplace_back(2.0, 0.0);
    
    auto ewkb = EWKBParser::serialize(geom);
    auto parsed = EWKBParser::parse(ewkb);
    
    EXPECT_EQ(parsed.type, GeometryType::LineString);
    EXPECT_EQ(parsed.coords.size(), 3);
    EXPECT_TRUE(approxEqual(parsed.coords[0].x, 0.0));
    EXPECT_TRUE(approxEqual(parsed.coords[1].y, 1.0));
    EXPECT_TRUE(approxEqual(parsed.coords[2].x, 2.0));
}

// Test: Polygon parsing
TEST_F(EWKBTest, ParsePolygon) {
    GeometryInfo geom(GeometryType::Polygon);
    
    // Exterior ring (square)
    std::vector<Coordinate> ring;
    ring.emplace_back(0.0, 0.0);
    ring.emplace_back(1.0, 0.0);
    ring.emplace_back(1.0, 1.0);
    ring.emplace_back(0.0, 1.0);
    ring.emplace_back(0.0, 0.0);  // Closed
    geom.rings.push_back(ring);
    
    auto ewkb = EWKBParser::serialize(geom);
    auto parsed = EWKBParser::parse(ewkb);
    
    EXPECT_EQ(parsed.type, GeometryType::Polygon);
    EXPECT_EQ(parsed.rings.size(), 1);
    EXPECT_EQ(parsed.rings[0].size(), 5);
    EXPECT_TRUE(approxEqual(parsed.rings[0][0].x, 0.0));
    EXPECT_TRUE(approxEqual(parsed.rings[0][2].y, 1.0));
}

// Test: MBR computation
TEST_F(EWKBTest, ComputeMBR) {
    GeometryInfo geom(GeometryType::LineString);
    geom.coords.emplace_back(0.0, 0.0);
    geom.coords.emplace_back(5.0, 10.0);
    geom.coords.emplace_back(-2.0, 3.0);
    
    auto mbr = geom.computeMBR();
    
    EXPECT_TRUE(approxEqual(mbr.minx, -2.0));
    EXPECT_TRUE(approxEqual(mbr.maxx, 5.0));
    EXPECT_TRUE(approxEqual(mbr.miny, 0.0));
    EXPECT_TRUE(approxEqual(mbr.maxy, 10.0));
}

// Test: MBR 3D computation
TEST_F(EWKBTest, ComputeMBR3D) {
    GeometryInfo geom(GeometryType::LineStringZ);
    geom.has_z = true;
    geom.coords.emplace_back(0.0, 0.0, 10.0);
    geom.coords.emplace_back(5.0, 10.0, 50.0);
    geom.coords.emplace_back(-2.0, 3.0, 5.0);
    
    auto mbr = geom.computeMBR();
    
    EXPECT_TRUE(mbr.hasZ());
    EXPECT_TRUE(approxEqual(mbr.z_min.value(), 5.0));
    EXPECT_TRUE(approxEqual(mbr.z_max.value(), 50.0));
}

// Test: Centroid computation
TEST_F(EWKBTest, ComputeCentroid) {
    GeometryInfo geom(GeometryType::LineString);
    geom.coords.emplace_back(0.0, 0.0);
    geom.coords.emplace_back(2.0, 0.0);
    geom.coords.emplace_back(2.0, 2.0);
    geom.coords.emplace_back(0.0, 2.0);
    
    auto centroid = geom.computeCentroid();
    
    EXPECT_TRUE(approxEqual(centroid.x, 1.0));
    EXPECT_TRUE(approxEqual(centroid.y, 1.0));
}

// Test: Sidecar computation
TEST_F(EWKBTest, ComputeSidecar) {
    GeometryInfo geom(GeometryType::PointZ);
    geom.has_z = true;
    geom.coords.emplace_back(13.4, 52.5, 100.5);
    
    auto sidecar = EWKBParser::computeSidecar(geom);
    
    EXPECT_TRUE(approxEqual(sidecar.mbr.minx, 13.4));
    EXPECT_TRUE(approxEqual(sidecar.mbr.maxx, 13.4));
    EXPECT_TRUE(approxEqual(sidecar.centroid.x, 13.4));
    EXPECT_TRUE(approxEqual(sidecar.centroid.y, 52.5));
    EXPECT_TRUE(approxEqual(sidecar.z_min, 100.5));
    EXPECT_TRUE(approxEqual(sidecar.z_max, 100.5));
}

// Test: MBR intersection
TEST_F(EWKBTest, MBRIntersection) {
    MBR mbr1(0.0, 0.0, 10.0, 10.0);
    MBR mbr2(5.0, 5.0, 15.0, 15.0);
    MBR mbr3(20.0, 20.0, 30.0, 30.0);
    
    EXPECT_TRUE(mbr1.intersects(mbr2));
    EXPECT_TRUE(mbr2.intersects(mbr1));
    EXPECT_FALSE(mbr1.intersects(mbr3));
}

// Test: MBR contains point
TEST_F(EWKBTest, MBRContainsPoint) {
    MBR mbr(0.0, 0.0, 10.0, 10.0);
    
    EXPECT_TRUE(mbr.contains(5.0, 5.0));
    EXPECT_TRUE(mbr.contains(0.0, 0.0));  // Edge
    EXPECT_TRUE(mbr.contains(10.0, 10.0));  // Edge
    EXPECT_FALSE(mbr.contains(15.0, 5.0));
}

// Test: GeoJSON parsing (Point)
TEST_F(EWKBTest, GeoJSONPoint) {
    std::string geojson = R"({"type":"Point","coordinates":[13.4,52.5]})";
    
    auto geom = EWKBParser::parseGeoJSON(geojson);
    
    EXPECT_EQ(geom.type, GeometryType::Point);
    EXPECT_EQ(geom.coords.size(), 1);
    EXPECT_TRUE(approxEqual(geom.coords[0].x, 13.4));
    EXPECT_TRUE(approxEqual(geom.coords[0].y, 52.5));
}

// Test: GeoJSON 3D Point
TEST_F(EWKBTest, GeoJSONPoint3D) {
    std::string geojson = R"({"type":"Point","coordinates":[13.4,52.5,100.5]})";
    
    auto geom = EWKBParser::parseGeoJSON(geojson);
    
    EXPECT_EQ(geom.type, GeometryType::PointZ);
    EXPECT_TRUE(geom.has_z);
    EXPECT_TRUE(approxEqual(geom.coords[0].getZ(), 100.5));
}

// Test: To GeoJSON
TEST_F(EWKBTest, ToGeoJSON) {
    GeometryInfo geom(GeometryType::Point);
    geom.coords.emplace_back(13.4, 52.5);
    
    std::string geojson = EWKBParser::toGeoJSON(geom);
    
    EXPECT_TRUE(geojson.find("\"type\":\"Point\"") != std::string::npos);
    EXPECT_TRUE(geojson.find("13.4") != std::string::npos);
    EXPECT_TRUE(geojson.find("52.5") != std::string::npos);
}

// Test: EWKB validation
TEST_F(EWKBTest, ValidateEWKB) {
    GeometryInfo geom(GeometryType::Point);
    geom.coords.emplace_back(13.4, 52.5);
    
    auto ewkb = EWKBParser::serialize(geom);
    EXPECT_TRUE(EWKBParser::validate(ewkb));
    
    // Invalid EWKB (too short)
    std::vector<uint8_t> invalid = {0x01, 0x02};
    EXPECT_FALSE(EWKBParser::validate(invalid));
}

// Test: Round-trip (serialize -> parse)
TEST_F(EWKBTest, RoundTrip) {
    GeometryInfo original(GeometryType::PolygonZ);
    original.has_z = true;
    
    std::vector<Coordinate> ring;
    ring.emplace_back(0.0, 0.0, 10.0);
    ring.emplace_back(10.0, 0.0, 20.0);
    ring.emplace_back(10.0, 10.0, 30.0);
    ring.emplace_back(0.0, 10.0, 15.0);
    ring.emplace_back(0.0, 0.0, 10.0);
    original.rings.push_back(ring);
    
    auto ewkb = EWKBParser::serialize(original);
    auto parsed = EWKBParser::parse(ewkb);
    
    EXPECT_EQ(parsed.type, original.type);
    EXPECT_EQ(parsed.has_z, original.has_z);
    EXPECT_EQ(parsed.rings.size(), original.rings.size());
    EXPECT_EQ(parsed.rings[0].size(), original.rings[0].size());
    
    for (size_t i = 0; i < parsed.rings[0].size(); ++i) {
        EXPECT_TRUE(approxEqual(parsed.rings[0][i].x, original.rings[0][i].x));
        EXPECT_TRUE(approxEqual(parsed.rings[0][i].y, original.rings[0][i].y));
        EXPECT_TRUE(approxEqual(parsed.rings[0][i].getZ(), original.rings[0][i].getZ()));
    }
}

 
