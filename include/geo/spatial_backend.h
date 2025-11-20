#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace themis {
namespace geo {

// Forward declarations
struct GeometryInfo;

// Minimal abstraction for compute backends (CPU/GPU) used by Geo exact checks
struct SpatialBatchInputs {
    // Placeholder for SoA/AoSoA layouts in the future
    // e.g., pointers/offsets to coordinates, MBR arrays, candidate id lists
    std::size_t count{0};
};

struct SpatialBatchResults {
    std::vector<uint8_t> mask; // 1 = hit, 0 = no hit
};

class ISpatialComputeBackend {
public:
    virtual ~ISpatialComputeBackend() = default;
    virtual const char* name() const noexcept = 0;
    virtual bool isAvailable() const noexcept = 0;

    // Example operation: batch Intersects exact-checks on prefiltered candidates
    virtual SpatialBatchResults batchIntersects(const SpatialBatchInputs& in) = 0;
    
    // Exact intersects check between two geometries (used by search path)
    // Returns true if geometries actually intersect, false otherwise
    virtual bool exactIntersects(const GeometryInfo& geom1, const GeometryInfo& geom2) = 0;
};

// Registry for dynamically loaded plugins
class IGeoRegistry {
public:
    virtual ~IGeoRegistry() = default;
    virtual void registerBackend(std::unique_ptr<ISpatialComputeBackend> backend) = 0;
};

// Plugin entry point signature a plugin must export if present
// extern "C" void RegisterGeoPlugin(IGeoRegistry* registry);
using RegisterGeoPluginFn = void(*)(IGeoRegistry*);

// Get the Boost CPU backend (if available)
ISpatialComputeBackend* getBoostCpuBackend();

} // namespace geo
} // namespace themis
