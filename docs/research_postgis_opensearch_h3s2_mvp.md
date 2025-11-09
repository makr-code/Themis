---
title: ARCHIVED – Geo Research Report (MVP)
status: archived
archived_at: 2025-11-09
replacement: (noch keine aktive Geo-Implementierung)
---

# Research-Report (Archiv): PostGIS vs. OpenSearch vs. H3/S2 – Leitentscheidungen (MVP)

Ziel: 1–2 Seiten Überblick für Index- und Storage-Entscheidungen in ThemisDB (Geo-MVP). Fokus: Portabilität (Windows/Linux), permissive Lizenzen, schnelle Kandidatenfilter + exakte Geo-Prüfung, einfache Integration in bestehende Engine/HTTP.

## Kurzprofil der Optionen

- PostGIS (Referenz-DB, SQL/OGC ST_*)
  - Storage: WKB/EWKB in `geometry`-Spalten, Z/M-Varianten, TOAST; GeoJSON als IO.
  - Indizes: GiST/SP-GiST als R-Tree auf MBR (2D), KNN-Unterstützung (<->), BRIN als leichtgewichtige Alternative.
  - Ausführung: Kandidaten via Index (MBR), exakte Tests über GEOS (Prepared Geometries für Speed), umfangreicher ST_* Katalog.
  - Stärken: sehr vollständige Funktionalität, reife Topologie, breite Community; klare Semantik (OGC).
  - Schwächen (für uns): Integration = externer SQL-Server; GEOS/PROJ-Stack (LGPL/MIT) – für In-Process-Core nur optional sinnvoll.

- OpenSearch (Lucene-basiert, verteilte Suche)
  - Datentypen: `geo_point` (BKD-Tree), `geo_shape` (BKD + prägeometrische Zerschnitte/Relationen).
  - Indizes/Ausführung: inverted/bkd-ähnliche Struktur, sehr gut für verteilte Filter, Tiles/Aggregationen; `within`/`intersects`/`distance` werden geometrisch approximiert und verifiziert.
  - Stärken: horizontale Skalierung, REST, Aggregationen/Tile-Kacheln; Apache-2.0.
  - Schwächen (für uns): Inverted/BKD-Index ist komplex/schwergewichtig; nicht ideal für eingebettete, transaktionale Engine; Formen werden vorab zerteilt.

- H3/S2 (hierarchische Raumzellen)
  - Ansatz: Sphärische Diskretisierung in Zellen-IDs (H3: Hex; S2: Quad auf Sphäre). `polyfill(geom, res)` → Zellmengen.
  - Nutzung: Sehr schneller Pre-Filter, Sharding/Partitionierung, Aggregation (Heatmaps/Hexbin). Exakte Geometrieprüfung bleibt erforderlich.
  - Stärken: einfache Mengenlogik (Zellen-Sets), skaliert gut, gute Verfügbarkeit (Apache-2.0), ideal für Geofencing/Viewport/Analytics.
  - Schwächen: Approximation; Auflösung/Genauigkeit Trade-off; 3D (Z) nicht abgedeckt; zusätzlicher Precompute/Storage.

## Ableitungen (Best Practices)
- Storage in der Praxis: Binäres WKB/EWKB, GeoJSON für API/Interchange, Sidecar-Felder (MBR, Centroid) für Indizes/Sortierung.
- Index-Pfad (MVP): R-Tree über MBR (2D) liefert robusten Kandidatenfilter; Z-Range-Index trennt 3D-Pruning; exakte Prüfung mit Boost.Geometry (permissiv) statt hartem GEOS-Depend.
- Tiling/Cells (H3/S2) sind exzellente Pre-Filter/Analytics-Indizes, aber ergänzend, nicht ersetzend.

## Leitentscheidungen (MVP)
1) Storage
   - WKB/EWKB(Z) als internes Storage-Format (Blob-Feld); GeoJSON als API.
   - Normalisierung: CRS = WGS84 (EPSG:4326) in MVP; Transform optional später (PROJ).
   - Sidecar-Metadaten pro Entity: `mbr(minx,miny,maxx,maxy)`, `centroid(lon,lat)`, `z_min`, `z_max`.

2) Index
   - Primär: R-Tree (R*-ähnlich) über 2D-MBR im SecondaryIndexManager; API: `createSpatialIndex(table, column, options)`.
   - 3D: separater Z-Range-Index (B+-/Range) für schnelles Pruning (Z-Overlap) als AND mit MBR.
   - Optional (nicht MVP): H3-Index (1–2 Auflösungen) für Geo-Prefilter/Aggregationen; S2 Evaluierung später.

3) Engine/Query-Ausführung
   - Kandidaten: MBR-Scan über R-Tree (+Z-Range-AND), dann exakte Topologie via Boost.Geometry (Intersects/Within/Contains) und Distanz (Haversine für DWithin/Distance in 2D; 3D später).
   - DWithin: MBR-Expand um geodätischen Puffer (approx), dann exakte Distanzprüfung.
   - KNN: ORDER BY Distance mit MBR-Index-gestütztem Early-Out (k-Best Heap) – MVP zunächst seriell, später Heap-gebunden.

4) Lizenzen
   - Boost.Geometry (BSL-1.0) als Default für exakte Checks.
   - PROJ (MIT/ISC) optional für ST_Transform nach MVP.
   - GEOS (LGPL) nur optional via dyn. Link/Plugin (Prepared Geometries), nicht für MVP erforderlich.
   - H3/S2 (Apache-2.0) optional, nach MVP.

## Begründung
- Implementationsrisiko minimal: R-Tree + Boost.Geometry sind portabel, gut dokumentiert und passen in den bestehenden SecondaryIndexManager/Engine-Ansatz.
- Korrektheit: Exakte Prädikate bleiben möglich (kein reines Zell-Approx), Semantik an PostGIS orientiert.
- Performance: R-Tree liefert hohen Pruning-Faktor; Sidecar `centroid` ermöglicht leichte KNN/Distance; optionaler Z-Range-Index reduziert 3D-Kandidaten signifikant.
- Evolutionspfad: H3/S2 als Zusatzindex (Analytics/Geofencing), GEOS Prepared Geometries als Speed-Upgrade, GPU/Simd-Pfade später.

## Abgrenzung (was wir explizit NICHT im MVP machen)
- Kein OpenSearch-ähnlicher BKD-/inverted Shape-Index (Komplexität/Footprint).
- Keine verpflichtenden Copyleft-Libs im Core (GEOS optional, dynamisch).
- Kein S2/H3 als Primärindex für exakte Topologie (nur Zusatz/Prefilter später).

## Risiken & Mitigations
- Geodätische Genauigkeit (WGS84): MVP nutzt Boost.Geometry + Haversine; PROJ-gestützte ST_Transform folgt in M4.
- Sehr große Polygone/MultiPolygons: Index kann viele Kandidaten liefern → Prepared-Geom/Cache (später) + Zellen/Tiles für Vorabbeschränkung.
- Schreibamplifikation im R-Tree: Mit RocksDB CF "spatial" und inkrementeller Pflege begrenzen; Rebuild-API bereitstellen.

## Nächste Schritte (konkret)
- Storage/Sidecar: EWKB(Z) Parser/Serializer, mbr/centroid/z_min/z_max füllen.
- Index: R-Tree Interface + Metriken (node_count, depth, candidate_count); Z-Range-Index.
- Engine: ST_Intersects/Within/Contains/DWithin/Distance mit Index+Fallback; Tests gegen Boost.Geometry-Referenz.
- Optional nach MVP: H3 Pre-Filter (polyfill), GEOS Prepared, SIMD-Kerne (PiP), Morton-Order.
