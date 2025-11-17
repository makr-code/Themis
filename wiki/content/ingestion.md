# Content v0: Bulk Ingestion

Dieses Dokument beschreibt das v0-Schema für den Bulk-Import von bereits vorverarbeiteten Inhalten über die HTTP-API. Ziel: Einfache, robuste Übernahme von Content-Metadaten, optionalem Original-Blob, Chunks (inkl. Embeddings) und optionalen Graph-Kanten.

Stand: MVP, stabil genug für erste Integrationen. Erweiterungen (z. B. SSE-CDC, fortgeschrittene Filter) folgen später.

## Endpunkte

- POST /content/import
  - Nimmt einen strukturierten JSON-Spec entgegen und speichert Content-Meta, Chunk-Metas, optional den Original-Blob sowie optional Graph-Kanten.
- GET /content/{id}
  - Liefert die Content-Metadaten zurück.
- GET /content/{id}/blob
  - Liefert den gespeicherten Binärblob (Content-Type aus `mime_type`).
- GET /content/{id}/chunks
  - Liefert alle Chunks (Metadaten). Aus Platzgründen wird die `embedding`-Liste in der Antwort auf leere Arrays normalisiert.

Hinweis: Alle IDs können mit oder ohne Prefix übergeben werden. Intern werden folgende Keys verwendet:
- content:{id}, content_blob:{id}, content_chunks:{id}
- chunk:{id}

## Import-Schema (Request)

Request-Body für POST /content/import:

{
  "content": {
    "id": "optional-string-uuid",
    "mime_type": "text/plain",
    "category": 0,
    "original_filename": "optional.txt",
    "size_bytes": 0,
    "created_at": 0,
    "modified_at": 0,
    "hash_sha256": "",
    "text_extracted": false,
    "chunked": false,
    "indexed": false,
    "chunk_count": 0,
    "embedding_dim": 0,
    "extracted_metadata": {},
    "user_metadata": {},
    "tags": ["optional", "tags"],
    "parent_id": "",
    "child_ids": []
  },
  "chunks": [
    {
      "id": "optional-chunk-id",
      "content_id": "optional-content-id",
      "seq_num": 0,
      "chunk_type": "text",
      "text": "optional chunk text",
      "data": {},
      "blob_ref": "",
      "start_offset": 0,
      "end_offset": 0,
      "embedding": [/* optional float[] */],
      "embedding_indexed": false,
      "created_at": 0
    }
  ],
  "edges": [
    { "from": "nodeA", "to": "nodeB", "type": "next", "weight": 1.0 }
  ],
  "blob": "optional raw blob als String" 
  // alternativ: "blob_base64": "..." (derzeit ohne Dekodierung im Server-MVP)
}

Erläuterungen:
- content.id: Wenn nicht gesetzt, wird serverseitig eine UUID generiert.
- blob: Wird unter key "content_blob:{id}" gespeichert. Größe wird in `content.size_bytes` übernommen.
- chunks[*].id: Wird generiert, falls nicht angegeben. `content_id` wird auf die Content-ID normiert.
- chunks[*].embedding: Falls gesetzt und Dimension zum initialisierten Vektorindex passt, wird ein Vektorobjekt mit PK "chunks:{chunk_id}" im VectorIndex angelegt. Ist noch kein Index initialisiert, wird er implizit mit Objektname "chunks" und COSINE-Metrik anhand der ersten Embedding-Dimension initialisiert.
- edges: Optionale Kanten, die als Graph-Edges gespeichert werden (frei gestaltbares Feldschema; häufig: from, to, type, weight).

## Antworten

- 200 OK: { "status": "success", "content_id": "..." }
- 4xx/5xx mit Schema { error: true, message: string, status_code: number }

## Beispiele

Minimaler Text-Import ohne Embeddings:

POST /content/import
Content-Type: application/json

{
  "content": {
    "id": "doc-001",
    "mime_type": "text/plain",
    "user_metadata": {"dataset": "alpha"},
    "tags": ["demo"]
  },
  "blob": "Hello world",
  "chunks": [
    {"seq_num": 0, "chunk_type": "text", "text": "Hello"},
    {"seq_num": 1, "chunk_type": "text", "text": "world"}
  ]
}

Antwort:

{
  "status": "success",
  "content_id": "doc-001"
}

Abfrage der Metadaten:
- GET /content/doc-001 → ContentMeta inkl. chunk_count
- GET /content/doc-001/chunks → { count, chunks: [...] } (embedding-Felder leere Arrays)
- GET /content/doc-001/blob → liefert den Blob (Content-Type: text/plain)

## Filter- und Graph-Funktionen (Ausblick)

- Hybrid-/Vektorsuche: POST /search/hybrid (siehe allgemeine Doku); nutzt VectorIndex plus optionale Graph-Expansion.
- Filterkonfiguration: GET/PUT /config/content-filters
  - Schema: { field_map: { alias: "json.pfad.im.contentmeta" } }
  - Dient der Deklaration, welche Felder in Content-Metadaten für Filter verwendet werden können.
- Kanten-Gewichte: GET/PUT /config/edge-weights

## Kompatibilität und Grenzen

- `blob_base64` wird aktuell nicht automatisch dekodiert; bitte `blob` verwenden oder selbst dekodieren, bevor es gesendet wird.
- `category` ist derzeit als numerischer Enumwert erwartbar (0=TEXT, 1=IMAGE, ...); ist optional, da `mime_type` für viele Workflows ausreichend ist.
- Embeddings erfordern einen initialisierten VectorIndex (wird bei erstem Einfügen implizit für Objektname "chunks" angelegt).

## Troubleshooting

- 400 Invalid JSON: Request-Body ist kein valides JSON.
- 500 failed to store ...: Persistenzfehler (RocksDB). Logs prüfen.
- Chunks erscheinen ohne Embeddings: Prüfen, ob Dimension konsistent ist bzw. ob überhaupt Embeddings übergeben wurden.

---

# Ingestion-Leitfaden: Vorverarbeitung vor dem Import (Maximale Verknüpfbarkeit)

Ziel dieses Leitfadens ist es, externe Ingestion-Pipelines so zu beschreiben, dass Inhalte vor dem Schreiben in THEMIS maximal verknüpfbar sind. Die DB selbst führt keine Extraktion durch – sie erwartet bereits strukturierte JSON-Daten gemäß `POST /content/import` und baut darauf Indexe (Dokument-, Graph- und Vektorindex) auf.

Die folgenden Empfehlungen liefern pro Datentyp konkrete Schritte, Felder und Kanten-Modelle, um Breite (viele Verbindungsmöglichkeiten) und Tiefe (hohe semantische Dichte) zu maximieren.

## Grundprinzipien

- Eindeutige, stabile IDs: Bestimmen Sie `content.id` deterministisch (z. B. Hash aus Normal-Form des Quellobjekts). Chunk-IDs können aus `content.id` + Positionsmerkmalen abgeleitet werden.
- Chunking vor Embedding: Zerlegen Sie Inhalte semantisch (Absätze, Szenen, Segmente, Zeilen, Komponenten), erzeugen Sie dann pro Chunk Embeddings. So bleiben Kanten und Kontext fein granular.
- Kanonisches Schema nutzen: Mappen Sie alles auf `content` (Metadaten), `chunks` (Kleinstrukturen), `edges` (Relationen). Halten Sie sich an wiederkehrende Schlüssel:
  - `source_uri`, `hash_sha256`, `created_at`, `modified_at`, `language`, `authors`, `topics`, `tags`
  - Zeitliche Felder: `start_ms`, `end_ms` (Audio/Video-Segmente)
  - Räumliche Felder: `lat`, `lon`, `bbox`, `srid`, `geometry`
  - Strukturfelder in `chunks[*].data`: typ-spezifisch (siehe unten)
- Ontologien & kontrollierte Vokabulare: Verwenden Sie normierte Werte (ISO-Sprachcodes, EPSG für Geo, Standard-Einheiten, taxonomische Kategorien), um Abgleich/Join zu erleichtern.
- Kanten explizit modellieren: Nutzen Sie `edges` mit `type` und optional `weight`. Empfohlene Typen: `derived_from`, `contains`, `references`, `mentions`, `same_as`, `next`, `similar`, `depicts`, `located_in`, `belongs_to`, `mate` (CAD).
- Datenschutz: Entfernen/Maskieren Sie PII frühzeitig. Speichern Sie nur notwendige Felder, nutzen Sie Pseudonyme/Hashes bei Bedarf.

## Modalitätsspezifische Leitfäden

### 1) Bilder (Rastergrafiken)

Pipeline-Schritte:
- EXIF/Metadaten extrahieren (Kamera, Zeit, GPS)
- Objekterkennung/Tags (z. B. Personen, Orte, Dinge) – `chunks[*].data.objects`
- OCR für eingebetteten Text – Ergebnisse als eigener Text-Chunk
- Bildunterschrift/Captioning – kurze Beschreibung als Text-Chunk
- Perceptual Hash (pHash/aHash) – Deduplikation/Ähnlichkeit
- Multimodales Embedding (z. B. CLIP) für Retrieval

Mapping:
- `content.mime_type = image/jpeg|png` | `category = IMAGE`
- Chunks:
  - `chunk_type = image_meta` mit `{ exif: {...}, p_hash: "...", colors: [...] }`
  - `chunk_type = text` für OCR/Caption (`text` gesetzt)
  - `embedding` am passenden Chunk (z. B. Caption-Chunk oder `image_meta`)
- Kanten:
  - `derived_from` (zwischen Transformaten/Thumbnails und Original)
  - `similar` (zu anderen Bildern per pHash/Embedding)
  - `depicts` (zu Entitäten: Personen, Orte, Objekte)

Beispiel (auszugsweise):
```
{
  "content": {"id":"img:beach-001","mime_type":"image/jpeg","tags":["beach","sunset"],"extracted_metadata":{"exif":{"gps":{"lat":36.6,"lon":-121.9}}}},
  "chunks": [
    {"seq_num":0,"chunk_type":"image_meta","data":{"p_hash":"cafe1234","objects":["person","sea"],"colors":["#ffaa77","#003355"]},"embedding":[...]},
    {"seq_num":1,"chunk_type":"text","text":"A person walking along the beach at sunset.","embedding":[...]}
  ],
  "edges":[{"from":"img:beach-001","to":"place:carmel","type":"located_in","weight":0.9}]
}
```

### 2) Video

Pipeline-Schritte:
- Szenen-/Shot-Erkennung → Szenen-Segmente mit `start_ms`/`end_ms`
- Keyframes extrahieren (als separate Bild-Contents, mit `derived_from`-Kanten)
- ASR-Transkript (Sprache erkennen, ggf. Übersetzung) + Sprecherdiarisierung
- Objekterkennung/Tracking über Zeit (Tracks als Arrays)
- Segment-Embeddings (z. B. pro Szene, pro 2–5 Sekunden)

Mapping:
- `content.mime_type = video/mp4` | `category = VIDEO`
- Chunks:
  - `chunk_type = scene` mit `{ start_ms, end_ms, transcript, speakers:[...], objects:[...], tracks:[...] }`
  - `chunk_type = text` für reine Transkript-Segmente
  - Embeddings am jeweiligen Segment-Chunk
- Kanten:
  - `contains` (Video → Keyframe-Bilder), `derived_from` (Keyframe → Video)
  - `mentions`/`depicts` zu erkannten Entitäten
  - `next` zwischen Szenen in zeitlicher Reihenfolge

Beispiel (auszugsweise):
```
{
  "content":{"id":"vid:promo-2025","mime_type":"video/mp4","duration_ms":90000},
  "chunks":[
    {"seq_num":0,"chunk_type":"scene","data":{"start_ms":0,"end_ms":6000,"transcript":"Welcome to...","speakers":["spk1"]},"embedding":[...]},
    {"seq_num":1,"chunk_type":"scene","data":{"start_ms":6000,"end_ms":12000,"transcript":"Our product..."},"embedding":[...]}
  ],
  "edges":[{"from":"vid:promo-2025","to":"img:keyframe-abc","type":"contains"}]
}
```

### 3) Audio

Pipeline-Schritte:
- ASR-Transkript + Sprecher-Erkennung
- Segmentierung (z. B. Voice Activity Detection) mit `start_ms`/`end_ms`
- Audio-Embeddings pro Segment

Mapping:
- `content.mime_type = audio/mpeg|wav` | `category = AUDIO`
- `chunk_type = segment|text` mit zeitlichen Feldern und `text`
- Kanten: `next` (Sequenz), `mentions` (Erwähnte Entitäten)

### 4) Schriftdokumente (PDF/DOCX)

Pipeline-Schritte:
- Layout-aware Parsing (Abschnitt, Überschrift, Absatz, Liste, Tabelle, Fußnote)
- Semantische Chunking-Regeln (Absatzblöcke 200–400 Tokens; Tabellen als eigene Chunks)
- Referenzen/Zitationen extrahieren (DOIs, URLs) → `edges: references`
- Embeddings pro Text-Chunk

Mapping:
- `mime_type = application/pdf|vnd.openxmlformats-officedocument.wordprocessingml.document`
- Chunks:
  - `chunk_type = text` mit `{ section:"2.3", page: 5, heading: "Method" }`
  - `chunk_type = table` mit `{ schema: {...}, cells: [...], page: n }` (kompakt halten)
- Kanten:
  - `references` (Zitationen), `mentions` (Begriffe), `same_as` (Duplikat-Funde)

### 5) Tabellendokumente (CSV/XLSX)

Pipeline-Schritte:
- Schema- und Typinferenz (Datums-/Zahl-/Einheiten-Normalisierung)
- Fremdschlüssel-/Referenzen ableiten (Spalten, die IDs/Schlüssel enthalten)
- Zeilen-Embeddings (aus konkatenierten textuellen Spalten) optional

Mapping:
- `mime_type = text/csv|application/vnd.openxmlformats-officedocument.spreadsheetml.sheet`
- Chunks:
  - `chunk_type = row` mit `{ row_index, values: {col: val, ...}, normalized: {...} }`
  - `chunk_type = schema` für Kopf-/Spaltenmetadaten
- Kanten:
  - `references`/`foreign_key` (Zeile → referenzierte Entität/Content)

Beispiel (auszugsweise):
```
{
  "content":{"id":"csv:customers-2025","mime_type":"text/csv","user_metadata":{"dataset":"crm"}},
  "chunks":[
    {"seq_num":0,"chunk_type":"schema","data":{"columns":[{"name":"customer_id","type":"string"},{"name":"city","type":"string"}]}},
    {"seq_num":1,"chunk_type":"row","data":{"row_index":1,"values":{"customer_id":"C-001","city":"Berlin"}}}
  ],
  "edges":[{"from":"chunk:csv:customers-2025:row:1","to":"city:berlin","type":"references","weight":0.8}]
}
```

### 6) CAD-Daten (BOM/Assemblies)

Pipeline-Schritte:
- Ableitung der Stückliste (BOM), Komponenten, Unterbaugruppen
- Geometrische Metadaten (Abmessungen, Material, Toleranzen), optional vereinfachte Meshes/Thumbnails
- Ähnlichkeitssuche (Feature-Hash/Embedding) für „ähnliche Teile“
- Constraints/Mates zwischen Komponenten

Mapping:
- `mime_type = application/vnd.cad+zip|step|iges` (je nach Quelle)
- Chunks:
  - `chunk_type = cad_part` mit `{ part_no, name, material, dims:{...}, mass, attributes:{...} }`
  - `chunk_type = cad_assembly` mit `{ components:[{ref:"...", qty:n}], constraints:[...]} `
- Kanten:
  - `contains` (Assembly → Part), `mate` (Part ↔ Part), `similar` (Part ↔ Part)
  - `derived_from` (Darstellungen wie Thumbnails ↔ Original)

### 7) Geodaten

Pipeline-Schritte:
- Geometrien normieren (GeoJSON), Ziel-SRID EPSG:4326 (WGS84)
- Bounding Box berechnen (`bbox`), ggf. Vereinfachung für Übersicht
- Verknüpfungen zu administrativen Einheiten (Gemeinde, Landkreis, Bundesland) – vorberechnen
- Zeitliche Gültigkeit (falls vorhanden) als Felder abbilden

Mapping:
- `mime_type = application/geo+json|application/json`
- Chunks:
  - `chunk_type = feature` mit `{ geometry: {...}, srid: 4326, bbox: [...], properties: {...} }`
- Kanten:
  - `located_in` (Feature → admin Einheit), `adjacent_to`, `part_of`

Beispiel (auszugsweise):
```
{
  "content":{"id":"geo:park-berlin","mime_type":"application/geo+json"},
  "chunks":[{"seq_num":0,"chunk_type":"feature","data":{"srid":4326,"geometry":{"type":"Polygon","coordinates":[... ]},"bbox":[...],"properties":{"name":"Tiergarten"}}}]
}
```

## Kanten-Design und Gewichte

- Verwenden Sie sprechende `type`-Werte (siehe oben). Gewichte `weight` optional verwenden, um Relevanz (0–1) zu codieren.
- Systemweite Gewichtung kann via `/config/edge-weights` kalibriert werden.
- Reihenfolge-Kanten (`next`) erleichtern zeitliche/strukturelle Pfade (z. B. Video-Szenen, Dokument-Absätze).

## IDs, Deduplikation, Provenienz

- IDs deterministisch: `id = prefix:base64(sha256(normalized_source))`
- `hash_sha256` am `content` für schnelle Duplikatprüfung
- `source_uri` und `provenance` (z. B. Tool-Versionen) in `extracted_metadata`

## Qualitätssicherung (Empfehlungen)

- Mindestabdeckung: ≥95% der Chunks mit Spracheintrag `language` (falls Text vorhanden)
- Embedding-Dimension konsistent halten; Fehlwürfe (NaNs) filtern
- Tabellen: Typinferenzquote und Normalisierung dokumentieren
- ASR: Wort-Fehlerrate (WER) erfassen; Sprecherlabels plausibilisieren

## Performance & Größe

- Text-Chunks 200–400 Tokens für gute Retrieval/Ranking-Qualität
- Video/Audio-Segmente 2–10 Sekunden für annehmbare Granularität
- Bilder: OCR-Text als eigener Chunk, Bild-Captioning kurz halten (≤ 200 Zeichen)
- Große Payloads ggf. in Batches aufteilen; `chunk_count` am `content` liefern

## Sicherheit & Datenschutz

- PII vor Import anonymisieren; nur erforderliche Teile speichern
- Hashen/Pseudonymisieren, wo möglich
- Vollständige Ereignis-/Zugriffsketten außerhalb von THEMIS auditieren

---

Mit diesem Leitfaden werden Inhalte so vorbereitet, dass THEMIS sie optimal über Dokument-, Graph- und Vektorindex verknüpfen kann. Die Beispiele zeigen, wie Modalitäten in das kanonische `content/chunks/edges`-Schema abgebildet werden, um reichhaltige Abfragen (Hybrid-Suche, Pfad-Expansion, Analysen) zu ermöglichen.
