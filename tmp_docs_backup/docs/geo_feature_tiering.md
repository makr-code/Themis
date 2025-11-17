# Geo Feature Tiering & Plugin-Architektur

Ziel: ThemisDB Geo so gestalten, dass erweiterte Funktionen optional als "Enterprise Capabilities" zugeschaltet werden können, ohne den Core (OSS) zu belasten. Build- und Runtime-Gating, klare Schnittstellen, Capability-Discovery.

## Grundprinzipien
- Clean Core (OSS): Minimaler, portabler Funktionsumfang (MVP), permissive Dependencies.
- Add-In (Enterprise): Leistungs- und Komfortfeatures als separate Module (DLL/.so), dynamisch ladbar.
- Kein Vendor-Lock: Öffentliche Plugin-ABI, klare Capability-API, CPU-Fallbacks jederzeit.

## Tiering (Vorschlag)

Core (OSS)
- Storage & Model: EWKB(Z) Parser/Serializer; Sidecar (mbr, centroid, z_min/z_max)
- Indizes: R-Tree (2D), Z-Range-Index
- AQL: ST_Point, ST_GeomFrom*, ST_AsGeoJSON/Text, ST_Envelope, ST_Distance, ST_DWithin, ST_Intersects, ST_Within, ST_Contains, 3D-Basis (HasZ/Z/Force2D/3D/ZBetween)
- Engine: CPU-Exact (Boost.Geometry), DNF/OR Orchestrierung, Fallback Full-Scan
- Observability: Basis-Metriken (index_hits, candidate_count, exact_checks, z_pruned)

Enterprise (Add-Ins)
- Compute/Performance
  - GPU-Backend (DX12/Vulkan) für Batch-Intersects/DWithin
  - SIMD-Kerne gebündelt/optimiert (Highway/xsimd) + Morton-Prepass
  - Prepared Geometries (GEOS Plugin, dyn. link) für schnelle Masken
  - LBVH/BVH Acceleration (CPU/GPU)
  - Roaring Bitmaps (CRoaring) für schnelle Set-Algebra
- Geofunktionen
  - Erweiterte Topologie: Buffer, Union, Intersection, Difference, SymDiff, Simplify, MakeValid
  - CRS-Transformationen (PROJ) + Caching
  - KNN Geo (geodätisch, Index-gestützt) mit Early-Out/Heuristiken
  - H3/S2 Integration (polyfill, coverage, analytics)
- Operative Features
  - Erweiterte Metriken/Tracing (gpu_batches, simd_hits, bvh_pruned, roaring_ops)
  - Admin-APIs: Rebuild/Compaction-Strategien, Warmup/Prepare

Hinweis: Lizenzen sind permissiv; "Enterprise" ist Produkt-Tiering, kein Zwang aus Lizenzsicht. GEOS (LGPL) bleibt optional/dynamisch.

## Gating-Mechanismen

Build-Time (CMake Options)
- THEMIS_GEO=ON                               # Basis Geo-Core
- THEMIS_GEO_SIMD=ON/OFF                      # SIMD-Kerne (optional)
- THEMIS_GEO_GPU=ON/OFF                       # GPU-Backend (Add-In)
- THEMIS_GEO_H3=ON/OFF                        # H3 Integration (Add-In)
- THEMIS_GEO_GEOS_PLUGIN=ON/OFF               # GEOS Prepared (dyn)
- THEMIS_ENTERPRISE=ON/OFF                    # Bündelt Add-Ins, Packaging

Runtime (config.json)
```json
{
  "geo": {
    "use_gpu": false,
    "use_simd": true,
    "plugins": ["geos", "h3"],
    "enterprise": false
  }
}
```

Plugin-Discovery
- Pfad: `modules/geo/*.dll|so`
- ABI: `ISpatialComputeBackend v1`, `IGeoOpExtension v1`
- Registrierung: Factory-Funktion `RegisterGeoPlugin(IGeoRegistry*)`
- Capability-Keys: `features`, `accel` (cpu/gpu/simd), `ops` (st_*), `version`, `license`

## API: Capabilities
- GET `/api/capabilities` → enthält `geo`-Block mit `features`, `enterprise` (bool), `plugins`.
- GET `/api/health` → Status von geladenen Plugins/Backends.

## Packaging
- Core-Binär: themis_core + Geo-Core
- Enterprise-Bundle: `themis_geo_gpu.(dll|so)`, `themis_geo_geos.(dll|so)`, `themis_geo_h3.(dll|so)` + Konfig-Beispiele
- Optionaler License-Hook: einfacher Token-Check (Datei/ENV) im Enterprise-Ladepfad (später)

## Migrationspfad
- Alle Enterprise-Funktionen haben CPU-Fallback oder sind additive (nicht-blockierend).
- AQL-Syntax bleibt gleich; Capability-API zeigt verfügbare Operatoren/Backends an.
- Tests: Core-Tests laufen ohne Enterprise; Enterprise-Tests optional/markiert.

## Nächste Schritte
- CMake-Optionen definieren; Targets für Plugins (shared libs).
- Capability-API `GET /api/capabilities` (HTTP/Server) implementieren.
- Plugin-Interfaces entwerfen (`include/geo/spatial_backend.h`, `include/geo/geo_ops_ext.h`).
- Minimaler GPU-Stub-Backend + GEOS-Stub-Plugin zum Laden/Listen.
