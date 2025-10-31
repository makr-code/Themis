# Covina FastAPI-Ingestion – Modalitäten-Übersicht

Diese Dokumentation beschreibt, wie die Covina-Ingestion (FastAPI) verschiedene Datentypen für die THEMIS-Content-API vorbereitet. Ziel: Maximale Verknüpfbarkeit und Kompatibilität gemäß Leitfaden in `docs/content/ingestion.md`.

## Grundprinzip
- Die Ingestion extrahiert, chunked und annotiert alle Inhalte so, dass sie als strukturierte JSON-Payloads (`content`, `chunks`, `edges`, optional `blob`) an THEMIS `/content/import` gesendet werden können.
- Embeddings werden pro Chunk erzeugt (ML-Modell oder Fallback), Kanten werden explizit modelliert.

## Unterstützte Modalitäten & empfohlene Pipeline-Schritte

### 1. Text / Schriftdokumente (PDF, TXT, DOCX)
- Layout-aware Parsing (Absätze, Überschriften, Tabellen, Fußnoten)
- Chunking: Absätze 200–400 Tokens, Tabellen als eigene Chunks
- Embeddings pro Text-Chunk
- Edges: `references` (Zitate/DOIs/URLs), `mentions` (Begriffe)

### 2. Bilddateien (JPEG, PNG, TIFF)
- EXIF/Metadaten extrahieren (Kamera, Zeit, GPS)
- Objekterkennung/Tags (z. B. Personen, Orte, Dinge)
- OCR für eingebetteten Text
- Captioning (Kurzbeschreibung)
- Embeddings (z. B. CLIP)
- Edges: `depicts`, `similar`, `located_in`, `derived_from`

### 3. Video (MP4, MOV, AVI)
- Szenen-/Shot-Erkennung, Keyframes extrahieren
- ASR-Transkript + Sprecherdiarisierung
- Objekterkennung/Tracking
- Segment-Embeddings
- Edges: `contains`, `derived_from`, `mentions`, `next`

### 4. Audio (MP3, WAV, FLAC)
- ASR-Transkript, Sprecher-Erkennung
- Segmentierung (Voice Activity Detection)
- Embeddings pro Segment
- Edges: `next`, `mentions`

### 5. Tabellen (CSV, XLSX)
- Schema- und Typinferenz
- Chunking: Zeilen als `row`-Chunks, Kopf als `schema`-Chunk
- Embeddings optional (aus textuellen Spalten)
- Edges: `references`, `foreign_key`

### 6. CAD-Daten (STEP, IGES, ZIP)
- Stückliste (BOM), Komponenten, Unterbaugruppen
- Geometrische Metadaten, Material, Toleranzen
- Embeddings für Teile/Assemblies
- Edges: `contains`, `mate`, `similar`, `derived_from`

### 7. Geodaten (GeoJSON, SHP)
- Geometrien normieren (GeoJSON, EPSG:4326)
- Bounding Box, Vereinfachung
- Verknüpfungen zu administrativen Einheiten
- Edges: `located_in`, `adjacent_to`, `part_of`

## Beispiel-Payload (Text)
```json
{
  "content": {
    "id": "doc-001",
    "mime_type": "text/plain",
    "tags": ["demo"],
    "user_metadata": {"dataset": "alpha"}
  },
  "chunks": [
    {"seq_num": 0, "chunk_type": "text", "text": "Hello world", "embedding": [0.1, 0.2, ...] }
  ],
  "edges": []
}
```

## Hinweise
- Die Ingestion kann beliebig erweitert werden (z. B. Bild-, Video-, Audio-, CAD-, Geo-Prozessoren als Plug-ins).
- Embedding-Dimension muss konsistent sein (wird beim ersten Insert in THEMIS festgelegt).
- Kanten-Typen und Gewichtung siehe Leitfaden in `docs/content/ingestion.md` und `/config/edge-weights`.
- Datenschutz: PII vor Import anonymisieren/maskieren.

## Weiterführende Doku
- THEMIS Content-API: `docs/content/ingestion.md`
- Edge-Weights: `/config/edge-weights`
- OpenAPI: `docs/openapi.yaml`
