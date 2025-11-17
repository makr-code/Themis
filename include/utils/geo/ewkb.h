#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace themis {
namespace geo {

// WKB Geometry Types (PostGIS Extended)
enum class GeometryType : uint32_t {
    Point = 1,
    LineString = 2,
    Polygon = 3,
    MultiPoint = 4,
    MultiLineString = 5,
    MultiPolygon = 6,
    GeometryCollection = 7,
    
    // 3D variants (EWKB Z flag: type | 0x80000000)
    PointZ = 0x80000001,
    LineStringZ = 0x80000002,
    PolygonZ = 0x80000003,
    MultiPointZ = 0x80000004,
    MultiLineStringZ = 0x80000005,
    MultiPolygonZ = 0x80000006,
    GeometryCollectionZ = 0x80000007
};

// 2D/3D Coordinate
struct Coordinate {
    double x;
    double y;
    std::optional<double> z;  // For 3D geometries
    
    Coordinate() : x(0.0), y(0.0) {}
    Coordinate(double x_, double y_) : x(x_), y(y_) {}
    Coordinate(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    
    bool hasZ() const { return z.has_value(); }
    double getZ() const { return z.value_or(0.0); }
};

// Minimum Bounding Rectangle (2D + optional Z)
struct MBR {
    double minx = 0.0;
    double miny = 0.0;
    double maxx = 0.0;
    double maxy = 0.0;
    
    std::optional<double> z_min;
    std::optional<double> z_max;
    
    MBR() = default;
    MBR(double minx_, double miny_, double maxx_, double maxy_)
        : minx(minx_), miny(miny_), maxx(maxx_), maxy(maxy_) {}
    
    // Check if this MBR intersects with another
    bool intersects(const MBR& other) const {
        return !(minx > other.maxx || maxx < other.minx ||
                 miny > other.maxy || maxy < other.miny);
    }
    
    // Check if this MBR contains a point
    bool contains(double x, double y) const {
        return x >= minx && x <= maxx && y >= miny && y <= maxy;
    }
    
    // Expand MBR by distance (meters, approximate for lat/lon)
    MBR expand(double distance_meters) const;
    
    // Area in square degrees (approximate)
    double area() const {
        return (maxx - minx) * (maxy - miny);
    }
    
    // Center point
    Coordinate center() const {
        return Coordinate((minx + maxx) / 2.0, (miny + maxy) / 2.0);
    }
    
    bool hasZ() const { return z_min.has_value() && z_max.has_value(); }
};

// Geometry metadata (sidecar for fast filtering)
struct GeoSidecar {
    MBR mbr;              // 2D bounding box
    Coordinate centroid;  // Geometric center
    double z_min = 0.0;   // Min elevation (for 3D)
    double z_max = 0.0;   // Max elevation (for 3D)
    
    GeoSidecar() = default;
    explicit GeoSidecar(const MBR& mbr_) : mbr(mbr_), centroid(mbr_.center()) {}
};

// Parsed geometry information
struct GeometryInfo {
    GeometryType type;
    int srid = 4326;  // Default: WGS84
    bool has_z = false;
    bool has_m = false;  // Measure (not supported yet)
    
    std::vector<Coordinate> coords;  // Point: 1 coord, LineString: N coords, Polygon: N coords (ring)
    
    // For complex types (MultiPoint, Polygon with holes, etc.)
    std::vector<std::vector<Coordinate>> rings;  // Polygon rings
    std::vector<GeometryInfo> geometries;        // GeometryCollection
    
    GeometryInfo() = default;
    GeometryInfo(GeometryType type_) : type(type_) {}
    
    bool isPoint() const { return type == GeometryType::Point || type == GeometryType::PointZ; }
    bool isLineString() const { return type == GeometryType::LineString || type == GeometryType::LineStringZ; }
    bool isPolygon() const { return type == GeometryType::Polygon || type == GeometryType::PolygonZ; }
    bool hasZ() const { return has_z; }
    
    // Compute MBR from coordinates
    MBR computeMBR() const;
    
    // Compute centroid
    Coordinate computeCentroid() const;
};

// EWKB Parser/Serializer
class EWKBParser {
public:
    // Parse EWKB binary to GeometryInfo
    static GeometryInfo parse(const std::vector<uint8_t>& ewkb);
    
    // Serialize GeometryInfo to EWKB binary
    static std::vector<uint8_t> serialize(const GeometryInfo& geom);
    
    // Parse from WKT (Well-Known Text)
    static GeometryInfo parseWKT(const std::string& wkt);
    
    // Parse from GeoJSON geometry object
    static GeometryInfo parseGeoJSON(const std::string& geojson);
    
    // Serialize to GeoJSON geometry object
    static std::string toGeoJSON(const GeometryInfo& geom);
    
    // Serialize to WKT
    static std::string toWKT(const GeometryInfo& geom);
    
    // Compute sidecar metadata from geometry
    static GeoSidecar computeSidecar(const GeometryInfo& geom);
    
    // Validate EWKB format
    static bool validate(const std::vector<uint8_t>& ewkb);
    
private:
    // Internal parsing helpers
    static GeometryInfo parsePoint(const uint8_t*& ptr, bool has_z, bool is_little_endian);
    static GeometryInfo parseLineString(const uint8_t*& ptr, bool has_z, bool is_little_endian);
    static GeometryInfo parsePolygon(const uint8_t*& ptr, bool has_z, bool is_little_endian);
    
    // Binary read helpers
    static double readDouble(const uint8_t*& ptr, bool is_little_endian);
    static uint32_t readUInt32(const uint8_t*& ptr, bool is_little_endian);
    static void writeDouble(std::vector<uint8_t>& buf, double val, bool is_little_endian);
    static void writeUInt32(std::vector<uint8_t>& buf, uint32_t val, bool is_little_endian);
};

}  // namespace geo
}  // namespace themis
