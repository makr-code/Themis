#include "index/spatial_index.h"
#include "utils/logger.h"
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace themis {
namespace index {

using json = nlohmann::json;

// Constants
constexpr double EARTH_RADIUS_METERS = 6371000.0;
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double Z_BUCKET_SIZE = 10.0;  // 10 meter buckets for elevation

// ===== Morton Encoder Implementation =====

// Interleave bits for 2D Morton code
uint64_t MortonEncoder::interleaveBits2D(uint32_t x, uint32_t y) {
    uint64_t result = 0;
    for (int i = 0; i < 32; ++i) {
        result |= ((x & (1ULL << i)) << i) | ((y & (1ULL << i)) << (i + 1));
    }
    return result;
}

// Interleave bits for 3D Morton code (21 bits each)
uint64_t MortonEncoder::interleaveBits3D(uint32_t x, uint32_t y, uint32_t z) {
    uint64_t result = 0;
    for (int i = 0; i < 21; ++i) {
        result |= ((x & (1ULL << i)) << (2 * i)) |
                  ((y & (1ULL << i)) << (2 * i + 1)) |
                  ((z & (1ULL << i)) << (2 * i + 2));
    }
    return result;
}

// Normalize coordinate to [0, 2^32-1]
uint32_t MortonEncoder::normalizeCoord(double coord, double min_val, double max_val) {
    if (max_val <= min_val) return 0;
    
    double normalized = (coord - min_val) / (max_val - min_val);
    normalized = std::clamp(normalized, 0.0, 1.0);
    
    return static_cast<uint32_t>(normalized * 0xFFFFFFFFULL);
}

// Encode 2D to Morton code
uint64_t MortonEncoder::encode2D(double x, double y, const geo::MBR& bounds) {
    uint32_t nx = normalizeCoord(x, bounds.minx, bounds.maxx);
    uint32_t ny = normalizeCoord(y, bounds.miny, bounds.maxy);
    return interleaveBits2D(nx, ny);
}

// Encode 3D to Morton code
uint64_t MortonEncoder::encode3D(double x, double y, double z, const geo::MBR& bounds) {
    // Use 21 bits per dimension for 3D
    uint32_t nx = normalizeCoord(x, bounds.minx, bounds.maxx) >> 11;
    uint32_t ny = normalizeCoord(y, bounds.miny, bounds.maxy) >> 11;
    
    double z_min = bounds.z_min.value_or(0.0);
    double z_max = bounds.z_max.value_or(1000.0);
    uint32_t nz = normalizeCoord(z, z_min, z_max) >> 11;
    
    return interleaveBits3D(nx, ny, nz);
}

// Decode 2D Morton code
std::pair<double, double> MortonEncoder::decode2D(uint64_t code, const geo::MBR& bounds) {
    uint32_t x = 0, y = 0;
    for (int i = 0; i < 32; ++i) {
        x |= ((code >> (2 * i)) & 1) << i;
        y |= ((code >> (2 * i + 1)) & 1) << i;
    }
    
    double dx = static_cast<double>(x) / 0xFFFFFFFFULL;
    double dy = static_cast<double>(y) / 0xFFFFFFFFULL;
    
    double real_x = bounds.minx + dx * (bounds.maxx - bounds.minx);
    double real_y = bounds.miny + dy * (bounds.maxy - bounds.miny);
    
    return {real_x, real_y};
}

// Get Morton ranges for MBR query (simplified: just return min/max range)
std::vector<std::pair<uint64_t, uint64_t>> MortonEncoder::getRanges(
    const geo::MBR& query_bbox,
    const geo::MBR& total_bounds,
    int max_ranges
) {
    // Simplified implementation: compute min/max Morton codes
    uint64_t min_code = encode2D(query_bbox.minx, query_bbox.miny, total_bounds);
    uint64_t max_code = encode2D(query_bbox.maxx, query_bbox.maxy, total_bounds);
    
    // For accurate query, we'd need to decompose into multiple ranges
    // For MVP, use single range (may include false positives)
    return {{min_code, max_code}};
}

// ===== SpatialIndexManager Implementation =====

SpatialIndexManager::SpatialIndexManager(std::shared_ptr<StorageEngine> storage)
    : storage_(storage) {}

// Key prefixes
std::string SpatialIndexManager::getSpatialKeyPrefix(std::string_view table) const {
    return std::string("spatial:") + std::string(table) + ":";
}

std::string SpatialIndexManager::getZRangeKeyPrefix(std::string_view table) const {
    return std::string("zrange:") + std::string(table) + ":";
}

std::string SpatialIndexManager::getConfigKey(std::string_view table) const {
    return std::string("spatial_config:") + std::string(table);
}

std::string SpatialIndexManager::makeSpatialKey(std::string_view table, uint64_t morton_code) const {
    char buf[32];
    snprintf(buf, sizeof(buf), "%016lx", morton_code);
    return getSpatialKeyPrefix(table) + buf;
}

std::string SpatialIndexManager::makeZRangeKey(std::string_view table, int z_bucket) const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%08d", z_bucket);
    return getZRangeKeyPrefix(table) + buf;
}

// Config persistence
std::optional<RTreeConfig> SpatialIndexManager::getConfig(std::string_view table) const {
    auto value = storage_->get(getConfigKey(table));
    if (!value) return std::nullopt;
    
    try {
        auto j = json::parse(*value);
        RTreeConfig config;
        config.max_entries_per_node = j.value("max_entries", 16);
        config.use_3d = j.value("use_3d", false);
        
        if (j.contains("total_bounds")) {
            auto& b = j["total_bounds"];
            config.total_bounds.minx = b.value("minx", -180.0);
            config.total_bounds.miny = b.value("miny", -90.0);
            config.total_bounds.maxx = b.value("maxx", 180.0);
            config.total_bounds.maxy = b.value("maxy", 90.0);
        }
        
        return config;
    } catch (...) {
        return std::nullopt;
    }
}

Status SpatialIndexManager::saveConfig(std::string_view table, const RTreeConfig& config) {
    json j;
    j["max_entries"] = config.max_entries_per_node;
    j["use_3d"] = config.use_3d;
    j["total_bounds"] = {
        {"minx", config.total_bounds.minx},
        {"miny", config.total_bounds.miny},
        {"maxx", config.total_bounds.maxx},
        {"maxy", config.total_bounds.maxy}
    };
    
    return storage_->put(getConfigKey(table), j.dump());
}

// Create spatial index
Status SpatialIndexManager::createSpatialIndex(
    std::string_view table,
    std::string_view geometry_column,
    const RTreeConfig& config
) {
    // Save config
    RTreeConfig cfg = config;
    if (cfg.total_bounds.minx == 0.0 && cfg.total_bounds.maxx == 0.0) {
        // Default: global lat/lon bounds
        cfg.total_bounds = geo::MBR(-180.0, -90.0, 180.0, 90.0);
    }
    
    return saveConfig(table, cfg);
}

// Drop spatial index
Status SpatialIndexManager::dropSpatialIndex(std::string_view table) {
    // Delete config
    storage_->remove(getConfigKey(table));
    
    // Delete all spatial keys (prefix scan + delete)
    std::string prefix = getSpatialKeyPrefix(table);
    auto keys = storage_->scanKeys(prefix, prefix + "~");  // Scan range
    
    for (const auto& key : keys) {
        storage_->remove(key);
    }
    
    return Status::ok();
}

bool SpatialIndexManager::hasSpatialIndex(std::string_view table) const {
    return getConfig(table).has_value();
}

// Parse sidecar list
std::vector<SpatialIndexManager::SidecarEntry> SpatialIndexManager::parseSidecarList(
    const std::string& value
) const {
    std::vector<SidecarEntry> result;
    
    try {
        auto j = json::parse(value);
        for (const auto& item : j) {
            SidecarEntry entry;
            entry.primary_key = item["pk"];
            
            auto& mbr = item["mbr"];
            entry.sidecar.mbr.minx = mbr["minx"];
            entry.sidecar.mbr.miny = mbr["miny"];
            entry.sidecar.mbr.maxx = mbr["maxx"];
            entry.sidecar.mbr.maxy = mbr["maxy"];
            
            if (item.contains("z_min")) {
                entry.sidecar.z_min = item["z_min"];
                entry.sidecar.z_max = item["z_max"];
            }
            
            result.push_back(entry);
        }
    } catch (...) {
        // Parsing error
    }
    
    return result;
}

std::string SpatialIndexManager::serializeSidecarList(
    const std::vector<SidecarEntry>& entries
) const {
    json j = json::array();
    
    for (const auto& entry : entries) {
        json item;
        item["pk"] = entry.primary_key;
        item["mbr"] = {
            {"minx", entry.sidecar.mbr.minx},
            {"miny", entry.sidecar.mbr.miny},
            {"maxx", entry.sidecar.mbr.maxx},
            {"maxy", entry.sidecar.mbr.maxy}
        };
        
        if (entry.sidecar.z_min != 0.0 || entry.sidecar.z_max != 0.0) {
            item["z_min"] = entry.sidecar.z_min;
            item["z_max"] = entry.sidecar.z_max;
        }
        
        j.push_back(item);
    }
    
    return j.dump();
}

// Insert
Status SpatialIndexManager::insert(
    std::string_view table,
    std::string_view primary_key,
    const geo::GeoSidecar& sidecar
) {
    auto config = getConfig(table);
    if (!config) {
        return Status::error("Spatial index not found for table: " + std::string(table));
    }
    
    // Compute Morton code for centroid
    uint64_t morton = MortonEncoder::encode2D(
        sidecar.centroid.x,
        sidecar.centroid.y,
        config->total_bounds
    );
    
    std::string key = makeSpatialKey(table, morton);
    
    // Get existing entries for this Morton bucket
    auto value = storage_->get(key);
    std::vector<SidecarEntry> entries;
    
    if (value) {
        entries = parseSidecarList(*value);
    }
    
    // Add new entry
    SidecarEntry new_entry;
    new_entry.primary_key = std::string(primary_key);
    new_entry.sidecar = sidecar;
    entries.push_back(new_entry);
    
    // Save back
    return storage_->put(key, serializeSidecarList(entries));
}

// Remove
Status SpatialIndexManager::remove(
    std::string_view table,
    std::string_view primary_key,
    const geo::GeoSidecar& sidecar
) {
    auto config = getConfig(table);
    if (!config) return Status::ok();
    
    uint64_t morton = MortonEncoder::encode2D(
        sidecar.centroid.x,
        sidecar.centroid.y,
        config->total_bounds
    );
    
    std::string key = makeSpatialKey(table, morton);
    auto value = storage_->get(key);
    
    if (!value) return Status::ok();
    
    auto entries = parseSidecarList(*value);
    
    // Remove matching entry
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [&](const SidecarEntry& e) { return e.primary_key == primary_key; }),
        entries.end()
    );
    
    if (entries.empty()) {
        return storage_->remove(key);
    } else {
        return storage_->put(key, serializeSidecarList(entries));
    }
}

// Update
Status SpatialIndexManager::update(
    std::string_view table,
    std::string_view primary_key,
    const geo::GeoSidecar& old_sidecar,
    const geo::GeoSidecar& new_sidecar
) {
    auto status = remove(table, primary_key, old_sidecar);
    if (!status) return status;
    
    return insert(table, primary_key, new_sidecar);
}

// Haversine distance
double SpatialIndexManager::haversineDistance(double lat1, double lon1, double lat2, double lon2) const {
    double dlat = (lat2 - lat1) * DEG_TO_RAD;
    double dlon = (lon2 - lon1) * DEG_TO_RAD;
    
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * DEG_TO_RAD) * std::cos(lat2 * DEG_TO_RAD) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    
    return EARTH_RADIUS_METERS * c;
}

// Search intersects
std::vector<SpatialResult> SpatialIndexManager::searchIntersects(
    std::string_view table,
    const geo::MBR& query_bbox
) const {
    auto config = getConfig(table);
    if (!config) return {};
    
    // Get Morton ranges
    auto ranges = MortonEncoder::getRanges(query_bbox, config->total_bounds);
    
    std::vector<SpatialResult> results;
    
    for (const auto& [min_code, max_code] : ranges) {
        // Scan RocksDB range
        std::string start_key = makeSpatialKey(table, min_code);
        std::string end_key = makeSpatialKey(table, max_code);
        
        auto kvs = storage_->scanRange(start_key, end_key);
        
        for (const auto& [key, value] : kvs) {
            auto entries = parseSidecarList(value);
            
            for (const auto& entry : entries) {
                // Check MBR intersection
                if (entry.sidecar.mbr.intersects(query_bbox)) {
                    SpatialResult result;
                    result.primary_key = entry.primary_key;
                    result.mbr = entry.sidecar.mbr;
                    result.z_min = entry.sidecar.z_min != 0.0 
                        ? std::optional<double>(entry.sidecar.z_min) : std::nullopt;
                    result.z_max = entry.sidecar.z_max != 0.0 
                        ? std::optional<double>(entry.sidecar.z_max) : std::nullopt;
                    results.push_back(result);
                }
            }
        }
    }
    
    return results;
}

// Search within
std::vector<SpatialResult> SpatialIndexManager::searchWithin(
    std::string_view table,
    const geo::MBR& query_bbox,
    double z_min,
    double z_max
) const {
    auto candidates = searchIntersects(table, query_bbox);
    
    // Filter: entity MBR must be fully within query MBR
    std::vector<SpatialResult> results;
    
    for (const auto& cand : candidates) {
        bool within = (cand.mbr.minx >= query_bbox.minx &&
                       cand.mbr.maxx <= query_bbox.maxx &&
                       cand.mbr.miny >= query_bbox.miny &&
                       cand.mbr.maxy <= query_bbox.maxy);
        
        // Check Z-range if specified
        if (within && z_min > -DBL_MAX && cand.z_min.has_value()) {
            within = (cand.z_min.value() >= z_min && cand.z_max.value() <= z_max);
        }
        
        if (within) {
            results.push_back(cand);
        }
    }
    
    return results;
}

// Search contains point
std::vector<SpatialResult> SpatialIndexManager::searchContains(
    std::string_view table,
    double x,
    double y,
    std::optional<double> z
) const {
    // Create small query box around point
    geo::MBR point_bbox(x - 0.0001, y - 0.0001, x + 0.0001, y + 0.0001);
    
    auto candidates = searchIntersects(table, point_bbox);
    
    // Filter: MBR must contain point
    std::vector<SpatialResult> results;
    
    for (const auto& cand : candidates) {
        if (cand.mbr.contains(x, y)) {
            results.push_back(cand);
        }
    }
    
    return results;
}

// Search nearby (distance-based)
std::vector<SpatialResult> SpatialIndexManager::searchNearby(
    std::string_view table,
    double x,
    double y,
    double max_distance_meters,
    std::optional<double> z,
    size_t limit
) const {
    // Expand to bbox (approximate)
    double degrees_delta = max_distance_meters / 111320.0;  // Rough approximation
    geo::MBR query_bbox(x - degrees_delta, y - degrees_delta, x + degrees_delta, y + degrees_delta);
    
    auto candidates = searchIntersects(table, query_bbox);
    
    // Compute exact distances
    std::vector<SpatialResult> results;
    
    for (auto& cand : candidates) {
        double dist = haversineDistance(y, x, cand.mbr.center().y, cand.mbr.center().x);
        
        if (dist <= max_distance_meters) {
            cand.distance = dist;
            results.push_back(cand);
        }
    }
    
    // Sort by distance
    std::sort(results.begin(), results.end(),
        [](const SpatialResult& a, const SpatialResult& b) {
            return a.distance < b.distance;
        });
    
    // Limit results
    if (results.size() > limit) {
        results.resize(limit);
    }
    
    return results;
}

// Get index stats
SpatialIndexManager::IndexStats SpatialIndexManager::getStats(std::string_view table) const {
    IndexStats stats;
    
    auto config = getConfig(table);
    if (!config) return stats;
    
    stats.total_bounds = config->total_bounds;
    
    // Scan all spatial keys
    std::string prefix = getSpatialKeyPrefix(table);
    auto kvs = storage_->scanRange(prefix, prefix + "~");
    
    stats.morton_buckets = kvs.size();
    
    double total_area = 0.0;
    for (const auto& [key, value] : kvs) {
        auto entries = parseSidecarList(value);
        stats.entry_count += entries.size();
        
        for (const auto& entry : entries) {
            total_area += entry.sidecar.mbr.area();
        }
    }
    
    if (stats.entry_count > 0) {
        stats.avg_area = total_area / stats.entry_count;
    }
    
    return stats;
}

}  // namespace index
}  // namespace themis
