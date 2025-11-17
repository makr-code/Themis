# Relationales Schema für Geo-Daten (Post-Go-Live)

Dieses Dokument definiert ein abfragefreundliches relationales Schema für Punkt-, Linien- und Polygon-Daten inklusive Indexierung und Beispielabfragen. Es dient als Zielbild für die Ablage nach dem Ingestion-Prozess.

## Ziele
- Saubere Trennung von Geometrietypen (Point/LineString/Polygon)
- Schnelle räumliche Abfragen (R-Tree/GiST) und begriffliche Filter (B-Tree/FTS/Trigram)
- Kompatibel mit EPSG:4326 (lon/lat); Bounding-Box pro Feature
- Unterstützt Suchszenarien wie: "LSG", "Fließgewässer", nahe (lon, lat)

## Tabellenentwurf (neutral)

### features
- `feature_id` (PK, UUID/ULID)
- `source_id` (TEXT)
- `source_pk` (TEXT)
- `class` (TEXT) — z. B. "LSG", "Fließgewässer"
- `name` (TEXT)
- `bbox_min_lon` (DOUBLE)
- `bbox_min_lat` (DOUBLE)
- `bbox_max_lon` (DOUBLE)
- `bbox_max_lat` (DOUBLE)
- `tags` (JSON)
- `created_at` (TIMESTAMP)

### points
- `feature_id` (FK → features)
- `lon` (DOUBLE)
- `lat` (DOUBLE)

### lines
- `feature_id` (FK → features)
- `geom_wkt` (TEXT) — WKT LineString; optional: normalisierte Stützpunkte in separater Tabelle

### polygons
- `feature_id` (FK → features)
- `geom_wkt` (TEXT) — WKT Polygon (Außenring + Innenringe)

### terms (Begriffe für Volltext/Facetten)
- `feature_id` (FK → features)
- `term` (TEXT)
- `lang` (TEXT, optional)

### synonyms (Synonym-/Alias-Lexikon)
- `term` (TEXT)
- `canonical` (TEXT)
- `lang` (TEXT)

Hinweis: In PostGIS-Umgebungen können `geom_wkt`-Spalten als `geometry`-Typ modelliert werden (GiST-Index). In einfacheren Setups bleiben WKT + BBox; räumliche Filter laufen über BBox-First-Filter + optionale Software-Präzisierung.

## Beispiel-DDL (PostgreSQL/PostGIS optional)

```sql
-- Basis (ohne PostGIS)
CREATE TABLE features (
  feature_id TEXT PRIMARY KEY,
  source_id TEXT NOT NULL,
  source_pk TEXT NOT NULL,
  class TEXT,
  name TEXT,
  bbox_min_lon DOUBLE PRECISION,
  bbox_min_lat DOUBLE PRECISION,
  bbox_max_lon DOUBLE PRECISION,
  bbox_max_lat DOUBLE PRECISION,
  tags JSONB,
  created_at TIMESTAMP DEFAULT now()
);
CREATE INDEX ix_features_class ON features(class);
CREATE INDEX ix_features_name ON features(name);
CREATE INDEX ix_features_bbox ON features(bbox_min_lon, bbox_min_lat, bbox_max_lon, bbox_max_lat);

CREATE TABLE points (
  feature_id TEXT REFERENCES features(feature_id) ON DELETE CASCADE,
  lon DOUBLE PRECISION NOT NULL,
  lat DOUBLE PRECISION NOT NULL
);
CREATE INDEX ix_points_lonlat ON points(lon, lat);

CREATE TABLE lines (
  feature_id TEXT REFERENCES features(feature_id) ON DELETE CASCADE,
  geom_wkt TEXT NOT NULL
);

CREATE TABLE polygons (
  feature_id TEXT REFERENCES features(feature_id) ON DELETE CASCADE,
  geom_wkt TEXT NOT NULL
);

CREATE TABLE terms (
  feature_id TEXT REFERENCES features(feature_id) ON DELETE CASCADE,
  term TEXT NOT NULL,
  lang TEXT
);
CREATE INDEX ix_terms_term ON terms(term);

CREATE TABLE synonyms (
  term TEXT,
  canonical TEXT,
  lang TEXT
);
CREATE INDEX ix_synonyms_term ON synonyms(term);

-- Optional: FTS/Trigram (PostgreSQL-abhängig)
-- CREATE EXTENSION pg_trgm;
-- CREATE INDEX ix_features_name_trgm ON features USING gin (name gin_trgm_ops);
```

```sql
-- Variante mit PostGIS
-- CREATE EXTENSION postgis;
CREATE TABLE features (
  feature_id TEXT PRIMARY KEY,
  source_id TEXT NOT NULL,
  source_pk TEXT NOT NULL,
  class TEXT,
  name TEXT,
  bbox_min_lon DOUBLE PRECISION,
  bbox_min_lat DOUBLE PRECISION,
  bbox_max_lon DOUBLE PRECISION,
  bbox_max_lat DOUBLE PRECISION,
  tags JSONB,
  created_at TIMESTAMP DEFAULT now()
);

CREATE TABLE lines (
  feature_id TEXT REFERENCES features(feature_id) ON DELETE CASCADE,
  geom geometry(LineString, 4326) NOT NULL
);
CREATE INDEX ix_lines_geom_gist ON lines USING gist (geom);

CREATE TABLE polygons (
  feature_id TEXT REFERENCES features(feature_id) ON DELETE CASCADE,
  geom geometry(Polygon, 4326) NOT NULL
);
CREATE INDEX ix_polygons_geom_gist ON polygons USING gist (geom);

CREATE TABLE points (
  feature_id TEXT REFERENCES features(feature_id) ON DELETE CASCADE,
  geom geometry(Point, 4326) NOT NULL
);
CREATE INDEX ix_points_geom_gist ON points USING gist (geom);
```

## Beispiel-Abfragen

### 1) "LSG" in der Nähe eines Punkts (lon=45, lat=16)
- Neutral (BBox-First, grob):
```sql
SELECT f.*
FROM features f
WHERE f.class = 'LSG'
  AND 45 BETWEEN f.bbox_min_lon AND f.bbox_max_lon
  AND 16 BETWEEN f.bbox_min_lat AND f.bbox_max_lat;
```
- PostGIS (präzise):
```sql
SELECT f.*
FROM features f
JOIN polygons p ON p.feature_id = f.feature_id
WHERE f.class = 'LSG'
  AND ST_Contains(p.geom, ST_SetSRID(ST_MakePoint(45, 16), 4326));
```

### 2) "Fließgewässer" nahe Punkt (LineString Distanz)
```sql
SELECT f.*
FROM features f
JOIN lines l ON l.feature_id = f.feature_id
WHERE f.class = 'Fließgewässer'
  AND ST_DWithin(l.geom, ST_SetSRID(ST_MakePoint(45,16), 4326), 1000); -- 1000m
```

### 3) Begriffssuche mit Synonymen
```sql
-- Synonymauflösung (einfach)
SELECT f.*
FROM features f
JOIN terms t ON t.feature_id = f.feature_id
LEFT JOIN synonyms s ON s.term = t.term
WHERE (t.term = 'LSG' OR s.canonical = 'LSG');
```

## ETL-Mapping (aus Ingestion)
- `class`, `name`, `tags` aus `mappings` übernehmen
- `bbox_*` aus normalisierter Geometrie berechnen
- Punkt-/Linie-/Polygon nach Typ in jeweilige Tabelle schreiben (1:n möglich)
- Begriffe (terms) befüllen: `name`, `class`, extrahierte Schlagworte
- Synonyme als Lookup (z. B. aus `geo_classes_v1`)

## Indexierung & Performance
- BBox-Index beschleunigt Grobfilterung vor genauer Geometrieprüfung
- B-Tree auf `class`, `name`; optional FTS/Trigram für unscharfe Suchbegriffe
- GiST/SpGist für Geometriespalten (PostGIS)

## Governance & Qualität
- `source_id` + `source_pk` für Idempotenz/Lineage
- `feature_id` als stabile interne ID (UUID/ULID)
- Validierung: Geometrie-Validität, CRS=EPSG:4326, BBox vorhanden

## Offene Punkte
- Optional: MultiGeometrien (MultiPoint/MultiLineString/MultiPolygon)
- Optional: Generalisierung (Levels of Detail) für schnelle Kartenansichten
- Optional: Historisierung/Versionierung (Valid-From/To)
