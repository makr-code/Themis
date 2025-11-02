# GeoProcessor – Design (Phase 4)

Dieses Dokument beschreibt die Verarbeitung von Geo-Daten (GeoJSON/GPX) im Content/Filesystem-Layer.

## Ziele
- Extraktion und Normalisierung von Geometrien (Point/LineString/Polygon) nach EPSG:4326
- Berechnung der Bounding Box und optionale Vereinfachung
- Chunking in räumliche Tiles (z. B. quadtree-ähnlich oder fixes Grid)
- Embedding aus lat/lon (grid-based) für grobe semantische Nähe

## Contract
- extract(blob/json, content_type: application/geo+json | application/gpx+xml) → ExtractionResult
  - fields:
    - geometry_type: Point | LineString | Polygon | Multi*
    - coordinates (normalisiert, lon/lat)
    - bbox: [minLon, minLat, maxLon, maxLat]
    - properties: Map<string,any>
- chunk(extraction, cfg: { tile_size_deg: 0.1, max_tiles?: 64 }) → [Chunk]
  - Schneidet Geometrie gegen ein reguläres Lon/Lat-Grid (Kachelung)
  - payload je Chunk: { tile_bbox, geom_fraction, length_or_area }
- generateEmbedding(chunk_payload) → float[128]
  - 128D Vektor aus (tile center lon/lat) + scale; normalisiert

## Datenablage (Verweis)
- siehe `docs/storage/geo_relational_schema.md` (features/points/lines/polygons)
- Content-Layer speichert zusätzlich `content:<id>` Meta (bbox, type, props)

## Normalisierung
- CRS nach EPSG:4326 (lon/lat)
- BBox aus Geometrie berechnen
- MultiGeometrien in Einzelgeometrien aufteilen (optional)

## Chunking
- Reguläres Grid über BBox; für jede Kachel, die Geometrie schneidet → Chunk
- `geom_fraction`: Anteil der Geometrie in der Kachel (Heuristik)
- `length_or_area`: Metrik (Linienlänge/Polygonfläche) innerhalb der Kachel

## Embedding (grid-based)
- Positionsembedding über sinus/cosinus Kodierung der lon/lat des Kachelzentrums
- Skalierung über Zoom/Frequenzen; Ausgabe 128D, L2-normalisiert

## Tests (20 Unit Tests)
1) extract: Point korrekt normalisiert + bbox
2-3) extract: LineString/Polygon bbox korrekt
4) extract: MultiPolygon → mehrere Einheiten (optional)
5-8) chunk: Kachelzählung abhängig von bbox/tile_size
9-12) chunk: geom_fraction plausibel (0..1)
13-14) chunk: length/area konsistent
15-17) embedding: 128D, L2≈1.0, deterministisch
18-20) integration: insert + index + query stub

## Open Points
- GPX Höhenprofil/Elevation, Zeitstempel
- Genauere Flächen-/Längenberechnung (Geodäsie)
- Integration mit räumlichen Indizes (RocksDB sekundär vs. externer Store)
