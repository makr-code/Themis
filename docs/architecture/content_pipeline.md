# Content Pipeline

## Überblick

Die **Content Pipeline** ist das zentrale System für die Verarbeitung heterogener Datentypen in THEMIS. Sie ermöglicht es, beliebige Inhalte (Text, Bilder, Geodaten, CAD, Audio, strukturierte Daten) zu importieren, zu transformieren und für Vektor-, Graph- und Attributsuche bereitzustellen.

**Kernkonzepte:**

- **Modulare Architektur:** Typ-spezifische Prozessoren (`IContentProcessor`) für jede Datenkategorie
- **Einheitliche API:** Ein Import-Endpoint (`POST /content/import`) für alle Datentypen
- **Wiederverwendbare Komponenten:** Deduplication, Chunking, Graph-Erstellung, Embedding-Generierung
- **Kanonisches Schema:** Strukturiertes JSON-Format (Content/Chunks/Edges) für alle Modalitäten
- **RAG-Ready:** Automatische Graph-Konstruktion mit `parent`, `next`, `prev`, `contains`, `sibling` Edges für kontextuelle Suche

**Architektur-Diagramm:**

```
Client → POST /content/import
           ↓
    ContentManager (Orchestrator)
           ↓
    ┌──────────────────────────┐
    │  ContentTypeRegistry     │  MIME → Category Mapping
    └──────────────────────────┘
           ↓
    ┌──────────────────────────┐
    │  Processor Routing       │  Category → IContentProcessor
    └──────────────────────────┘
           ↓
    ┌───────────────────────────────────────────────┐
    │  Prozessoren (TextProcessor, ImageProcessor,  │
    │  GeoProcessor, CADProcessor, AudioProcessor,  │
    │  StructuredProcessor, BinaryProcessor)        │
    └───────────────────────────────────────────────┘
           ↓
    ┌──────────────────────────┐
    │  Storage Layer           │
    │  • RocksDB (Metadata)    │
    │  • VectorIndex (HNSW)    │
    │  • GraphIndex (Edges)    │
    │  • SecondaryIndex (Tags) │
    └──────────────────────────┘
```

## 1. Content-Type-System

### 1.1 ContentCategory

THEMIS unterstützt 9 Content-Kategorien:

| Kategorie       | Beschreibung                          | Beispiel-MIME-Types                          |
|-----------------|---------------------------------------|----------------------------------------------|
| `TEXT`          | Textdokumente, Code, Markdown         | `text/plain`, `application/json`, `text/xml` |
| `IMAGE`         | Fotos, Diagramme, Screenshots         | `image/jpeg`, `image/png`, `image/webp`      |
| `AUDIO`         | Musik, Podcasts, Sprachchips          | `audio/mpeg`, `audio/wav`, `audio/flac`      |
| `VIDEO`         | Videos                                | `video/mp4`, `video/webm`                    |
| `GEO`           | Geodaten, GIS                         | `application/geo+json`, `application/gpx`    |
| `CAD`           | 3D-Modelle, CAD-Zeichnungen           | `model/step`, `model/iges`, `model/stl`      |
| `STRUCTURED`    | Tabellarische Daten                   | `text/csv`, `application/parquet`            |
| `ARCHIVE`       | Archive, Container                    | `application/zip`, `application/tar+gzip`    |
| `BINARY`        | Unbekannte Binärdaten (Fallback)      | `application/octet-stream`                   |

### 1.2 ContentTypeRegistry

Die `ContentTypeRegistry` mappt MIME-Types auf Kategorien und speichert Metadaten über Fähigkeiten:

```cpp
struct ContentType {
    std::string mime_type;
    ContentCategory category;
    std::vector<std::string> extensions;  // z.B. {".txt", ".md"}
    bool supports_text_extraction;        // Kann Text extrahiert werden?
    bool supports_chunking;               // Kann in Chunks zerlegt werden?
    bool supports_embedding;              // Kann Embedding generiert werden?
    
    struct Features {
        bool hierarchical;   // Hat Hierarchie (CAD Assembly, Archive)
        bool spatial;        // Hat räumliche Koordinaten (Geo, GeoTIFF)
        bool temporal;       // Hat zeitliche Dimension (Audio, Video)
        bool multimodal;     // Mehrere Modalitäten (Video = Audio + Frames)
    };
    Features features;
};
```

**Beispiel-Registrierung:**

```cpp
ContentType text_plain;
text_plain.mime_type = "text/plain";
text_plain.category = ContentCategory::TEXT;
text_plain.extensions = {".txt", ".log", ".md"};
text_plain.supports_text_extraction = true;
text_plain.supports_chunking = true;
text_plain.supports_embedding = true;
text_plain.features.hierarchical = false;

ContentTypeRegistry::instance().registerType(text_plain);
```

## 2. Content-Prozessoren

### 2.1 IContentProcessor Interface

Jeder Prozessor implementiert 3 Kernmethoden:

```cpp
class IContentProcessor {
public:
    // 1. Extraktion: Blob → Strukturierte Daten
    virtual ExtractionResult extract(
        const std::string& blob,
        const ContentType& content_type
    ) = 0;
    
    // 2. Chunking: Extraktion → Viele kleine Chunks
    virtual std::vector<json> chunk(
        const ExtractionResult& extraction_result,
        int chunk_size,
        int overlap
    ) = 0;
    
    // 3. Embedding: Chunk → Vektorrepräsentation
    virtual std::vector<float> generateEmbedding(
        const std::string& chunk_data
    ) = 0;
};
```

**ExtractionResult-Struktur:**

```cpp
struct ExtractionResult {
    bool ok;
    std::string text;              // Extrahierter Plain-Text
    json metadata;                 // EXIF, ID3, CAD-Properties, etc.
    std::vector<float> embedding;  // Optional: Pre-computed Embedding
    std::string error_message;
    
    // Typ-spezifische Daten
    std::optional<GeoData> geo_data;      // Koordinaten, Projektion, Properties
    std::optional<MediaData> media_data;  // Duration, Width, Height, Codec
    std::optional<CADData> cad_data;      // Parts, BOM, Dimensionen
};
```

### 2.2 TextProcessor

**Verantwortlichkeiten:**

- Text-Normalisierung (Whitespace, Newlines)
- Sentenz-basiertes Chunking mit Overlap
- Embedding via Sentence-Transformers (768D)

**Chunking-Strategie:**

```cpp
std::vector<json> TextProcessor::chunk(
    const ExtractionResult& extraction,
    int chunk_size,      // z.B. 512 Tokens
    int overlap          // z.B. 50 Tokens
) {
    std::string text = normalizeText(extraction.text);
    std::vector<std::string> sentences = splitIntoSentences(text);
    
    std::vector<json> chunks;
    std::string current_chunk;
    int current_tokens = 0;
    
    for (const auto& sentence : sentences) {
        int tokens = countTokens(sentence);
        
        if (current_tokens + tokens > chunk_size && !current_chunk.empty()) {
            // Save chunk
            chunks.push_back({
                {"type", "text"},
                {"text", current_chunk},
                {"tokens", current_tokens}
            });
            
            // Overlap: Behalte letzte N Tokens
            current_chunk = getLastNTokens(current_chunk, overlap) + " " + sentence;
            current_tokens = overlap + tokens;
        } else {
            current_chunk += " " + sentence;
            current_tokens += tokens;
        }
    }
    
    return chunks;
}
```

**Embedding:**

```cpp
std::vector<float> TextProcessor::generateEmbedding(const std::string& chunk_data) {
    json chunk = json::parse(chunk_data);
    std::string text = chunk["text"];
    
    // Aufruf an externes Embedding-Service (z.B. FastAPI mit Sentence-Transformers)
    json request = {{"text", text}};
    auto response = http_client_->post("http://localhost:5000/embed", request);
    return response["embedding"].get<std::vector<float>>(); // 768D
}
```

### 2.3 ImageProcessor

**Verantwortlichkeiten:**

- EXIF-Metadaten extrahieren (GPS, DateTime, Camera Model)
- Bild-Dimensionen, Format, Kompression erkennen
- Embedding via CLIP (512D)

**Extraktion:**

```cpp
ExtractionResult ImageProcessor::extract(const std::string& blob, const ContentType& type) {
    ExtractionResult result;
    
    // EXIF-Tags parsen
    result.metadata = extractEXIF(blob);
    
    // Dimensionen
    auto [width, height] = getImageDimensions(blob);
    result.media_data = MediaData{
        .duration_seconds = 0,
        .width = width,
        .height = height,
        .codec = type.mime_type
    };
    
    // OCR (optional, falls Text im Bild)
    if (config.enable_ocr) {
        result.text = performOCR(blob);
    }
    
    result.ok = true;
    return result;
}
```

**Chunking:** Bilder werden i.d.R. als ein Chunk gespeichert. Bei großen Bildern könnte Tiling erfolgen:

```cpp
std::vector<json> ImageProcessor::chunk(const ExtractionResult& extraction, int chunk_size, int overlap) {
    // Für MVP: Ein Chunk pro Bild
    json chunk = {
        {"type", "image"},
        {"width", extraction.media_data->width},
        {"height", extraction.media_data->height},
        {"exif", extraction.metadata}
    };
    
    // Falls OCR-Text vorhanden, separater Text-Chunk
    std::vector<json> chunks = {chunk};
    if (!extraction.text.empty()) {
        chunks.push_back({
            {"type", "ocr_text"},
            {"text", extraction.text}
        });
    }
    
    return chunks;
}
```

**Embedding:**

```cpp
std::vector<float> ImageProcessor::generateEmbedding(const std::string& chunk_data) {
    // CLIP: Bild → 512D Vektor
    // blob muss als Base64 oder Dateipfad übermittelt werden
    json request = {{"image_base64", base64Encode(blob)}};
    auto response = http_client_->post("http://localhost:5000/embed/image", request);
    return response["embedding"].get<std::vector<float>>(); // 512D
}
```

### 2.4 GeoProcessor

**Verantwortlichkeiten:**

- GeoJSON, GPX, GeoTIFF parsen
- Koordinaten-Extraktion, Projektion (EPSG/SRID), Bounding Box
- Spatial Embeddings via Geo2Vec oder TileDB

**Extraktion:**

```cpp
ExtractionResult GeoProcessor::extract(const std::string& blob, const ContentType& type) {
    ExtractionResult result;
    
    if (type.mime_type == "application/geo+json") {
        result.geo_data = parseGeoJSON(blob);
    } else if (type.mime_type.find("gpx") != std::string::npos) {
        result.geo_data = parseGPX(blob);
    }
    
    // Metadata: SRID, Feature-Count, BBox
    result.metadata = {
        {"srid", result.geo_data->projection},
        {"feature_count", result.geo_data->coordinates.size()},
        {"bbox", computeBBox(result.geo_data->coordinates)}
    };
    
    result.ok = true;
    return result;
}
```

**Chunking:** Ein Chunk pro Feature (bei FeatureCollection):

```cpp
std::vector<json> GeoProcessor::chunk(const ExtractionResult& extraction, int chunk_size, int overlap) {
    std::vector<json> chunks;
    
    for (size_t i = 0; i < extraction.geo_data->coordinates.size(); ++i) {
        auto [lat, lon] = extraction.geo_data->coordinates[i];
        json properties = extraction.geo_data->properties[i]; // Annahme: properties pro Feature
        
        chunks.push_back({
            {"type", "geo_feature"},
            {"geometry", {{"type", "Point"}, {"coordinates", {lon, lat}}}},
            {"srid", extraction.geo_data->projection},
            {"properties", properties}
        });
    }
    
    return chunks;
}
```

**Embedding:**

```cpp
std::vector<float> GeoProcessor::generateEmbedding(const std::string& chunk_data) {
    json chunk = json::parse(chunk_data);
    auto coords = chunk["geometry"]["coordinates"];
    double lon = coords[0], lat = coords[1];
    
    // Geo2Vec: [lon, lat] → 128D Vektor
    json request = {{"lon", lon}, {"lat", lat}};
    auto response = http_client_->post("http://localhost:5000/embed/geo", request);
    return response["embedding"].get<std::vector<float>>();
}
```

### 2.5 CADProcessor

**Verantwortlichkeiten:**

- STEP, IGES, STL, DXF parsen
- Assembly-Hierarchie extrahieren (Parent-Child-Beziehungen)
- Bill of Materials (BOM), Dimensionen, Constraints

**Extraktion:**

```cpp
ExtractionResult CADProcessor::extract(const std::string& blob, const ContentType& type) {
    ExtractionResult result;
    
    if (type.mime_type == "model/step") {
        json step_data = parseSTEP(blob);
        result.cad_data = CADData{
            .part_ids = extractPartIDs(step_data),
            .bom = extractBOM(step_data),
            .dimensions = extractDimensions(step_data)
        };
    }
    
    result.metadata = {
        {"format", "STEP"},
        {"part_count", result.cad_data->part_ids.size()}
    };
    
    result.ok = true;
    return result;
}
```

**Chunking:** Ein Chunk pro Part (Hierarchie via Edges):

```cpp
std::vector<json> CADProcessor::chunk(const ExtractionResult& extraction, int chunk_size, int overlap) {
    std::vector<json> chunks;
    
    for (const auto& part_id : extraction.cad_data->part_ids) {
        json part_info = extraction.cad_data->bom[part_id];
        
        chunks.push_back({
            {"type", "cad_part"},
            {"part_id", part_id},
            {"name", part_info["name"]},
            {"material", part_info["material"]},
            {"dimensions", part_info["dimensions"]}
        });
    }
    
    return chunks;
}
```

**Graph-Edges:** `contains` (Assembly → Part), `sibling` (Parts im gleichen Assembly)

### 2.6 AudioProcessor

**Verantwortlichkeiten:**

- ID3-Tags extrahieren (Titel, Artist, Album)
- Duration, Bitrate, Codec erkennen
- Speech-to-Text (optional)
- Embedding via Wav2Vec2 (768D)

**Chunking:** Zeit-basiert (z.B. 10-Sekunden-Segmente):

```cpp
std::vector<json> AudioProcessor::chunk(const ExtractionResult& extraction, int chunk_size, int overlap) {
    int duration = extraction.media_data->duration_seconds;
    std::vector<json> chunks;
    
    for (int i = 0; i < duration; i += chunk_size) {
        int end_time = std::min(i + chunk_size, duration);
        
        chunks.push_back({
            {"type", "audio_segment"},
            {"start_time", i},
            {"end_time", end_time},
            {"transcript", extractTranscript(i, end_time)} // Optional: Whisper ASR
        });
    }
    
    return chunks;
}
```

### 2.7 StructuredProcessor

**Verantwortlichkeiten:**

- CSV, Parquet, Arrow parsen
- Schema-Inferenz (Spaltentypen)
- Row-Level Chunking
- Spalten-Embeddings (für Table-QA)

**Chunking:**

```cpp
std::vector<json> StructuredProcessor::chunk(const ExtractionResult& extraction, int chunk_size, int overlap) {
    auto rows = parseCSV(extraction.text);
    json schema = extractSchema(rows);
    
    std::vector<json> chunks;
    
    // Header als separater Chunk
    chunks.push_back({
        {"type", "table_schema"},
        {"schema", schema}
    });
    
    // Rows in Batches
    for (size_t i = 1; i < rows.size(); i += chunk_size) {
        json row_batch = json::array();
        for (size_t j = i; j < std::min(i + chunk_size, rows.size()); ++j) {
            row_batch.push_back(rows[j]);
        }
        
        chunks.push_back({
            {"type", "table_rows"},
            {"rows", row_batch},
            {"row_range", {i, std::min(i + chunk_size, rows.size())}}
        });
    }
    
    return chunks;
}
```

### 2.8 BinaryProcessor (Fallback)

Falls kein spezialisierter Prozessor vorhanden, nutzt `BinaryProcessor` nur Hash + Größe:

```cpp
ExtractionResult BinaryProcessor::extract(const std::string& blob, const ContentType& type) {
    ExtractionResult result;
    result.metadata = {
        {"size_bytes", blob.size()},
        {"hash_sha256", computeSHA256(blob)}
    };
    result.ok = true;
    return result;
}
```

## 3. Import-Pipeline (POST /content/import)

Ab **MVP 2.0** wird die Ingestion **client-seitig** durchgeführt. Der Server erwartet vorverarbeitete JSON-Objekte mit einem kanonischen Schema.

### 3.1 Import-Schema

```json
{
  "content": {
    "id": "content-uuid",
    "mime_type": "text/plain",
    "category": "TEXT",
    "original_filename": "doc.txt",
    "size_bytes": 1024,
    "hash_sha256": "abc123...",
    "created_at": 1672531200,
    "user_metadata": {"author": "John Doe"},
    "tags": ["documentation", "v1.0"]
  },
  "chunks": [
    {
      "id": "chunk-uuid-1",
      "content_id": "content-uuid",
      "seq_num": 0,
      "chunk_type": "text",
      "text": "This is the first chunk...",
      "start_offset": 0,
      "end_offset": 512,
      "embedding": [0.12, -0.45, ...]  // 768D
    },
    {
      "id": "chunk-uuid-2",
      "content_id": "content-uuid",
      "seq_num": 1,
      "chunk_type": "text",
      "text": "This is the second chunk...",
      "start_offset": 462,
      "end_offset": 974,
      "embedding": [0.34, 0.67, ...]
    }
  ],
  "edges": [
    {
      "from": "chunk-uuid-1",
      "to": "chunk-uuid-2",
      "type": "next",
      "weight": 1.0
    },
    {
      "from": "chunk-uuid-2",
      "to": "chunk-uuid-1",
      "type": "prev",
      "weight": 1.0
    }
  ],
  "blob": "<base64-encoded-binary>"  // Optional: Original-Blob
}
```

### 3.2 Server-seitige Import-Verarbeitung

```cpp
Status ContentManager::importContent(const json& spec, const std::optional<std::string>& blob) {
    // 1. Validierung
    if (!spec.contains("content") || !spec.contains("chunks")) {
        return Status::Error("Invalid schema: missing 'content' or 'chunks'");
    }
    
    // 2. Content-Metadata speichern
    ContentMeta meta = ContentMeta::fromJson(spec["content"]);
    BaseEntity content_entity(meta.id, "content");
    content_entity.set("mime_type", meta.mime_type);
    content_entity.set("category", static_cast<int>(meta.category));
    content_entity.set("original_filename", meta.original_filename);
    // ... (weitere Felder)
    storage_->put(content_entity);
    
    // 3. Blob speichern (falls vorhanden)
    if (blob.has_value()) {
        BaseEntity blob_entity(meta.id, "content_blob");
        blob_entity.setBlob(*blob);
        storage_->put(blob_entity);
    }
    
    // 4. Chunks speichern + VectorIndex aktualisieren
    for (const auto& chunk_json : spec["chunks"]) {
        ChunkMeta chunk = ChunkMeta::fromJson(chunk_json);
        
        BaseEntity chunk_entity(chunk.id, "chunk");
        chunk_entity.set("content_id", chunk.content_id);
        chunk_entity.set("seq_num", chunk.seq_num);
        chunk_entity.set("chunk_type", chunk.chunk_type);
        chunk_entity.set("text", chunk.text);
        storage_->put(chunk_entity);
        
        // VectorIndex: Embedding einfügen
        if (!chunk.embedding.empty()) {
            vector_index_->addVector(chunk.id, chunk.embedding);
        }
    }
    
    // 5. Graph-Edges speichern
    if (spec.contains("edges")) {
        for (const auto& edge : spec["edges"]) {
            graph_index_->addEdge(
                edge["from"],
                edge["to"],
                edge["type"],
                edge.value("weight", 1.0)
            );
        }
    }
    
    return Status::OK();
}
```

### 3.3 HTTP-Endpoint

```cpp
// In main_server.cpp
app.post("/content/import", [&](const Request& req, Response& res) {
    json spec = json::parse(req.body);
    
    std::optional<std::string> blob;
    if (spec.contains("blob")) {
        blob = base64Decode(spec["blob"].get<std::string>());
    }
    
    auto status = content_manager->importContent(spec, blob);
    
    if (status.ok) {
        res.status = 200;
        res.set_content(json{{"ok", true}, {"content_id", spec["content"]["id"]}}.dump(), "application/json");
    } else {
        res.status = 400;
        res.set_content(json{{"error", status.message}}.dump(), "application/json");
    }
});
```

## 4. Batching & Bulk-Import

### 4.1 Motivation

Beim Import vieler Dokumente (z.B. 10.000 PDFs) ist sequentielles Einfügen ineffizient. **Batching** gruppiert mehrere Operationen:

- **Embedding-Batch-API:** Generierung von 100 Embeddings in einem Request
- **RocksDB WriteBatch:** Atomare Transaktionen für mehrere Entities
- **VectorIndex Bulk-Insert:** HNSW-Build via Batch-Add

### 4.2 Batch-Import-Schema

```json
{
  "batch": [
    {
      "content": {...},
      "chunks": [...],
      "edges": [...],
      "blob": "..."
    },
    {
      "content": {...},
      "chunks": [...],
      "edges": [...],
      "blob": "..."
    }
  ]
}
```

### 4.3 Server-Implementierung

```cpp
Status ContentManager::importBatch(const json& batch_spec) {
    rocksdb::WriteBatch batch;
    std::vector<std::pair<std::string, std::vector<float>>> embeddings_to_add;
    
    for (const auto& item : batch_spec["batch"]) {
        // Content + Chunks in WriteBatch speichern
        ContentMeta meta = ContentMeta::fromJson(item["content"]);
        batch.Put("content:" + meta.id, meta.toJson().dump());
        
        for (const auto& chunk_json : item["chunks"]) {
            ChunkMeta chunk = ChunkMeta::fromJson(chunk_json);
            batch.Put("chunk:" + chunk.id, chunk.toJson().dump());
            
            if (!chunk.embedding.empty()) {
                embeddings_to_add.push_back({chunk.id, chunk.embedding});
            }
        }
    }
    
    // Atomare DB-Write
    auto db_status = storage_->getRaw()->Write(rocksdb::WriteOptions(), &batch);
    if (!db_status.ok()) {
        return Status::Error("Batch write failed: " + db_status.ToString());
    }
    
    // VectorIndex: Bulk-Add
    vector_index_->addVectorsBatch(embeddings_to_add);
    
    return Status::OK();
}
```

## 5. Error Handling

### 5.1 Fehler-Kategorien

| Fehler                     | HTTP-Code | Ursache                                   | Recovery                            |
|----------------------------|-----------|-------------------------------------------|-------------------------------------|
| `Invalid Schema`           | 400       | Fehlende Pflichtfelder (`content`, `chunks`) | Client: Schema validieren          |
| `Duplicate Content`        | 409       | `hash_sha256` bereits vorhanden          | Client: Dedup-Check vor Upload     |
| `Embedding Dimension Mismatch` | 400  | Embedding hat falsche Dimension          | Client: Modell-Version prüfen      |
| `Storage Error`            | 500       | RocksDB Write fehlgeschlagen             | Server: Retry mit Backoff          |
| `VectorIndex Error`        | 500       | HNSW-Build fehlgeschlagen                | Server: Rebuild Index              |

### 5.2 Status-Objekt

Alle ContentManager-Methoden geben `Status` zurück:

```cpp
struct Status {
    bool ok = true;
    std::string message;
    
    static Status OK() { return {}; }
    static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
};
```

**Beispiel-Nutzung:**

```cpp
auto status = content_manager->importContent(spec, blob);
if (!status.ok) {
    THEMIS_ERROR("Import failed: {}", status.message);
    // Logging, Retry-Logik, Client-Notification
}
```

### 5.3 Deduplication

Hash-basierte Dedup verhindert doppelte Speicherung:

```cpp
Status ContentManager::importContent(const json& spec, const std::optional<std::string>& blob) {
    ContentMeta meta = ContentMeta::fromJson(spec["content"]);
    
    // Dedup-Check via SecondaryIndex (hash_sha256)
    auto existing = secondary_index_->get("hash_sha256", meta.hash_sha256);
    if (existing.has_value()) {
        return Status{
            .ok = true,
            .message = "Duplicate content (hash: " + meta.hash_sha256 + "), skipping import"
        };
    }
    
    // ... normal import
}
```

## 6. Graph-Konstruktion

### 6.1 Edge-Typen

THEMIS konstruiert automatisch Edges für verschiedene Beziehungen:

| Edge-Typ          | Bedeutung                              | Beispiel                                      |
|-------------------|----------------------------------------|-----------------------------------------------|
| `parent`          | Chunk gehört zu Content                | `chunk:uuid` → `content:uuid`                 |
| `next`            | Sequentieller Nachfolger               | `chunk:1` → `chunk:2`                         |
| `prev`            | Sequentieller Vorgänger                | `chunk:2` → `chunk:1`                         |
| `contains`        | Hierarchie (Assembly → Part)           | `content:assembly` → `content:part`           |
| `sibling`         | Gleiche Hierarchie-Ebene               | `chunk:part1` → `chunk:part2`                 |
| `member_of`       | Geodaten: Feature gehört zu Region     | `chunk:poi` → `chunk:region`                  |
| `spatially_near`  | Geodaten: Räumliche Nachbarschaft      | `chunk:poi1` → `chunk:poi2` (z.B. < 1 km)     |

### 6.2 Automatische Edge-Generierung

**Text-Chunks:**

```cpp
// parent + next/prev Edges
for (size_t i = 0; i < chunks.size(); ++i) {
    graph_index_->addEdge(chunks[i].id, content_id, "parent", 1.0);
    
    if (i > 0) {
        graph_index_->addEdge(chunks[i].id, chunks[i-1].id, "prev", 1.0);
    }
    if (i < chunks.size() - 1) {
        graph_index_->addEdge(chunks[i].id, chunks[i+1].id, "next", 1.0);
    }
}
```

**CAD-Assembly:**

```cpp
// contains + sibling Edges
for (const auto& part_id : assembly_parts) {
    graph_index_->addEdge(assembly_id, part_id, "contains", 1.0);
    
    for (const auto& sibling_id : assembly_parts) {
        if (part_id != sibling_id) {
            graph_index_->addEdge(part_id, sibling_id, "sibling", 0.5);
        }
    }
}
```

**Geodaten:**

```cpp
// member_of + spatially_near Edges
for (const auto& poi : pois) {
    std::string region = findRegion(poi.lat, poi.lon); // Spatial Join
    graph_index_->addEdge(poi.id, region, "member_of", 1.0);
    
    auto nearby = findNearbyPOIs(poi.lat, poi.lon, 1000.0); // 1 km Radius
    for (const auto& neighbor : nearby) {
        double distance = haversineDistance(poi, neighbor);
        double weight = 1.0 / (1.0 + distance); // Näher = höheres Gewicht
        graph_index_->addEdge(poi.id, neighbor.id, "spatially_near", weight);
    }
}
```

### 6.3 Graph-Expansion (RAG)

Suche mit Kontext-Expansion:

```cpp
std::vector<std::pair<std::string, float>> ContentManager::searchWithExpansion(
    const std::string& query_text,
    int k,
    int expansion_hops
) {
    // 1. Vektor-Suche: Top-K Chunks
    auto embedding = generateQueryEmbedding(query_text);
    auto top_k_chunks = vector_index_->search(embedding, k);
    
    // 2. Graph-Expansion: Nachbarn via BFS
    std::set<std::string> expanded_chunks;
    for (const auto& [chunk_id, score] : top_k_chunks) {
        expanded_chunks.insert(chunk_id);
        
        // BFS: expansion_hops Schritte
        auto neighbors = graph_index_->bfs(chunk_id, expansion_hops, {"next", "prev", "parent", "sibling"});
        for (const auto& neighbor : neighbors) {
            expanded_chunks.insert(neighbor);
        }
    }
    
    // 3. Ergebnisse ranken (Original-Score beibehalten)
    std::vector<std::pair<std::string, float>> results;
    for (const auto& chunk_id : expanded_chunks) {
        auto it = std::find_if(top_k_chunks.begin(), top_k_chunks.end(),
            [&](const auto& p) { return p.first == chunk_id; });
        
        float score = (it != top_k_chunks.end()) ? it->second : 0.5; // Nachbarn mit reduziertem Score
        results.push_back({chunk_id, score});
    }
    
    // 4. Nach Score sortieren
    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    return results;
}
```

## 7. Embedding-Strategien

### 7.1 Embedding-Modelle pro Modalität

| Modalität     | Modell                        | Dimension | Beschreibung                                 |
|---------------|-------------------------------|-----------|----------------------------------------------|
| Text          | Sentence-Transformers (`all-MiniLM-L6-v2`) | 768D | Semantische Textsuche                       |
| Bilder        | CLIP (`openai/clip-vit-base-patch32`)     | 512D | Multi-modale (Bild + Text) Embeddings       |
| Geodaten      | Geo2Vec (Custom)              | 128D      | [lon, lat] → Vektor                         |
| CAD           | PartNet / GraphSAINT (Custom) | 256D      | Geometry + BOM → Vektor                     |
| Audio         | Wav2Vec2 (`facebook/wav2vec2-base`) | 768D | Akustische Features                         |
| Tabellen      | TaBERT / TAPAS (Custom)       | 768D      | Schema + Rows → Vektor                      |

### 7.2 Externe Embedding-Services

Embeddings werden via HTTP-APIs generiert:

**Text-Embedding:**

```http
POST http://localhost:5000/embed
Content-Type: application/json

{
  "text": "This is a sample document."
}

Response:
{
  "embedding": [0.123, -0.456, ..., 0.789],  // 768D
  "model": "all-MiniLM-L6-v2"
}
```

**Batch-Embedding:**

```http
POST http://localhost:5000/embed/batch
Content-Type: application/json

{
  "texts": ["Doc 1", "Doc 2", ..., "Doc 100"]
}

Response:
{
  "embeddings": [
    [0.1, 0.2, ...],
    [0.3, 0.4, ...],
    ...
  ]
}
```

### 7.3 Hybrid-Search

Kombination von Vektor- und Attributsuche:

```cpp
std::vector<std::pair<std::string, float>> ContentManager::hybridSearch(
    const std::string& query_text,
    const json& filters,
    int k
) {
    // 1. Vektor-Suche
    auto embedding = generateQueryEmbedding(query_text);
    auto vector_results = vector_index_->search(embedding, k * 2); // Oversampling
    
    // 2. Filter auf Metadaten (SecondaryIndex)
    std::vector<std::pair<std::string, float>> filtered_results;
    for (const auto& [chunk_id, score] : vector_results) {
        auto chunk = getChunk(chunk_id);
        if (!chunk.has_value()) continue;
        
        // Filter anwenden
        bool matches = true;
        if (filters.contains("category")) {
            auto content = getContentMeta(chunk->content_id);
            if (content->category != filters["category"].get<ContentCategory>()) {
                matches = false;
            }
        }
        if (filters.contains("tags")) {
            auto content = getContentMeta(chunk->content_id);
            auto required_tags = filters["tags"].get<std::vector<std::string>>();
            for (const auto& tag : required_tags) {
                if (std::find(content->tags.begin(), content->tags.end(), tag) == content->tags.end()) {
                    matches = false;
                    break;
                }
            }
        }
        
        if (matches) {
            filtered_results.push_back({chunk_id, score});
        }
    }
    
    // 3. Top-K zurückgeben
    filtered_results.resize(std::min(k, static_cast<int>(filtered_results.size())));
    return filtered_results;
}
```

## 8. HTTP API

### 8.1 Endpoints

| Endpoint                  | Methode | Beschreibung                                    |
|---------------------------|---------|-------------------------------------------------|
| `/content/import`         | POST    | Import vorverarbeiteter Inhalte (Schema oben)   |
| `/content/:id`            | GET     | Content-Metadaten abrufen                       |
| `/content/:id/blob`       | GET     | Original-Blob herunterladen                     |
| `/content/:id/chunks`     | GET     | Alle Chunks eines Contents (sortiert)          |
| `/content/search`         | POST    | Semantische Suche (Query-Text + Filters)        |
| `/content/:id`            | DELETE  | Content + Chunks + Edges löschen                |

### 8.2 Beispiele

**Import:**

```http
POST /content/import
Content-Type: application/json

{
  "content": {
    "id": "doc-123",
    "mime_type": "text/plain",
    "category": "TEXT",
    "original_filename": "readme.txt",
    "size_bytes": 512,
    "hash_sha256": "abc...",
    "created_at": 1672531200,
    "tags": ["docs"]
  },
  "chunks": [
    {
      "id": "chunk-1",
      "content_id": "doc-123",
      "seq_num": 0,
      "chunk_type": "text",
      "text": "Introduction...",
      "embedding": [0.1, 0.2, ...]
    }
  ],
  "edges": [
    {
      "from": "chunk-1",
      "to": "doc-123",
      "type": "parent"
    }
  ]
}

Response:
{
  "ok": true,
  "content_id": "doc-123",
  "chunks_created": 1,
  "edges_created": 1
}
```

**Suche:**

```http
POST /content/search
Content-Type: application/json

{
  "query": "machine learning tutorial",
  "k": 10,
  "filters": {
    "category": "TEXT",
    "tags": ["ai"]
  },
  "expansion_hops": 1
}

Response:
{
  "results": [
    {
      "chunk_id": "chunk-42",
      "content_id": "doc-ml-intro",
      "score": 0.87,
      "text": "Introduction to machine learning...",
      "metadata": {"filename": "ml_intro.md"}
    },
    ...
  ]
}
```

## 9. Best Practices

### 9.1 Chunking-Richtlinien

- **Text:** 200–400 Tokens (besserer Retrieval/Ranking-Tradeoff), 50 Tokens Overlap
- **Audio/Video:** 2–10 Sekunden pro Segment (annehmbare Granularität)
- **Tabellen:** 100–500 Rows pro Batch (Balance zwischen Kontext und Performance)
- **Geodaten:** Ein Chunk pro Feature (GeoJSON), Spatial Aggregation bei Millionen Features

### 9.2 Embedding-Optimierung

- **Batch-Processing:** 100–500 Embeddings pro Request (10x schneller als einzeln)
- **GPU-Beschleunigung:** Sentence-Transformers mit CUDA (5–10x Speedup)
- **Embedding-Cache:** Hash → Embedding speichern (Dedup vermeidet Re-Computation)

### 9.3 Storage-Optimierung

- **Inline vs. External Blobs:**
  - < 1 MB: RocksDB (schneller Zugriff)
  - \> 1 MB: Filesystem oder S3 (Blob-Ref in RocksDB)
- **Kompression:** Zstd für große Blobs (3:1 Ratio bei Text, 1.5:1 bei Binär)
- **TTL:** Alte Inhalte automatisch löschen (siehe `docs/base_entity.md`)

### 9.4 Modalitäts-spezifische Guidelines

**Bilder:**

- EXIF-Daten extrahieren (GPS, DateTime, Camera Model)
- OCR nur bei Bedarf (teuer!)
- Object Detection optional (YOLO, Detectron2)
- Captioning mit BLIP/GIT für Textsuche

**Geodaten:**

- Normalisierung auf WGS84 (EPSG:4326) vor Import
- Bounding Box pre-compute für schnelle Spatial Queries
- Spatial Joins für `member_of` Edges (Punkt-in-Polygon-Tests)

**CAD:**

- BOM-Extraktion via Open CASCADE oder STEP-Parser
- Geometrie-Hashing für Duplikaterkennung (Topology-Hash)
- Assembly-Hierarchie als `contains` Edges abbilden

**Tabellen:**

- Schema-Inferenz (Datentypen, Primärschlüssel)
- Foreign-Key-Erkennung → `references` Edges
- Spalten-Normalisierung (z.B. Dates → ISO8601)

## 10. Performance & Benchmarks

### 10.1 Ingestion-Throughput

**Baseline (Sequentiell):**

- Text (1 MB): ~2 s (Tokenization + Embedding)
- Image (5 MB): ~5 s (EXIF + CLIP-Embedding)
- Geodaten (10k Features): ~10 s (Parsing + Spatial Embeddings)

**Mit Batching:**

- Text Batch (100 Docs): ~10 s (10x Speedup via Batch-Embedding)
- Image Batch (50 Images): ~15 s (3x Speedup via GPU-Batch)

### 10.2 Search-Latenz

- **Vektor-Suche (HNSW):** 10–50 ms (1M Vektoren, k=10)
- **Hybrid-Search (Vektor + Filter):** 50–100 ms (Filter auf 100k Entities)
- **Graph-Expansion (1 Hop):** +20 ms (BFS auf 100k Edges)

### 10.3 Storage-Größe

- **RocksDB Overhead:** ~30% (Metadata, Indexes)
- **VectorIndex (HNSW):** ~1.5x der Embedding-Größe (Links + Metadata)
- **GraphIndex:** ~40 Bytes pro Edge (From + To + Type + Weight)

**Beispiel:** 1 Million Text-Chunks (768D Embeddings)

- RocksDB (Metadata): ~500 MB
- VectorIndex (Embeddings): 1M * 768 * 4 Bytes * 1.5 = ~4.5 GB
- GraphIndex (2 Edges/Chunk): 2M * 40 Bytes = ~80 MB
- **Total:** ~5 GB

## 11. Testing

### 11.1 Unit-Tests (pro Processor)

```cpp
TEST(TextProcessorTest, ExtractsTextFromPlainText) {
    TextProcessor processor;
    std::string blob = "Hello, world!";
    ContentType type = {.mime_type="text/plain", .category=ContentCategory::TEXT};
    
    auto result = processor.extract(blob, type);
    
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.text, "Hello, world!");
}

TEST(ImageProcessorTest, ExtractsEXIF) {
    ImageProcessor processor;
    std::string blob = loadTestImage("test_photo.jpg");
    ContentType type = {.mime_type="image/jpeg", .category=ContentCategory::IMAGE};
    
    auto result = processor.extract(blob, type);
    
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.metadata.contains("exif"));
    EXPECT_EQ(result.metadata["exif"]["Make"], "Canon");
}
```

### 11.2 Integration-Tests

```cpp
TEST(ContentManagerTest, ImportTextDocumentEndToEnd) {
    auto storage = std::make_shared<RocksDBWrapper>("./test_db");
    auto vector_index = std::make_shared<VectorIndexManager>(storage, 768);
    auto graph_index = std::make_shared<GraphIndexManager>(storage);
    auto secondary_index = std::make_shared<SecondaryIndexManager>(storage);
    
    ContentManager manager(storage, vector_index, graph_index, secondary_index);
    
    json spec = {
        {"content", {
            {"id", "test-doc"},
            {"mime_type", "text/plain"},
            {"category", "TEXT"},
            {"original_filename", "test.txt"},
            {"size_bytes", 100},
            {"hash_sha256", "hash123"},
            {"created_at", 1672531200}
        }},
        {"chunks", json::array({
            {{"id", "chunk-1"}, {"content_id", "test-doc"}, {"seq_num", 0}, {"chunk_type", "text"}, {"text", "Hello"}, {"embedding", std::vector<float>(768, 0.1)}}
        })},
        {"edges", json::array({
            {{"from", "chunk-1"}, {"to", "test-doc"}, {"type", "parent"}}
        })}
    };
    
    auto status = manager.importContent(spec, std::nullopt);
    
    ASSERT_TRUE(status.ok);
    
    // Verify metadata stored
    auto meta = manager.getContentMeta("test-doc");
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->mime_type, "text/plain");
    
    // Verify chunks stored
    auto chunks = manager.getContentChunks("test-doc");
    EXPECT_EQ(chunks.size(), 1);
    
    // Verify graph edges
    auto neighbors = graph_index->getOutNeighbors("chunk-1");
    EXPECT_EQ(neighbors.size(), 1); // Has 'parent' edge
}
```

### 11.3 Performance-Benchmarks

```cpp
static void BM_ImportLargeDocument(benchmark::State& state) {
    ContentManager manager(/* ... */);
    
    std::string large_text(10 * 1024 * 1024, 'A'); // 10 MB
    json spec = createTestSpec(large_text, 512, 50); // 512 Tokens/Chunk, 50 Overlap
    
    for (auto _ : state) {
        manager.importContent(spec, std::nullopt);
    }
}
BENCHMARK(BM_ImportLargeDocument);

static void BM_HybridSearch(benchmark::State& state) {
    ContentManager manager(/* ... */);
    // ... populate with 100k documents
    
    for (auto _ : state) {
        manager.hybridSearch("machine learning", {{"category", "TEXT"}}, 10);
    }
}
BENCHMARK(BM_HybridSearch);
```

## 12. Roadmap

### MVP 1.0 (Aktuell)

- [x] ContentTypeRegistry + ContentCategory
- [x] TextProcessor (Extraction, Chunking, Embedding via Mock)
- [x] ContentManager::importContent() (kanonisches Schema)
- [x] HTTP Endpoint: POST /content/import
- [x] BasicTests (Unit + Integration)

### MVP 2.0 (Geplant)

- [ ] ImageProcessor (EXIF + CLIP-Embedding via external service)
- [ ] GeoProcessor (GeoJSON + Geo2Vec)
- [ ] CADProcessor (STEP + PartNet-Embedding)
- [ ] Batch-Import (POST /content/import/batch)
- [ ] Hybrid-Search (Vektor + Filter)
- [ ] Graph-Expansion (BFS mit `next`, `prev`, `contains`)

### Future (Post-MVP)

- [ ] AudioProcessor (ID3 + Wav2Vec2 + Whisper ASR)
- [ ] StructuredProcessor (CSV/Parquet + TaBERT)
- [ ] Async-Ingestion (Job-Queue mit Status-Tracking)
- [ ] External Blob-Storage (S3-Anbindung)
- [ ] Multi-Hop Graph-Reasoning (z.B. "Show CAD parts from same supplier")
- [ ] Federated Search (Multi-Tenant mit Access Control)

## 13. Fazit

Die **Content Pipeline** bietet eine **skalierbare, erweiterbare Architektur** für heterogene Datentypen. Durch die Trennung von generischen Operationen (Hashing, Graph-Erstellung) und typ-spezifischer Verarbeitung (via Processors) bleibt das System wartbar und einfach erweiterbar.

**Key Benefits:**

- **Einheitliche API:** Ein Import-Endpoint für alle Datentypen
- **Wiederverwendbare Komponenten:** Chunking, Graph-Erstellung, Deduplication
- **Typ-Sicherheit:** ContentTypeRegistry verhindert falsche Verarbeitung
- **Produktivität:** Neue Datentypen in < 1 Tag integrierbar (nur Processor implementieren)
- **RAG-Ready:** Graph-Expansion für kontextuelle Suche out-of-the-box
- **Performance:** Batching, GPU-Embeddings, HNSW-Index für Millionen Vektoren

**Weiterführende Dokumentation:**

- [Content Architecture](content_architecture.md) - Design-Details der Prozessor-Architektur
- [Ingestion Guidelines](content/ingestion.md) - Modalitäts-spezifische Vorverarbeitungs-Empfehlungen
- [Vector Index](indexes.md#vector-index) - HNSW-Algorithmus und Tuning-Parameter
- [Graph Index](indexes.md#graph-index) - BFS/DFS-Traversierung und Edge-Typen
- [AQL Syntax](aql_syntax.md) - Hybrid-Queries mit `VECTOR_KNN()` und `GRAPH_EXPAND()`
