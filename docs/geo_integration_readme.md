# Geo MVP Integration Guide

## Overview

This document describes the geo MVP implementation that connects blob ingestion with spatial indexing and provides CPU-based exact geometry checks.

## Architecture

The geo MVP consists of three main components:

1. **Geo Index Hooks** (`src/api/geo_index_hooks.cpp`)
   - Integrates spatial index updates into entity lifecycle (PUT/DELETE)
   - Parses geometry from entity blobs (GeoJSON or EWKB)
   - Computes sidecar metadata (MBR, centroid, z-range)
   - Updates spatial index via `SpatialIndexManager`

2. **Boost.Geometry CPU Backend** (`src/geo/boost_cpu_exact_backend.cpp`)
   - Provides exact geometry intersection checks
   - Uses Boost.Geometry library for computational geometry
   - Supports Point, LineString, and Polygon types
   - Falls back to MBR checks for unsupported types

3. **Per-PK Storage Optimization** (`src/index/spatial_index.cpp`)
   - Stores sidecar per primary key in addition to bucket JSON
   - Allows updating/deleting individual entities without rewriting entire Morton bucket
   - Backward compatible with existing bucket-based storage

## Entity Write Integration

### HTTP API Handlers

The geo hooks are integrated into the HTTP API entity handlers:

- **PUT /entities/:key** - After successful entity write, calls `GeoIndexHooks::onEntityPut()`
- **DELETE /entities/:key** - Before entity deletion, calls `GeoIndexHooks::onEntityDelete()`

### Supported Geometry Formats

Entity blobs can contain geometry in several formats:

1. **GeoJSON** (recommended):
```json
{
  "id": "entity1",
  "geometry": {
    "type": "Point",
    "coordinates": [10.5, 50.5]
  }
}
```

2. **Hex-encoded EWKB**:
```json
{
  "id": "entity1",
  "geometry": "0101000000000000000000244000000000008049400"
}
```

3. **Binary EWKB array**:
```json
{
  "id": "entity1",
  "geom_blob": [1, 1, 0, 0, 0, ...]
}
```

## Limitations and Caveats

### Transaction Atomicity

**IMPORTANT**: In the MVP implementation, spatial index updates are **not atomic** with entity writes.

- Entity write and spatial index update happen in separate operations
- Parse/index errors do not abort the entity write (logged only)
- Future versions should integrate into RocksDB transactions or use saga pattern

### Error Handling

The hooks are designed to be robust:

- Geometry parse errors → logged as warnings, entity write succeeds
- Spatial index failures → logged as warnings, entity write succeeds
- Missing geometry field → silently skipped (not an error)
- Invalid JSON → logged, entity write succeeds

This ensures that geo functionality is **additive** and doesn't break existing functionality.

## Build Configuration

### Boost.Geometry Support

To enable the Boost.Geometry exact backend, ensure Boost is available:

```bash
# vcpkg.json already includes boost dependencies
# The backend is conditionally compiled with THEMIS_GEO_BOOST_BACKEND flag
```

Build with geo support:
```bash
cmake -DTHEMIS_GEO=ON -DTHEMIS_GEO_BOOST_BACKEND=ON ..
```

### Fallback Behavior

If Boost.Geometry is not available:
- The build will still succeed
- `getBoostCpuBackend()` returns `nullptr`
- Queries fall back to MBR-only filtering (no exact checks)

## Usage Example

### 1. Create Spatial Index

```bash
curl -X POST http://localhost:8080/api/spatial/index \
  -H "Content-Type: application/json" \
  -d '{
    "table": "places",
    "geometry_column": "geometry",
    "config": {
      "total_bounds": {"minx": -180, "miny": -90, "maxx": 180, "maxy": 90}
    }
  }'
```

### 2. Insert Entity with Geometry

```bash
curl -X PUT http://localhost:8080/api/entities/places:berlin \
  -H "Content-Type: application/json" \
  -d '{
    "key": "places:berlin",
    "blob": "{\"id\":\"berlin\",\"name\":\"Berlin\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[13.4,52.5]}}"
  }'
```

The spatial index is automatically updated.

### 3. Query Spatial Index

```bash
curl -X POST http://localhost:8080/api/spatial/search \
  -H "Content-Type: application/json" \
  -d '{
    "table": "places",
    "bbox": {"minx": 13.0, "miny": 52.0, "maxx": 14.0, "maxy": 53.0}
  }'
```

Returns entities whose MBR intersects the query bbox. With Boost backend enabled, exact geometry checks are performed.

## Testing

Run the integration tests:

```bash
cd build
ctest -R test_geo_index_integration -V
```

Tests verify:
- Entity PUT triggers spatial index insert
- searchIntersects returns correct results
- Entity DELETE removes from index
- Error handling (missing geometry, invalid JSON)
- Null spatial manager handling

## Future Improvements

1. **Transactional Integration**
   - Integrate hooks into RocksDB WriteBatch
   - Or use saga pattern for multi-step transactions
   - Ensure atomicity between entity write and index update

2. **Exact Geometry in Query Engine**
   - Wire Boost backend into `SpatialIndexManager::searchIntersects()`
   - Load entity blobs, parse geometries, call exact checks
   - Filter out MBR false positives

3. **Additional Backends**
   - SIMD-optimized CPU kernels for batch operations
   - GPU compute shaders for large-scale queries
   - GEOS prepared geometries plugin

4. **Storage Optimization**
   - Migrate fully to per-PK keys
   - Remove bucket JSON format (breaking change)
   - Compact binary sidecar format (not JSON)

## Security Considerations

- Geometry parsing uses exception handling to prevent crashes
- No user input is directly executed (only parsed as JSON/EWKB)
- Spatial index updates are logged for audit trails
- No SQL injection risk (key-value storage only)

## Performance Notes

- MBR computation: O(n) where n = number of coordinates
- Morton encoding: O(1)
- Bucket read/write: O(k) where k = entities per bucket
- Per-PK write: O(1) additional overhead per insert/delete
- Exact checks: Depends on geometry complexity (typically fast for simple polygons)

## References

- Geo Execution Plan: `docs/geo_execution_plan_over_blob.md`
- Feature Tiering: `docs/geo_feature_tiering.md`
- EWKB Spec: PostGIS Extended Well-Known Binary format
- Boost.Geometry: https://www.boost.org/doc/libs/release/libs/geometry/
