# ImageProcessor – Design (Phase 4)

Dieses Dokument beschreibt die Architektur und Testspezifikation für den Bildverarbeitungsprozessor innerhalb des Content/Filesystem-Layers.

## Ziele
- Einheitliche Verarbeitung von Image-Content (JPEG/PNG) über das Processor-Plugin-Modell
- Extraktion von EXIF/Meta (Dimensionen, GPS, Kamera), Thumbnail-Erzeugung
- Chunking als 3x3 Tile-Grid für lokale Features
- Mock-Embedding (768D) kompatibel zum bestehenden Vector-Index (Cosine)

## Contract
- extract(blob, content_type: image/*) → ExtractionResult
  - fields:
    - width, height (px)
    - exif: { camera_make, camera_model, focal_length, iso, datetime_original?, gps_lat?, gps_lon? }
    - mime_type
    - thumbnail (optional, z. B. 256px Kante, JPEG/PNG)
- chunk(extraction, cfg: { grid: 3x3, overlap_px?: 0 }) → [Chunk]
  - Jeder Chunk repräsentiert ein Tile (row, col, bbox_px)
  - payload: { tile_bbox: {x,y,w,h}, stats: { mean_rgb, std_rgb } }
- generateEmbedding(chunk_payload) → float[768]
  - Mock-CLIP: deterministische Hash-Verteilung über Pixel-Statistiken + Position
  - L2-normalisiert

## Datenablage (Schlüssel/JSON)
- content:<id> → Meta (mime_type,image meta, exif, dims)
- content_blob:<id> → Originalbild (binary)
- content_thumbnail:<id> → Thumbnail (binary)
- content_chunks:<id> → [chunk_ids]
- chunk:<chunk_id> → { parent_id, row, col, bbox_px, stats, embedding_ref }
- Hinweis: Vector-Index unter Namespace "chunks" (dim=768, COSINE)

## Tile-Spezifikation
- Grid 3x3 über (width,height)
- bbox_px Berechnung: floor/ceil so verteilen, dass alle Pixel abgedeckt sind
- stats: mean/std RGB (grob, optional proxy aus Thumbnail/Downscale)

## Embedding (Mock)
- Input: { mean_rgb, std_rgb, row, col, width, height }
- Hash-basierte Projektion mit 3 Seeds → 768D; leichte Positionskodierung
- L2-Normalisierung (Cosine-kompatibel)

## EXIF/Meta
- Felder:
  - Dimensionen: width/height
  - Kamera: make/model
  - Aufnahme: datetime_original, iso, focal_length
  - GPS: gps_lat, gps_lon (falls vorhanden)
- Fehlertoleranz: fehlende EXIF zulässig; gps optional

## Tests (20 Unit Tests)
1-3) extract: liest width/height korrekt (JPEG/PNG Samples)
4) extract: fehlende EXIF → Felder optional
5) extract: GPS parsing korrekt
6-10) chunk: 3x3 Tiles count/bbox korrekt, Gesamtfläche abgedeckt
11-13) chunk: stats plausibel (mean in [0,1], std ≥ 0)
14-17) embedding: dimension=768, L2≈1.0, deterministisch bei idempotentem Input
18) embedding: unterschiedliche Tiles → unterschiedliche Vektoren (cosine<0.99)
19) integration: ingest → vector index init (dim 768)
20) integration: retrieval of chunks ohne Embeddings (Datenschutz/Antwortgröße)

## Open Points
- Farbmanagement/EXIF-Orientierung berücksichtigen (Rotation)
- Downscale-Strategie (schneller Pfad via Thumbnail)
- Erweiterung: Face/Logo-Detektion (später, extern)
