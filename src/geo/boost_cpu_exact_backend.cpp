#include "geo/spatial_backend.h"

#ifdef THEMIS_GEO_BOOST_BACKEND
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/linestring.hpp>
#endif

#include "utils/geo/ewkb.h"
#include "storage/rocksdb_wrapper.h"
#include "utils/logger.h"
#include <vector>
#include <string>

namespace themis { namespace geo {

#ifdef THEMIS_GEO_BOOST_BACKEND

namespace bg = boost::geometry;
using Point = bg::model::d2::point_xy<double>;
using Polygon = bg::model::polygon<Point>;
using LineString = bg::model::linestring<Point>;

/// Convert GeometryInfo to Boost.Geometry polygon
static Polygon toBoostPolygon(const GeometryInfo& geom) {
    Polygon poly;
    
    if (geom.rings.empty() && !geom.coords.empty()) {
        // Simple polygon from coords
        for (const auto& coord : geom.coords) {
            bg::append(poly.outer(), Point(coord.x, coord.y));
        }
    } else if (!geom.rings.empty()) {
        // Polygon with rings (first ring is outer, rest are holes)
        for (size_t i = 0; i < geom.rings.size(); ++i) {
            if (i == 0) {
                // Outer ring
                for (const auto& coord : geom.rings[i]) {
                    bg::append(poly.outer(), Point(coord.x, coord.y));
                }
            } else {
                // Inner ring (hole)
                Polygon::ring_type hole;
                for (const auto& coord : geom.rings[i]) {
                    bg::append(hole, Point(coord.x, coord.y));
                }
                poly.inners().push_back(hole);
            }
        }
    }
    
    return poly;
}

/// CPU exact backend using Boost.Geometry
class BoostCpuExactBackend final : public ISpatialComputeBackend {
public:
    const char* name() const noexcept override { 
        return "boost_cpu_exact"; 
    }
    
    bool isAvailable() const noexcept override { 
        return true; 
    }
    
    SpatialBatchResults batchIntersects(const SpatialBatchInputs& in) override {
        SpatialBatchResults out;
        out.mask.assign(in.count, 0u);
        
        // Note: This is a stub implementation showing the structure.
        // In a full implementation, the input would contain:
        // - Query geometry (parsed)
        // - Candidate PKs and their blobs
        // - Database handle to load geometries
        //
        // For now, we return empty results as the integration with
        // SpatialIndexManager::searchIntersects needs to be completed.
        
        return out;
    }
    
    /// Exact intersects check between two geometries
    /// This is the core exact check function called by the query engine
    bool exactIntersects(const GeometryInfo& geom1, const GeometryInfo& geom2) override {
        try {
            // Handle different geometry types
            if (geom1.isPolygon() && geom2.isPolygon()) {
                auto poly1 = toBoostPolygon(geom1);
                auto poly2 = toBoostPolygon(geom2);
                return bg::intersects(poly1, poly2);
            } else if (geom1.isPoint() && geom2.isPolygon()) {
                if (geom1.coords.empty()) return false;
                Point pt(geom1.coords[0].x, geom1.coords[0].y);
                auto poly = toBoostPolygon(geom2);
                return bg::within(pt, poly) || bg::touches(pt, poly);
            } else if (geom1.isPolygon() && geom2.isPoint()) {
                if (geom2.coords.empty()) return false;
                Point pt(geom2.coords[0].x, geom2.coords[0].y);
                auto poly = toBoostPolygon(geom1);
                return bg::within(pt, poly) || bg::touches(pt, poly);
            } else if (geom1.isPoint() && geom2.isPoint()) {
                if (geom1.coords.empty() || geom2.coords.empty()) return false;
                Point pt1(geom1.coords[0].x, geom1.coords[0].y);
                Point pt2(geom2.coords[0].x, geom2.coords[0].y);
                return bg::equals(pt1, pt2);
            }
            
            // Fallback: use MBR intersection for unsupported types
            auto mbr1 = geom1.computeMBR();
            auto mbr2 = geom2.computeMBR();
            return mbr1.intersects(mbr2);
            
        } catch (const std::exception& e) {
            THEMIS_WARN("Boost.Geometry exact check failed: {}", e.what());
            // Fallback to MBR on error
            auto mbr1 = geom1.computeMBR();
            auto mbr2 = geom2.computeMBR();
            return mbr1.intersects(mbr2);
        }
    }
};

// Global registry for backends (simple static storage for MVP)
static std::unique_ptr<ISpatialComputeBackend> g_boost_backend;

static void register_boost_backend() {
    g_boost_backend = std::make_unique<BoostCpuExactBackend>();
}

// Auto-register on module load
static int s_boost_backend_anchor = (register_boost_backend(), 0);

// Public API to get the backend
ISpatialComputeBackend* getBoostCpuBackend() {
    return g_boost_backend.get();
}

#else // !THEMIS_GEO_BOOST_BACKEND

// Fallback when Boost.Geometry is not available
ISpatialComputeBackend* getBoostCpuBackend() {
    return nullptr;
}

#endif // THEMIS_GEO_BOOST_BACKEND

} } // namespace themis::geo
