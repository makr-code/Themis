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
/// TRANSACTION SUPPORT (Phase 2):
/// - onEntityPut/onEntityDelete: Best-effort, non-atomic (called after entity write)
/// - onEntityPutAtomic: Atomic via RocksDB WriteBatch (requires integration in caller)
/// 
/// Future: Integrate atomic hooks into SecondaryIndexManager::put() for full atomicity
class GeoIndexHooks {
public:
    /// Hook called after successful entity PUT/UPDATE (non-atomic)
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

    /// Atomic entity PUT with spatial index update (Phase 2)
    /// Uses WriteBatch to ensure entity write and spatial index update are atomic
    /// @param batch RocksDB WriteBatch for atomic operations
    /// @param spatial_mgr Spatial index manager
    /// @param table Table name
    /// @param pk Primary key
    /// @param blob Entity blob
    /// @return true if sidecar was computed and added to batch, false otherwise
    static bool onEntityPutAtomic(
        RocksDBWrapper::WriteBatchWrapper& batch,
        index::SpatialIndexManager* spatial_mgr,
        const std::string& table,
        const std::string& pk,
        const std::vector<uint8_t>& blob
    );

    /// Hook called before entity DELETE (non-atomic)
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
