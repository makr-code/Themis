#include "utils/geo/ewkb.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace themis {
namespace geo {

using json = nlohmann::json;

// Constants
constexpr double EARTH_RADIUS_METERS = 6371000.0;  // Mean Earth radius
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double METERS_PER_DEGREE_APPROX = 111320.0;  // At equator

// MBR expand by distance (approximate for lat/lon)
MBR MBR::expand(double distance_meters) const {
    double delta_deg = distance_meters / METERS_PER_DEGREE_APPROX;
    return MBR(
        minx - delta_deg,
        miny - delta_deg,
        maxx + delta_deg,
        maxy + delta_deg
    );
}

// GeometryInfo: Compute MBR
MBR GeometryInfo::computeMBR() const {
    if (coords.empty() && rings.empty()) {
        return MBR();
    }
    
    MBR mbr;
    mbr.minx = mbr.maxx = coords.empty() ? rings[0][0].x : coords[0].x;
    mbr.miny = mbr.maxy = coords.empty() ? rings[0][0].y : coords[0].y;
    
    std::optional<double> z_min, z_max;
    
    auto update_mbr = [&](const Coordinate& c) {
        mbr.minx = std::min(mbr.minx, c.x);
        mbr.maxx = std::max(mbr.maxx, c.x);
        mbr.miny = std::min(mbr.miny, c.y);
        mbr.maxy = std::max(mbr.maxy, c.y);
        
        if (c.hasZ()) {
            if (!z_min || c.getZ() < *z_min) z_min = c.getZ();
            if (!z_max || c.getZ() > *z_max) z_max = c.getZ();
        }
    };
    
    for (const auto& c : coords) {
        update_mbr(c);
    }
    
    for (const auto& ring : rings) {
        for (const auto& c : ring) {
            update_mbr(c);
        }
    }
    
    mbr.z_min = z_min;
    mbr.z_max = z_max;
    
    return mbr;
}

// GeometryInfo: Compute centroid
Coordinate GeometryInfo::computeCentroid() const {
    if (coords.empty() && rings.empty()) {
        return Coordinate();
    }
    
    double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
    size_t count = 0;
    bool has_z_coord = false;
    
    auto add_coord = [&](const Coordinate& c) {
        sum_x += c.x;
        sum_y += c.y;
        if (c.hasZ()) {
            sum_z += c.getZ();
            has_z_coord = true;
        }
        count++;
    };
    
    for (const auto& c : coords) {
        add_coord(c);
    }
    
    for (const auto& ring : rings) {
        for (const auto& c : ring) {
            add_coord(c);
        }
    }
    
    if (count == 0) {
        return Coordinate();
    }
    
    Coordinate centroid(sum_x / count, sum_y / count);
    if (has_z_coord) {
        centroid.z = sum_z / count;
    }
    
    return centroid;
}

// EWKB Parser: Read helpers
double EWKBParser::readDouble(const uint8_t*& ptr, bool is_little_endian) {
    double val;
    if (is_little_endian == true) {  // System is little endian
        std::memcpy(&val, ptr, sizeof(double));
    } else {
        // Byte swap for big endian
        uint8_t temp[sizeof(double)];
        for (size_t i = 0; i < sizeof(double); ++i) {
            temp[i] = ptr[sizeof(double) - 1 - i];
        }
        std::memcpy(&val, temp, sizeof(double));
    }
    ptr += sizeof(double);
    return val;
}

uint32_t EWKBParser::readUInt32(const uint8_t*& ptr, bool is_little_endian) {
    uint32_t val;
    if (is_little_endian == true) {
        std::memcpy(&val, ptr, sizeof(uint32_t));
    } else {
        uint8_t temp[sizeof(uint32_t)];
        for (size_t i = 0; i < sizeof(uint32_t); ++i) {
            temp[i] = ptr[sizeof(uint32_t) - 1 - i];
        }
        std::memcpy(&val, temp, sizeof(uint32_t));
    }
    ptr += sizeof(uint32_t);
    return val;
}

void EWKBParser::writeDouble(std::vector<uint8_t>& buf, double val, bool is_little_endian) {
    if (is_little_endian) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val);
        buf.insert(buf.end(), bytes, bytes + sizeof(double));
    } else {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val);
        for (int i = sizeof(double) - 1; i >= 0; --i) {
            buf.push_back(bytes[i]);
        }
    }
}

void EWKBParser::writeUInt32(std::vector<uint8_t>& buf, uint32_t val, bool is_little_endian) {
    if (is_little_endian) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val);
        buf.insert(buf.end(), bytes, bytes + sizeof(uint32_t));
    } else {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val);
        for (int i = sizeof(uint32_t) - 1; i >= 0; --i) {
            buf.push_back(bytes[i]);
        }
    }
}

// Parse Point
GeometryInfo EWKBParser::parsePoint(const uint8_t*& ptr, bool has_z, bool is_little_endian) {
    GeometryInfo geom(has_z ? GeometryType::PointZ : GeometryType::Point);
    geom.has_z = has_z;
    
    double x = readDouble(ptr, is_little_endian);
    double y = readDouble(ptr, is_little_endian);
    double z = has_z ? readDouble(ptr, is_little_endian) : 0.0;
    
    geom.coords.emplace_back(x, y, has_z ? std::optional<double>(z) : std::nullopt);
    return geom;
}

// Parse LineString
GeometryInfo EWKBParser::parseLineString(const uint8_t*& ptr, bool has_z, bool is_little_endian) {
    GeometryInfo geom(has_z ? GeometryType::LineStringZ : GeometryType::LineString);
    geom.has_z = has_z;
    
    uint32_t num_points = readUInt32(ptr, is_little_endian);
    geom.coords.reserve(num_points);
    
    for (uint32_t i = 0; i < num_points; ++i) {
        double x = readDouble(ptr, is_little_endian);
        double y = readDouble(ptr, is_little_endian);
        double z = has_z ? readDouble(ptr, is_little_endian) : 0.0;
        
        geom.coords.emplace_back(x, y, has_z ? std::optional<double>(z) : std::nullopt);
    }
    
    return geom;
}

// Parse Polygon
GeometryInfo EWKBParser::parsePolygon(const uint8_t*& ptr, bool has_z, bool is_little_endian) {
    GeometryInfo geom(has_z ? GeometryType::PolygonZ : GeometryType::Polygon);
    geom.has_z = has_z;
    
    uint32_t num_rings = readUInt32(ptr, is_little_endian);
    geom.rings.resize(num_rings);
    
    for (uint32_t r = 0; r < num_rings; ++r) {
        uint32_t num_points = readUInt32(ptr, is_little_endian);
        geom.rings[r].reserve(num_points);
        
        for (uint32_t i = 0; i < num_points; ++i) {
            double x = readDouble(ptr, is_little_endian);
            double y = readDouble(ptr, is_little_endian);
            double z = has_z ? readDouble(ptr, is_little_endian) : 0.0;
            
            geom.rings[r].emplace_back(x, y, has_z ? std::optional<double>(z) : std::nullopt);
        }
    }
    
    return geom;
}

// Parse EWKB
GeometryInfo EWKBParser::parse(const std::vector<uint8_t>& ewkb) {
    if (ewkb.size() < 5) {
        throw std::runtime_error("EWKB: Invalid size (< 5 bytes)");
    }
    
    const uint8_t* ptr = ewkb.data();
    
    // Byte order: 0 = Big Endian, 1 = Little Endian
    bool is_little_endian = (*ptr == 0x01);
    ptr++;
    
    // Geometry type (may include SRID and Z flags)
    uint32_t type_code = readUInt32(ptr, is_little_endian);
    
    bool has_srid = (type_code & 0x20000000) != 0;
    bool has_z = (type_code & 0x80000000) != 0;
    bool has_m = (type_code & 0x40000000) != 0;
    
    uint32_t base_type = type_code & 0x000000FF;
    
    GeometryInfo geom;
    geom.has_z = has_z;
    geom.has_m = has_m;
    
    // Read SRID if present
    if (has_srid) {
        geom.srid = readUInt32(ptr, is_little_endian);
    }
    
    // Parse geometry based on type
    switch (base_type) {
        case 1:  // Point
            geom = parsePoint(ptr, has_z, is_little_endian);
            break;
        case 2:  // LineString
            geom = parseLineString(ptr, has_z, is_little_endian);
            break;
        case 3:  // Polygon
            geom = parsePolygon(ptr, has_z, is_little_endian);
            break;
        default:
            throw std::runtime_error("EWKB: Unsupported geometry type: " + std::to_string(base_type));
    }
    
    geom.srid = has_srid ? geom.srid : 4326;
    return geom;
}

// Serialize EWKB
std::vector<uint8_t> EWKBParser::serialize(const GeometryInfo& geom) {
    std::vector<uint8_t> buf;
    bool is_little_endian = true;
    
    // Byte order
    buf.push_back(is_little_endian ? 0x01 : 0x00);
    
    // Geometry type
    uint32_t type_code = static_cast<uint32_t>(geom.type);
    if (geom.has_z) type_code |= 0x80000000;
    writeUInt32(buf, type_code, is_little_endian);
    
    // Serialize coordinates based on type
    if (geom.isPoint()) {
        const auto& c = geom.coords[0];
        writeDouble(buf, c.x, is_little_endian);
        writeDouble(buf, c.y, is_little_endian);
        if (geom.has_z) writeDouble(buf, c.getZ(), is_little_endian);
    } else if (geom.isLineString()) {
        writeUInt32(buf, geom.coords.size(), is_little_endian);
        for (const auto& c : geom.coords) {
            writeDouble(buf, c.x, is_little_endian);
            writeDouble(buf, c.y, is_little_endian);
            if (geom.has_z) writeDouble(buf, c.getZ(), is_little_endian);
        }
    } else if (geom.isPolygon()) {
        writeUInt32(buf, geom.rings.size(), is_little_endian);
        for (const auto& ring : geom.rings) {
            writeUInt32(buf, ring.size(), is_little_endian);
            for (const auto& c : ring) {
                writeDouble(buf, c.x, is_little_endian);
                writeDouble(buf, c.y, is_little_endian);
                if (geom.has_z) writeDouble(buf, c.getZ(), is_little_endian);
            }
        }
    }
    
    return buf;
}

// Parse GeoJSON
GeometryInfo EWKBParser::parseGeoJSON(const std::string& geojson_str) {
    auto j = json::parse(geojson_str);
    
    std::string type = j["type"];
    GeometryInfo geom;
    
    if (type == "Point") {
        geom.type = GeometryType::Point;
        auto coords = j["coordinates"];
        double x = coords[0];
        double y = coords[1];
        if (coords.size() > 2) {
            geom.coords.emplace_back(x, y, coords[2].get<double>());
            geom.has_z = true;
            geom.type = GeometryType::PointZ;
        } else {
            geom.coords.emplace_back(x, y);
        }
    }
    // ... More GeoJSON types can be added here
    
    return geom;
}

// To GeoJSON
std::string EWKBParser::toGeoJSON(const GeometryInfo& geom) {
    json j;
    
    if (geom.isPoint()) {
        j["type"] = "Point";
        const auto& c = geom.coords[0];
        if (geom.has_z) {
            j["coordinates"] = {c.x, c.y, c.getZ()};
        } else {
            j["coordinates"] = {c.x, c.y};
        }
    }
    // ... More geometry types
    
    return j.dump();
}

// Compute sidecar
GeoSidecar EWKBParser::computeSidecar(const GeometryInfo& geom) {
    GeoSidecar sidecar;
    sidecar.mbr = geom.computeMBR();
    sidecar.centroid = geom.computeCentroid();
    
    if (sidecar.mbr.hasZ()) {
        sidecar.z_min = sidecar.mbr.z_min.value_or(0.0);
        sidecar.z_max = sidecar.mbr.z_max.value_or(0.0);
    }
    
    return sidecar;
}

// Validate EWKB
bool EWKBParser::validate(const std::vector<uint8_t>& ewkb) {
    try {
        parse(ewkb);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace geo
}  // namespace themis
