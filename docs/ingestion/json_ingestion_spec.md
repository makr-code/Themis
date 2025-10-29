# JSON Ingestion Spezifikation (Post-Go-Live)

Ziel dieses Dokuments ist, den standardisierten JSON-gestützten Ingestion-Prozess (ETL) zu definieren, damit strukturierte, Geo- und Textdaten aus heterogenen Quellen konsistent, abfragefreundlich und revisionssicher in die Kerndatenbank übernommen werden.

## Zweck & Scope
- Einheitlicher Contract für alle Quellen (GeoJSON, GPX, CSV, proprietär, Text/Binary mit Metadaten)
- Abfragefähigkeit entlang dreier Achsen: Relational (Attribute/Begriffe), Räumlich (Punkt/Linie/Polygon), Semantisch (Vektor)
- Qualität, Idempotenz, Deduplikation, Lineage/Audit als First-Class-Bestandteile

## Mini-Contract (Inputs/Outputs)
- Input: JSON-Dokument (Payload) + optionaler Binärblob (z. B. Originaldatei)
- Output:
  - Relationale Records (Tables: features/points/lines/polygons/terms/synonyms)
  - Optional Blob-Speicher (Original) + Hash
  - Indizes: räumlich (BBox/GiST), textuell (B-Tree/FTS/Trigram), vektoriell
- Fehler/Qualität: Validierungsfehler in DLQ; Metriken/Logs; Retry mit Idempotenz-Schlüssel

## JSON-Struktur (generisch)
```json
{
  "source_id": "behörde-bayern-lsg-2025",
  "source_pk": "ext-12345",
  "content_type": "geo",
  "geo": {
    "type": "Polygon",
    "crs": "EPSG:4326",
    "coordsPath": "features[0].geometry.coordinates",
    "bbox": [minLon, minLat, maxLon, maxLat]
  },
  "text": {
    "language": "de",
    "fields": ["properties.name", "properties.category"],
    "tokenization": "default"
  },
  "mappings": {
    "name": "properties.name",
    "class": "properties.category",
    "tags": "properties.tags"
  },
  "transforms": [
    {"op": "trim", "field": "name"},
    {"op": "upper", "field": "class"},
    {"op": "synonym_map", "field": "class", "dict": "geo_classes_v1"}
  ],
  "provenance": {
    "ingested_at": "2025-10-28T12:00:00Z",
    "ingested_by": "etl@system",
    "license": "CC-BY"
  },
  "metadata": {"version": 1, "note": "LSG Import 2025"}
}
```

Hinweis: Für reine Textquellen entfällt der Block `geo`. Für GPX/LineStrings: `type: "LineString"`. Für Punktdaten: `type: "Point"` und `coordsPath` verweist auf `[lon,lat]`.

## Pipeline-Schritte
1. detect: MIME/Category, ggf. Magic Bytes
2. extract: Quelle lesen (GeoJSON/GPX/CSV/Text)
3. normalize:
   - Geo: nach EPSG:4326 (lon/lat), BBox berechnen, MultiGeometrien auflösen
   - Text: Unicode-Normalisierung, Language-Detection (falls nicht vorgegeben)
4. map: Felder gemäß `mappings` extrahieren, `transforms` anwenden
5. validate(schema): Pflichtfelder, Datentypen, Geometrie-Validität (self-intersections)
6. write:
   - Relationale Tabellen (features + points/lines/polygons)
   - Optional: blobs + content_hash
7. index: räumliche Indizes, textuelle Indizes, Vektor-Embeddings (optional)
8. lineage/audit: Provenienz und Hash-Manifest erfassen

## Idempotenz & Deduplikation
- Idempotenz-Schlüssel: `(source_id, source_pk)`
- Deduplizierung: `content_hash` (SHA-256 des Normalform-JSONs bzw. Blobs)
- Wiederholungen: Upsert-Strategie mit Versionszähler (optimistische MVCC)

## Qualitätsregeln & Fehlerbehandlung
- Validation Errors → DLQ (JSON + Fehlerliste)
- Retry-Politik mit Backoff; Max-Retries konfigurierbar
- Metriken: `ingested_total`, `failed_total`, `duplicates_total`, Latenzen je Schritt

## Beispiel 1: LSG (Polygon, GeoJSON)
```json
{
  "source_id": "behörde-bayern-lsg-2025",
  "source_pk": "lsg-987",
  "content_type": "geo",
  "geo": {"type": "Polygon", "crs": "EPSG:4326", "coordsPath": "geometry.coordinates"},
  "mappings": {"name": "properties.name", "class": "properties.category"},
  "transforms": [{"op":"synonym_map","field":"class","dict":"geo_classes_v1"}],
  "metadata": {"dataset": "LSG", "year": 2025}
}
```

## Beispiel 2: Fließgewässer (Linie, GPX → LineString)
```json
{
  "source_id": "wasserverband-gpx-2025",
  "source_pk": "track-42",
  "content_type": "geo",
  "geo": {"type": "LineString", "crs": "EPSG:4326", "coordsPath": "tracks[0].segments[0]"},
  "mappings": {"name": "metadata.track_name", "class": "metadata.feature_class"},
  "metadata": {"dataset": "Fließgewässer"}
}
```

## Beispiel 3: Textdokument
```json
{
  "source_id": "dms-ordner-a",
  "source_pk": "doc-1001",
  "content_type": "text",
  "text": {"language": "de", "fields": ["title", "body"], "tokenization": "default"},
  "mappings": {"name": "title", "tags": "keywords"},
  "metadata": {"doctype": "bericht"}
}
```

## Indexierung (Hinweise)
- Räumlich: R-Tree/GiST (BBox bzw. Geometriespalte)
- Text: B-Tree auf `class/name`, optional FTS/Trigram für unscharfe Suche
- Vektor: Embeddings für Name/Tags (optional) + ANN-Index

## Versionierung & Governance
- Schema-Version je Quelle (`metadata.version`)
- Änderungen via Migrationsnotiz; kompatible Evolution bevorzugt
- Audit: `provenance` speichern; Hash-Manifest für Unveränderlichkeit

## Offene Punkte
- Einheitliche Synonymlisten-Verwaltung (z. B. `geo_classes_v1`)
- Vereinheitlichung der DLQ-Formate und Monitoring-Alarmierung
