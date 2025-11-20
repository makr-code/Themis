#pragma once

#include "storage/rocksdb_wrapper.h"
#include <string>
#include <vector>
#include <memory>

namespace themis {
namespace index {
class SpatialIndexManager;
}

namespace api {

/// Geo indexing hooks for entity write/delete operations
/// These hooks integrate spatial index updates into the entity lifecycle.
/// 
/// NOTE: These hooks are best-effort and non-transactional in MVP.
/// Future versions should integrate into the same RocksDB transaction or saga.
/// Parse errors and index failures are logged but do not abort the entity write.
class GeoIndexHooks {
public:
    /// Hook called after successful entity PUT/UPDATE
    /// Parses geometry from blob, computes sidecar, and inserts into spatial index
    /// @param db RocksDB storage wrapper
    /// @param spatial_mgr Spatial index manager (can be null if geo disabled)
    /// @param table Table name
    /// @param pk Primary key
    /// @param blob Entity blob (JSON or binary)
    static void onEntityPut(
        RocksDBWrapper& db,
        index::SpatialIndexManager* spatial_mgr,
        const std::string& table,
        const std::string& pk,
        const std::vector<uint8_t>& blob
    );

    /// Hook called before entity DELETE
    /// Removes entry from spatial index if it exists
    /// @param db RocksDB storage wrapper
    /// @param spatial_mgr Spatial index manager (can be null if geo disabled)
    /// @param table Table name
    /// @param pk Primary key
    /// @param old_blob Previous entity blob (for computing sidecar)
    static void onEntityDelete(
        RocksDBWrapper& db,
        index::SpatialIndexManager* spatial_mgr,
        const std::string& table,
        const std::string& pk,
        const std::vector<uint8_t>& old_blob
    );
};

} // namespace api
} // namespace themis
