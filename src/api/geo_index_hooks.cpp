#include "api/geo_index_hooks.h"
#include "index/spatial_index.h"
#include "utils/geo/ewkb.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>

namespace themis {
namespace api {

using json = nlohmann::json;

void GeoIndexHooks::onEntityPut(
    RocksDBWrapper& db,
    index::SpatialIndexManager* spatial_mgr,
    const std::string& table,
    const std::string& pk,
    const std::vector<uint8_t>& blob
) {
    // Skip if spatial index not available or table doesn't have spatial index
    if (!spatial_mgr || !spatial_mgr->hasSpatialIndex(table)) {
        return;
    }

    try {
        // Parse blob as JSON to extract geometry field
        std::string blob_str(reinterpret_cast<const char*>(blob.data()), blob.size());
        auto j = json::parse(blob_str);

        // Look for geometry field (common names: geometry, geom, location)
        std::vector<uint8_t> geom_blob;
        bool found_geometry = false;

        if (j.contains("geometry") && j["geometry"].is_string()) {
            // Geometry as hex-encoded EWKB string
            std::string hex_ewkb = j["geometry"].get<std::string>();
            // Simple hex decode (assumes valid hex)
            geom_blob.reserve(hex_ewkb.size() / 2);
            for (size_t i = 0; i < hex_ewkb.size(); i += 2) {
                uint8_t byte = static_cast<uint8_t>(
                    std::stoi(hex_ewkb.substr(i, 2), nullptr, 16)
                );
                geom_blob.push_back(byte);
            }
            found_geometry = true;
        } else if (j.contains("geometry") && j["geometry"].is_object()) {
            // Geometry as GeoJSON
            std::string geojson = j["geometry"].dump();
            auto geom_info = geo::EWKBParser::parseGeoJSON(geojson);
            geom_blob = geo::EWKBParser::serialize(geom_info);
            found_geometry = true;
        } else if (j.contains("geom_blob") && j["geom_blob"].is_array()) {
            // Geometry as binary array
            for (auto& byte : j["geom_blob"]) {
                geom_blob.push_back(static_cast<uint8_t>(byte.get<int>()));
            }
            found_geometry = true;
        }

        if (!found_geometry) {
            // No geometry field, skip silently
            return;
        }

        // Parse EWKB and compute sidecar
        auto geom_info = geo::EWKBParser::parse(geom_blob);
        auto sidecar = geo::EWKBParser::computeSidecar(geom_info);

        // Insert into spatial index
        auto status = spatial_mgr->insert(table, pk, sidecar);
        if (!status) {
            THEMIS_WARN("Spatial index insert failed for {}:{}: {}", 
                        table, pk, status.message);
        } else {
            THEMIS_DEBUG("Spatial index updated for {}:{}", table, pk);
        }

    } catch (const json::exception& e) {
        // JSON parse error - log but don't fail the entity write
        THEMIS_WARN("Geo hook JSON parse error for {}:{}: {}", table, pk, e.what());
    } catch (const std::exception& e) {
        // EWKB parse or other error - log but don't fail the entity write
        THEMIS_WARN("Geo hook error for {}:{}: {}", table, pk, e.what());
    }
}

// Phase 2: Atomic entity PUT with spatial index update via WriteBatch
bool GeoIndexHooks::onEntityPutAtomic(
    RocksDBWrapper::WriteBatchWrapper& batch,
    index::SpatialIndexManager* spatial_mgr,
    const std::string& table,
    const std::string& pk,
    const std::vector<uint8_t>& blob
) {
    // Skip if spatial index not available or table doesn't have spatial index
    if (!spatial_mgr || !spatial_mgr->hasSpatialIndex(table)) {
        return false;
    }

    try {
        // Parse blob as JSON to extract geometry field
        std::string blob_str(reinterpret_cast<const char*>(blob.data()), blob.size());
        auto j = json::parse(blob_str);

        // Look for geometry field
        std::vector<uint8_t> geom_blob;
        bool found_geometry = false;

        if (j.contains("geometry") && j["geometry"].is_string()) {
            std::string hex_ewkb = j["geometry"].get<std::string>();
            geom_blob.reserve(hex_ewkb.size() / 2);
            for (size_t i = 0; i < hex_ewkb.size(); i += 2) {
                uint8_t byte = static_cast<uint8_t>(
                    std::stoi(hex_ewkb.substr(i, 2), nullptr, 16)
                );
                geom_blob.push_back(byte);
            }
            found_geometry = true;
        } else if (j.contains("geometry") && j["geometry"].is_object()) {
            std::string geojson = j["geometry"].dump();
            auto geom_info = geo::EWKBParser::parseGeoJSON(geojson);
            geom_blob = geo::EWKBParser::serialize(geom_info);
            found_geometry = true;
        } else if (j.contains("geom_blob") && j["geom_blob"].is_array()) {
            for (auto& byte : j["geom_blob"]) {
                geom_blob.push_back(static_cast<uint8_t>(byte.get<int>()));
            }
            found_geometry = true;
        }

        if (!found_geometry) {
            return false;
        }

        // Parse EWKB and compute sidecar
        auto geom_info = geo::EWKBParser::parse(geom_blob);
        auto sidecar = geo::EWKBParser::computeSidecar(geom_info);

        // TODO: Add sidecar writes to WriteBatch
        // This requires exposing WriteBatch::put() and computing the spatial index keys
        // For now, we'll use the non-atomic insert as fallback
        // 
        // Future implementation:
        // 1. Compute Morton code from sidecar.centroid
        // 2. Load existing bucket entries from DB
        // 3. Add new entry to bucket list
        // 4. batch.put(bucket_key, serialized_bucket)
        // 5. batch.put(per_pk_key, serialized_sidecar)
        
        THEMIS_DEBUG("Atomic spatial index update for {}:{} (not yet implemented, using fallback)",
                    table, pk);
        
        // Fallback to non-atomic insert for now
        // NOTE: This is a temporary solution until WriteBatch integration is complete
        return false;

    } catch (const json::exception& e) {
        THEMIS_WARN("Geo hook atomic JSON parse error for {}:{}: {}", table, pk, e.what());
        return false;
    } catch (const std::exception& e) {
        THEMIS_WARN("Geo hook atomic error for {}:{}: {}", table, pk, e.what());
        return false;
    }
}

void GeoIndexHooks::onEntityDelete(
    RocksDBWrapper& db,
    index::SpatialIndexManager* spatial_mgr,
    const std::string& table,
    const std::string& pk,
    const std::vector<uint8_t>& old_blob
) {
    // Skip if spatial index not available
    if (!spatial_mgr || !spatial_mgr->hasSpatialIndex(table)) {
        return;
    }

    try {
        // Parse old blob to get geometry sidecar for removal
        std::string blob_str(reinterpret_cast<const char*>(old_blob.data()), 
                           old_blob.size());
        auto j = json::parse(blob_str);

        // Extract geometry blob
        std::vector<uint8_t> geom_blob;
        bool found_geometry = false;

        if (j.contains("geometry") && j["geometry"].is_string()) {
            std::string hex_ewkb = j["geometry"].get<std::string>();
            geom_blob.reserve(hex_ewkb.size() / 2);
            for (size_t i = 0; i < hex_ewkb.size(); i += 2) {
                uint8_t byte = static_cast<uint8_t>(
                    std::stoi(hex_ewkb.substr(i, 2), nullptr, 16)
                );
                geom_blob.push_back(byte);
            }
            found_geometry = true;
        } else if (j.contains("geometry") && j["geometry"].is_object()) {
            std::string geojson = j["geometry"].dump();
            auto geom_info = geo::EWKBParser::parseGeoJSON(geojson);
            geom_blob = geo::EWKBParser::serialize(geom_info);
            found_geometry = true;
        } else if (j.contains("geom_blob") && j["geom_blob"].is_array()) {
            for (auto& byte : j["geom_blob"]) {
                geom_blob.push_back(static_cast<uint8_t>(byte.get<int>()));
            }
            found_geometry = true;
        }

        if (!found_geometry) {
            return;
        }

        // Parse and compute sidecar
        auto geom_info = geo::EWKBParser::parse(geom_blob);
        auto sidecar = geo::EWKBParser::computeSidecar(geom_info);

        // Remove from spatial index
        auto status = spatial_mgr->remove(table, pk, sidecar);
        if (!status) {
            THEMIS_WARN("Spatial index remove failed for {}:{}: {}", 
                        table, pk, status.message);
        } else {
            THEMIS_DEBUG("Spatial index entry removed for {}:{}", table, pk);
        }

    } catch (const std::exception& e) {
        // Parse error - log but don't fail the delete
        THEMIS_WARN("Geo hook delete error for {}:{}: {}", table, pk, e.what());
    }
}

} // namespace api
} // namespace themis
