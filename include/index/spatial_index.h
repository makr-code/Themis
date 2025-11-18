#pragma once

#include "utils/geo/ewkb.h"
#include "storage/rocksdb_wrapper.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <cfloat>

namespace themis {
namespace index {

// Morton code encoder for Z-order spatial indexing
class MortonEncoder {
public:
    // Encode 2D coordinates to Morton code (Z-order curve)
    static uint64_t encode2D(double x, double y, const geo::MBR& bounds);
    
    // Encode 3D coordinates to Morton code
    static uint64_t encode3D(double x, double y, double z, const geo::MBR& bounds);
    
    // Decode Morton code to 2D coordinates
    static std::pair<double, double> decode2D(uint64_t code, const geo::MBR& bounds);
    
    // Get Morton code ranges for MBR query (multiple ranges for 2D query on 1D curve)
    static std::vector<std::pair<uint64_t, uint64_t>> getRanges(
        const geo::MBR& query_bbox,
        const geo::MBR& total_bounds,
        int max_ranges = 16
    );

private:
    // Interleave bits of two 32-bit integers
    static uint64_t interleaveBits2D(uint32_t x, uint32_t y);
    
    // Interleave bits of three 21-bit integers
    static uint64_t interleaveBits3D(uint32_t x, uint32_t y, uint32_t z);
    
    // Normalize coordinate to [0, 2^32-1] range
    static uint32_t normalizeCoord(double coord, double min_val, double max_val);
};

// R-Tree configuration
struct RTreeConfig {
    int max_entries_per_node = 16;    // M parameter
    int min_entries_per_node = 4;     // m parameter (typically M/4)
    bool use_3d = false;              // Enable Z-coordinate indexing
    geo::MBR total_bounds;            // Global bounds for normalization
};

// Spatial query result
struct SpatialResult {
    std::string primary_key;
    geo::MBR mbr;
    std::optional<double> z_min;
    std::optional<double> z_max;
    double distance = 0.0;  // For distance-based queries
};

// Spatial Index Manager (table-agnostic, works for all 5 models)
class SpatialIndexManager {
public:
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
        explicit operator bool() const { return ok; }
    };

    explicit SpatialIndexManager(RocksDBWrapper& db);
    ~SpatialIndexManager() = default;
    
    // ===== Index Management =====
    
    /// Create spatial index for ANY table (relational, graph, vector, content, time-series)
    Status createSpatialIndex(
        std::string_view table,
        std::string_view geometry_column = "geometry",
        const RTreeConfig& config = {}
    );
    
    /// Drop spatial index
    Status dropSpatialIndex(std::string_view table);
    
    /// Check if table has spatial index
    bool hasSpatialIndex(std::string_view table) const;
    
    /// Get index statistics
    struct IndexStats {
        size_t entry_count = 0;
        geo::MBR total_bounds;
        double avg_area = 0.0;
        size_t morton_buckets = 0;
    };
    IndexStats getStats(std::string_view table) const;
    
    // ===== Insert/Update/Delete =====
    
    /// Insert entity into spatial index
    Status insert(
        std::string_view table,
        std::string_view primary_key,
        const geo::GeoSidecar& sidecar
    );
    
    /// Update entity location
    Status update(
        std::string_view table,
        std::string_view primary_key,
        const geo::GeoSidecar& old_sidecar,
        const geo::GeoSidecar& new_sidecar
    );
    
    /// Remove entity from spatial index
    Status remove(
        std::string_view table,
        std::string_view primary_key,
        const geo::GeoSidecar& sidecar
    );
    
    // ===== Query Operations (Model-Agnostic) =====
    
    /// Find all entities intersecting with query MBR
    std::vector<SpatialResult> searchIntersects(
        std::string_view table,
        const geo::MBR& query_bbox
    ) const;
    
    /// Find all entities within query MBR
    std::vector<SpatialResult> searchWithin(
        std::string_view table,
        const geo::MBR& query_bbox,
        double z_min = -DBL_MAX,
        double z_max = DBL_MAX
    ) const;
    
    /// Find all entities containing a point
    std::vector<SpatialResult> searchContains(
        std::string_view table,
        double x,
        double y,
        std::optional<double> z = std::nullopt
    ) const;
    
    /// Find all entities within distance from point (2D Haversine or 3D Euclidean)
    std::vector<SpatialResult> searchNearby(
        std::string_view table,
        double x,
        double y,
        double max_distance_meters,
        std::optional<double> z = std::nullopt,
        size_t limit = 100
    ) const;
    
    /// K-Nearest Neighbors search (spatial only, no exact geometry check)
    std::vector<SpatialResult> searchKNN(
        std::string_view table,
        double x,
        double y,
        size_t k,
        std::optional<double> z = std::nullopt
    ) const;
    
    // ===== 3D Z-Range Queries =====
    
    /// Find entities within Z-range (elevation filtering)
    std::vector<SpatialResult> searchZRange(
        std::string_view table,
        double z_min,
        double z_max
    ) const;
    
    /// Combine spatial + Z-range filter
    std::vector<SpatialResult> searchIntersectsWithZ(
        std::string_view table,
        const geo::MBR& query_bbox,
        double z_min,
        double z_max
    ) const;
    
private:
    RocksDBWrapper& db_;
    
    // RocksDB key prefixes
    std::string getSpatialKeyPrefix(std::string_view table) const;
    std::string getZRangeKeyPrefix(std::string_view table) const;
    std::string getConfigKey(std::string_view table) const;
    
    // Key construction
    std::string makeSpatialKey(std::string_view table, uint64_t morton_code) const;
    std::string makeZRangeKey(std::string_view table, int z_bucket) const;
    
    // Get/Set config
    std::optional<RTreeConfig> getConfig(std::string_view table) const;
    Status saveConfig(std::string_view table, const RTreeConfig& config);
    
    // Haversine distance (lat/lon in degrees)
    double haversineDistance(double lat1, double lon1, double lat2, double lon2) const;
    
    // Euclidean 3D distance
    double euclidean3DDistance(double x1, double y1, double z1, double x2, double y2, double z2) const;
    
    // Z-bucket for elevation indexing (10m buckets)
    int getZBucket(double z) const;
    
    // Parse sidecar entry from RocksDB value
    struct SidecarEntry {
        std::string primary_key;
        geo::GeoSidecar sidecar;
    };
    std::vector<SidecarEntry> parseSidecarList(const std::string& value) const;
    std::string serializeSidecarList(const std::vector<SidecarEntry>& entries) const;
};

}  // namespace index
}  // namespace themis
