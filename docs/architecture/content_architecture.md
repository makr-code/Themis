# Content Manager Architektur

**Version:** 1.0  
**Datum:** 28. Oktober 2025  
**Status:** Design Phase

## 1. Überblick

Das Content Manager System ist eine **universelle Schicht** für die Verwaltung heterogener Datentypen in THEMIS. Es abstrahiert die Komplexität der Verarbeitung verschiedener Content-Typen (Text, Bilder, Audio, Geo-Daten, CAD-Modelle, etc.) hinter einer einheitlichen API.

### 1.1 Ziele

- **Erweiterbarkeit:** Neue Datentypen können über Plugins hinzugefügt werden
- **Wiederverwendbarkeit:** Gemeinsame Operationen (Hashing, Chunking, Graph-Erstellung) nur einmal implementiert
- **Typsicherheit:** Klare Trennung zwischen generischen und typspezifischen Operationen
- **Produktivität:** Entwickler müssen nicht für jeden Datentyp eine vollständige Pipeline implementieren

### 1.2 Architektur-Prinzipien

```
┌─────────────────────────────────────────────────────────────────┐
│                    HTTP API Layer                               │
│  POST /content/upload, GET /content/:id, POST /content/search   │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                    ContentManager                               │
│  • Unified ingestion pipeline                                   │
│  • Processor routing by category                                │
│  • Graph construction (parent, next/prev, hierarchical)         │
│  • Deduplication (SHA-256 hash)                                 │
└──────┬────────────┬────────────┬────────────┬────────────┬──────┘
       │            │            │            │            │
┌──────▼────┐  ┌───▼──────┐ ┌──▼──────┐ ┌───▼──────┐ ┌──▼──────┐
│   Text    │  │  Image   │ │   Geo   │ │   CAD    │ │  Audio  │
│ Processor │  │Processor │ │Processor│ │Processor │ │Processor│
└──────┬────┘  └───┬──────┘ └──┬──────┘ └───┬──────┘ └──┬──────┘
       │            │            │            │            │
       │  • extract()  • chunk()  • generateEmbedding()   │
       │                                                   │
┌──────▼───────────────────────────────────────────────────▼──────┐
│                    Storage Layer                                │
│  • RocksDB (metadata + blobs)                                   │
│  • VectorIndex (embeddings)                                     │
│  • GraphIndex (parent, next/prev, assembly hierarchy)           │
│  • SecondaryIndex (tags, mime_type, hash for dedup)             │
└─────────────────────────────────────────────────────────────────┘
```

## 2. Core Components

### 2.1 ContentTypeRegistry

**Verantwortlichkeit:** MIME-Type → Category Mapping

**Funktionen:**
- MIME-Type-Erkennung (manuelle Angabe, Magic Bytes, Dateiendung)
- Kategorisierung (TEXT, IMAGE, AUDIO, VIDEO, GEO, CAD, STRUCTURED, BINARY)
- Feature Flags (supports_text_extraction, supports_embedding, geospatial, hierarchical)

**Beispiel:**
```cpp
ContentType pdf_type;
pdf_type.mime_type = "application/pdf";
pdf_type.category = ContentCategory::TEXT;
pdf_type.extensions = {".pdf"};
pdf_type.supports_text_extraction = true;
pdf_type.supports_chunking = true;
pdf_type.binary_storage_required = true;

ContentTypeRegistry::instance().registerType(pdf_type);
```

**Default Types (Pre-Registered):**

| Category    | MIME Types                                                                 | Features                                      |
|-------------|---------------------------------------------------------------------------|-----------------------------------------------|
| TEXT        | `text/plain`, `text/markdown`, `text/html`, `application/json`, `text/x-python` | text_extraction, chunking, embedding          |
| IMAGE       | `image/jpeg`, `image/png`, `image/svg+xml`, `image/tiff`                  | metadata_extraction (EXIF), embedding (CLIP)  |
| AUDIO       | `audio/mpeg`, `audio/wav`, `audio/flac`                                   | metadata_extraction (ID3), temporal           |
| VIDEO       | `video/mp4`, `video/webm`                                                 | metadata_extraction, temporal, multimodal     |
| GEO         | `application/geo+json`, `application/gpx+xml`, `image/tiff` (GeoTIFF)     | geospatial, metadata_extraction               |
| CAD         | `model/step`, `model/iges`, `model/stl`, `application/dxf`                | hierarchical, metadata_extraction             |
| STRUCTURED  | `text/csv`, `application/vnd.apache.parquet`, `application/vnd.apache.arrow` | text_extraction, chunking (row-level)         |
| ARCHIVE     | `application/zip`, `application/x-tar`                                    | hierarchical (extract members recursively)    |
| BINARY      | Fallback für unbekannte Typen                                             | binary_storage_required                       |

### 2.2 IContentProcessor (Plugin Interface)

**Verantwortlichkeit:** Typ-spezifische Verarbeitung

**Kernmethoden:**
```cpp
class IContentProcessor {
public:
    // Extrahiere strukturierte Daten aus Blob
    virtual ExtractionResult extract(
        const std::string& blob,
        const ContentType& content_type
    ) = 0;
    
    // Chunking (z.B. Text → Paragraphen, CAD → Parts, CSV → Rows)
    virtual std::vector<json> chunk(
        const ExtractionResult& extraction_result,
        int chunk_size,
        int overlap
    ) = 0;
    
    // Embedding-Generierung (z.B. Text → Sentence-BERT, Image → CLIP)
    virtual std::vector<float> generateEmbedding(
        const std::string& chunk_data
    ) = 0;
    
    virtual std::vector<ContentCategory> getSupportedCategories() const = 0;
};
```

**Implementierte Processors:**

#### TextProcessor
- **Extraction:** UTF-8 Normalisierung, Markdown → Plain Text, Code Syntax-Highlighting
- **Chunking:** Fixed-size (512 Tokens) mit Overlap (50 Tokens), Sentence-Boundary-Preserving
- **Embedding:** Sentence-Transformers (z.B. `all-mpnet-base-v2`, 768D)

#### ImageProcessor
- **Extraction:** EXIF Metadata (Camera, GPS, Timestamp), Dimensions, Color Profile
- **Chunking:** Keine (Bild als ganzes) oder Region-Proposals (für Object Detection)
- **Embedding:** CLIP (`openai/clip-vit-base-patch32`, 512D)

#### GeoProcessor
- **Extraction:** GeoJSON → Coordinates, Properties; GPX → Tracks/Waypoints; GeoTIFF → Raster + Projection
- **Chunking:** Feature-Level (jedes GeoJSON Feature = 1 Chunk)
- **Embedding:** Geo2Vec (Lat/Lon → Embedding) oder Text-Embedding der Properties

#### CADProcessor
- **Extraction:** STEP → Assembly Hierarchy, Parts, BOM; STL → Mesh Geometry
- **Chunking:** Part-Level (jedes Part = 1 Chunk)
- **Embedding:** PartNet (3D Shape → Embedding) oder Property-Text-Embedding

#### AudioProcessor
- **Extraction:** ID3 Tags (Title, Artist, Album), Duration, Bitrate, Codec
- **Chunking:** Time-based (z.B. 30s Segmente) oder Speech-Transcript-based
- **Embedding:** Wav2Vec2 (Audio → Embedding) oder Text-Embedding des Transcripts

#### StructuredProcessor
- **Extraction:** CSV → Schema + Rows, Parquet → Arrow Table
- **Chunking:** Row-Level (jede Zeile = 1 Chunk) oder Batch (z.B. 100 Zeilen)
- **Embedding:** Column-Embeddings (für Schema) + Row-Embeddings (für Data)

#### BinaryProcessor (Fallback)
- **Extraction:** Nur Metadata (Size, Hash, Magic Bytes)
- **Chunking:** Keine (gesamter Blob)
- **Embedding:** Keine (Binary-Daten nicht semantisch suchbar)

### 2.3 ContentManager (Orchestrator)

**Verantwortlichkeit:** Unified Ingestion Pipeline

**Ingestion Flow:**
```cpp
IngestionResult ContentManager::ingestContent(
    const std::string& blob,
    const std::optional<std::string>& mime_type,
    const std::string& filename,
    const json& user_metadata,
    const IngestionConfig& config
) {
    // 1. Content-Type Detection
    const ContentType* type = detectContentType(blob, mime_type, filename);
    
    // 2. Deduplication Check (SHA-256 Hash)
    std::string hash = computeSHA256(blob);
    if (auto existing = checkDuplicateByHash(hash)) {
        return {.ok=true, .content_id=*existing, .message="Duplicate"};
    }
    
    // 3. Processor Routing
    auto* processor = getProcessor(type->category);
    if (!processor) {
        // Fallback to BinaryProcessor
        processor = getProcessor(ContentCategory::BINARY);
    }
    
    // 4. Extraction
    auto extraction = processor->extract(blob, *type);
    if (!extraction.ok) {
        return {.ok=false, .message=extraction.error_message};
    }
    
    // 5. Store Blob (Optional)
    std::string content_id = generateUuid();
    if (config.store_blob) {
        BaseEntity content_entity("content:" + content_id);
        content_entity.setBlob(blob);
        storage_->put("content:" + content_id, content_entity.serialize());
    }
    
    // 6. Chunking
    std::vector<json> chunks;
    if (config.generate_chunks && type->supports_chunking) {
        chunks = processor->chunk(extraction, config.chunk_size, config.chunk_overlap);
    }
    
    // 7. Embedding Generation + VectorIndex Insertion
    std::vector<std::string> chunk_ids;
    if (config.generate_embeddings) {
        for (int i = 0; i < chunks.size(); i++) {
            std::string chunk_id = generateUuid();
            chunk_ids.push_back(chunk_id);
            
            auto embedding = processor->generateEmbedding(chunks[i]["text"]);
            
            // Insert into VectorIndex
            BaseEntity chunk_entity("chunk:" + chunk_id);
            chunk_entity.set("content_id", content_id);
            chunk_entity.set("seq_num", i);
            chunk_entity.set("text", chunks[i]["text"]);
            chunk_entity.set("embedding", embedding);
            
            storage_->put("chunk:" + chunk_id, chunk_entity.serialize());
            vector_index_->addEntity(chunk_entity, embedding);
        }
    }
    
    // 8. Graph Construction
    if (config.build_graph) {
        createChunkGraph(chunk_ids, content_id, "text_chunk");
    }
    
    // 9. Store Metadata
    ContentMeta meta;
    meta.id = content_id;
    meta.mime_type = type->mime_type;
    meta.category = type->category;
    meta.original_filename = filename;
    meta.size_bytes = blob.size();
    meta.hash_sha256 = hash;
    meta.chunk_count = chunks.size();
    meta.extracted_metadata = extraction.metadata;
    meta.user_metadata = user_metadata;
    
    storage_->put("meta:" + content_id, meta.toJson().dump());
    
    return {.ok=true, .content_id=content_id, .chunks_created=(int)chunks.size()};
}
```

## 3. Graph-Strukturen

### 3.1 Chunk-Graph (für RAG)

**Vertex-Typen:**
- `content:<uuid>`: Content-Item (Document, Image, etc.)
- `chunk:<uuid>`: Chunk

**Edge-Typen:**
- `parent`: `chunk -> content` (N:1, jeder Chunk gehört zu genau einem Content-Item)
- `next`: `chunk -> chunk` (sequentielle Reihenfolge, z.B. Paragraph 1 → Paragraph 2)
- `prev`: `chunk -> chunk` (Rückwärts-Navigation)

**Beispiel: Text-Dokument mit 3 Chunks**
```
content:doc123 (Document)
   ├─ chunk:c1 (Paragraph 1) ──next──> chunk:c2 (Paragraph 2) ──next──> chunk:c3 (Paragraph 3)
   │                             ↑                             ↑                             ↑
   └──────────parent──────────────┴──────────parent─────────────┴──────────parent────────────┘
```

**Query: Vector-Search + Graph-Expansion**
```aql
-- 1. Vector-Suche: Top-K Chunks
LET top_chunks = VECTOR_KNN('chunks', @query_vec, 10)

-- 2. Graph-Expansion: Lade Kontext (prev/next)
FOR chunk IN top_chunks
  FOR neighbor IN 1..1 ANY chunk GRAPH 'content_graph'
    FILTER neighbor._type == 'chunk'
    RETURN DISTINCT neighbor
```

### 3.2 Hierarchical Graph (für CAD/Archive)

**Vertex-Typen:**
- `content:assembly` (CAD Assembly)
- `content:part1`, `content:part2`, ... (CAD Parts)

**Edge-Typen:**
- `contains`: `assembly -> part` (1:N, Assembly enthält Parts)
- `sibling`: `part -> part` (Parts auf gleicher Hierarchie-Ebene)

**Beispiel: CAD Assembly**
```
content:assembly (Getriebe)
   ├─── contains ──> content:part1 (Zahnrad A)
   ├─── contains ──> content:part2 (Zahnrad B)
   └─── contains ──> content:part3 (Welle)
```

**Query: Finde alle Parts eines Assemblies**
```aql
FOR part IN 1..1 OUTBOUND 'content:assembly' GRAPH 'cad_graph'
  FILTER part._type == 'part'
  RETURN part
```

### 3.3 Geo-Graph (für GIS-Daten)

**Vertex-Typen:**
- `content:layer` (GeoJSON Layer)
- `content:feature1`, `content:feature2`, ... (GeoJSON Features)

**Edge-Typen:**
- `member_of`: `feature -> layer`
- `spatially_near`: `feature -> feature` (basierend auf Geohash-Proximity)

**Beispiel: GeoJSON Layer mit Features**
```
content:layer (Städte Deutschland)
   ├─── member_of ──> content:feature1 (Berlin)
   ├─── member_of ──> content:feature2 (Hamburg)
   └─── member_of ──> content:feature3 (München)

content:feature1 (Berlin) ──spatially_near──> content:feature4 (Potsdam)
```

## 4. Embedding-Strategien

### 4.1 Text-Embeddings (Sentence-Transformers)

**Modell:** `all-mpnet-base-v2` (768D, hohe Qualität)  
**Alternative:** `all-MiniLM-L6-v2` (384D, schneller)

**Integration:**
```cpp
// Externe API (z.B. Python Microservice mit Flask)
std::vector<float> TextProcessor::generateEmbedding(const std::string& text) {
    // HTTP POST to embedding service
    json request = {{"text", text}};
    auto response = http_client_->post("http://localhost:5000/embed", request);
    return response["embedding"];
}
```

**Mock für Tests:**
```cpp
std::vector<float> TextProcessor::generateEmbedding(const std::string& text) {
    // Simple hash-based mock embedding
    std::vector<float> embedding(768, 0.0f);
    std::hash<std::string> hasher;
    size_t hash = hasher(text);
    for (int i = 0; i < 768; i++) {
        embedding[i] = ((hash >> i) & 1) ? 1.0f : -1.0f;
    }
    return embedding;
}
```

### 4.2 Image-Embeddings (CLIP)

**Modell:** `openai/clip-vit-base-patch32` (512D)

**Integration:**
```python
# Embedding Service (Python Flask)
from transformers import CLIPProcessor, CLIPModel
import torch

model = CLIPModel.from_pretrained("openai/clip-vit-base-patch32")
processor = CLIPProcessor.from_pretrained("openai/clip-vit-base-patch32")

@app.route('/embed/image', methods=['POST'])
def embed_image():
    image_bytes = request.files['image'].read()
    image = Image.open(io.BytesIO(image_bytes))
    inputs = processor(images=image, return_tensors="pt")
    with torch.no_grad():
        embedding = model.get_image_features(**inputs)
    return jsonify({'embedding': embedding[0].tolist()})
```

### 4.3 CAD-Embeddings (PartNet / Custom)

**Ansatz:** 
- **Option 1:** Render CAD-Part als Bild (Multiple Views), dann CLIP-Embedding
- **Option 2:** Extract Properties (Volume, Surface Area, Material) → Text-Embedding
- **Option 3:** PartNet (3D Shape Encoder, research-basiert)

**MVP:** Text-Embedding der BOM/Properties
```cpp
std::vector<float> CADProcessor::generateEmbedding(const std::string& chunk_data) {
    // chunk_data = JSON with CAD properties
    json props = json::parse(chunk_data);
    std::string text = "Part: " + props["name"].get<std::string>() +
                       ", Material: " + props["material"].get<std::string>() +
                       ", Volume: " + std::to_string(props["volume"].get<double>());
    
    // Delegate to TextProcessor
    TextProcessor text_proc;
    return text_proc.generateEmbedding(text);
}
```

## 5. API Design

### 5.1 HTTP Endpoints

**Upload Content**
```http
POST /content/upload
Content-Type: multipart/form-data

Form fields:
- file: binary file
- mime_type: (optional) override MIME detection
- metadata: (optional) JSON with user metadata
- tags: (optional) comma-separated tags
- config: (optional) JSON with IngestionConfig

Response:
{
  "ok": true,
  "content_id": "uuid-1234",
  "chunks_created": 15,
  "message": "Content ingested successfully"
}
```

**Get Content Metadata**
```http
GET /content/:id

Response:
{
  "id": "uuid-1234",
  "mime_type": "application/pdf",
  "category": "TEXT",
  "original_filename": "report.pdf",
  "size_bytes": 1048576,
  "created_at": 1730120400,
  "chunk_count": 15,
  "extracted_metadata": {
    "pages": 10,
    "author": "John Doe"
  },
  "user_metadata": {
    "project": "Alpha"
  },
  "tags": ["report", "2025"]
}
```

**Download Content Blob**
```http
GET /content/:id/blob

Response:
Content-Type: application/pdf
Content-Disposition: attachment; filename="report.pdf"
Content-Length: 1048576

<binary data>
```

**Search Content**
```http
POST /content/search
Content-Type: application/json

{
  "query": "machine learning techniques",
  "k": 10,
  "filters": {
    "category": "TEXT",
    "tags": ["research"]
  },
  "expansion": {
    "enabled": true,
    "hops": 1
  }
}

Response:
{
  "results": [
    {
      "chunk_id": "chunk-uuid-1",
      "content_id": "uuid-1234",
      "score": 0.95,
      "text": "Machine learning techniques have revolutionized...",
      "seq_num": 5,
      "metadata": {
        "filename": "ml_paper.pdf",
        "page": 3
      }
    },
    ...
  ],
  "total": 10,
  "query_time_ms": 45
}
```

**Get Content Chunks**
```http
GET /content/:id/chunks

Response:
{
  "chunks": [
    {
      "id": "chunk-uuid-1",
      "seq_num": 0,
      "text": "Introduction...",
      "start_offset": 0,
      "end_offset": 512,
      "embedding_indexed": true
    },
    ...
  ]
}
```

**Delete Content**
```http
DELETE /content/:id

Response:
{
  "ok": true,
  "message": "Content and 15 chunks deleted"
}
```

**Get Content Compression Config**
```http
GET /content/config

Response:
{
  "compress_blobs": false,
  "compression_level": 19,
  "skip_compressed_mimes": ["image/", "video/", "application/zip", "application/gzip"]
}
```

**Update Content Compression Config**
```http
PUT /content/config
Content-Type: application/json

{
  "compress_blobs": true,
  "compression_level": 15,
  "skip_compressed_mimes": ["image/", "video/"]
}

Response:
{
  "status": "ok",
  "compress_blobs": true,
  "compression_level": 15,
  "skip_compressed_mimes": ["image/", "video/"],
  "note": "Configuration updated. Changes apply to new content imports only."
}
```

**Notes:**
- `compress_blobs`: Enable/disable ZSTD compression for content blobs >4KB
- `compression_level`: 1-22 (higher = better compression, slower; default 19)
- `skip_compressed_mimes`: Array of MIME prefixes to skip (already compressed formats)
- Configuration is stored in DB key `config:content`
- Changes only affect new content imports, not existing data

## 6. Erweiterung: Neue Datentypen hinzufügen

### Beispiel: Video-Processor

**1. Content-Type registrieren**
```cpp
ContentType video_mp4;
video_mp4.mime_type = "video/mp4";
video_mp4.category = ContentCategory::VIDEO;
video_mp4.extensions = {".mp4", ".m4v"};
video_mp4.supports_text_extraction = false; // (außer mit Speech-to-Text)
video_mp4.supports_chunking = true;         // Time-based chunks
video_mp4.supports_embedding = true;        // Video embeddings (VideoMAE, etc.)
video_mp4.features.temporal = true;
video_mp4.features.multimodal = true;       // Audio + Frames

ContentTypeRegistry::instance().registerType(video_mp4);
```

**2. VideoProcessor implementieren**
```cpp
class VideoProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& type) override {
        ExtractionResult result;
        
        // Extract metadata with FFmpeg
        result.metadata = extractVideoMetadata(blob);
        result.media_data = MediaData{
            .duration_seconds = result.metadata["duration"],
            .width = result.metadata["width"],
            .height = result.metadata["height"],
            .codec = result.metadata["codec"]
        };
        
        result.ok = true;
        return result;
    }
    
    std::vector<json> chunk(const ExtractionResult& extraction, int chunk_size, int overlap) override {
        // Chunk by time (e.g., 10-second segments)
        int duration = extraction.media_data->duration_seconds;
        std::vector<json> chunks;
        
        for (int i = 0; i < duration; i += chunk_size) {
            json chunk = {
                {"type", "video_segment"},
                {"start_time", i},
                {"end_time", std::min(i + chunk_size, duration)},
                {"frame_ref", "video_frames_" + std::to_string(i)}
            };
            chunks.push_back(chunk);
        }
        
        return chunks;
    }
    
    std::vector<float> generateEmbedding(const std::string& chunk_data) override {
        // Extract representative frame, encode with CLIP or VideoMAE
        json chunk = json::parse(chunk_data);
        int start_time = chunk["start_time"];
        
        // External call to video embedding service
        return callVideoEmbeddingService(start_time);
    }
    
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::VIDEO};
    }
};
```

**3. Processor registrieren**
```cpp
content_manager->registerProcessor(std::make_unique<VideoProcessor>());
```

**4. Verwenden**
```cpp
auto result = content_manager->ingestContent(
    video_blob,
    "video/mp4",
    "tutorial.mp4",
    json::object(),
    IngestionConfig{
        .chunk_size = 10,  // 10 seconds per chunk
        .chunk_overlap = 2  // 2 seconds overlap
    }
);
```

## 7. Performance-Überlegungen

### 7.1 Blob-Storage

**Problem:** Große Dateien (GB-Range) sollten nicht komplett in RocksDB gespeichert werden.

**Lösung:** Hybrid-Storage
```cpp
struct BlobStorageConfig {
    int64_t inline_threshold_bytes = 1024 * 1024; // 1 MB
    std::string external_storage_path = "./data/blobs/";
};

// In ContentManager::ingestContent()
if (blob.size() < config.inline_threshold_bytes) {
    // Store inline in RocksDB
    entity.setBlob(blob);
} else {
    // Store externally (filesystem or S3)
    std::string blob_path = external_storage_path + content_id + ".blob";
    writeToFile(blob_path, blob);
    entity.set("blob_ref", blob_path);
}
```

### 7.2 Embedding-Batch-Processing

**Problem:** Sequentielle Embedding-Generierung ist langsam.

**Lösung:** Batch-API
```cpp
std::vector<std::vector<float>> generateEmbeddingsBatch(const std::vector<std::string>& texts) {
    json request = {{"texts", texts}};
    auto response = http_client_->post("http://localhost:5000/embed/batch", request);
    return response["embeddings"];
}
```

### 7.3 Async-Ingestion

**Problem:** Große Dateien blockieren HTTP-Response.

**Lösung:** Job-Queue
```cpp
IngestionResult ContentManager::ingestContentAsync(/*...*/) {
    std::string job_id = generateUuid();
    
    // Queue job
    job_queue_->enqueue({
        .job_id = job_id,
        .blob = blob,
        .mime_type = mime_type,
        // ...
    });
    
    return {.ok=true, .content_id=job_id, .message="Queued for processing"};
}

// Background worker
void processJobs() {
    while (true) {
        auto job = job_queue_->dequeue();
        auto result = ingestContent(job.blob, job.mime_type, /*...*/);
        updateJobStatus(job.job_id, result);
    }
}
```

## 8. Testing-Strategie

### 8.1 Unit Tests (pro Processor)

```cpp
TEST(TextProcessorTest, ExtractsTextFromPlainText) {
    TextProcessor processor;
    std::string blob = "Hello, world!";
    ContentType type = {.mime_type="text/plain", .category=ContentCategory::TEXT};
    
    auto result = processor.extract(blob, type);
    
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.text, "Hello, world!");
}

TEST(TextProcessorTest, ChunksTextWithOverlap) {
    TextProcessor processor;
    ExtractionResult extraction;
    extraction.text = "Lorem ipsum dolor sit amet..."; // 1000 chars
    
    auto chunks = processor.chunk(extraction, 512, 50);
    
    ASSERT_GE(chunks.size(), 2);
    // Verify overlap
    std::string end_of_chunk1 = chunks[0]["text"].get<std::string>().substr(462, 50);
    std::string start_of_chunk2 = chunks[1]["text"].get<std::string>().substr(0, 50);
    EXPECT_EQ(end_of_chunk1, start_of_chunk2);
}
```

### 8.2 Integration Tests

```cpp
TEST(ContentManagerTest, IngestTextDocumentEndToEnd) {
    auto storage = std::make_shared<RocksDBWrapper>("./test_db");
    auto vector_index = std::make_shared<VectorIndexManager>(/*...*/);
    auto graph_index = std::make_shared<GraphIndexManager>(/*...*/);
    auto secondary_index = std::make_shared<SecondaryIndexManager>(/*...*/);
    
    ContentManager manager(storage, vector_index, graph_index, secondary_index);
    manager.registerProcessor(std::make_unique<TextProcessor>());
    
    std::string blob = "This is a test document. It has multiple sentences.";
    auto result = manager.ingestContent(blob, "text/plain", "test.txt");
    
    ASSERT_TRUE(result.ok);
    EXPECT_GT(result.chunks_created, 0);
    
    // Verify metadata stored
    auto meta = manager.getContentMeta(result.content_id);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->mime_type, "text/plain");
    
    // Verify chunks stored
    auto chunks = manager.getContentChunks(result.content_id);
    EXPECT_EQ(chunks.size(), result.chunks_created);
    
    // Verify graph edges
    auto neighbors = graph_index->getOutNeighbors("chunk:" + chunks[0].id);
    EXPECT_GT(neighbors.size(), 0); // Has 'next' edge
}
```

### 8.3 Performance Benchmarks

```cpp
BENCHMARK(BM_IngestLargeDocument) {
    std::string large_doc(10 * 1024 * 1024, 'A'); // 10 MB
    for (auto _ : state) {
        content_manager->ingestContent(large_doc, "text/plain", "large.txt");
    }
}

BENCHMARK(BM_SearchWithExpansion) {
    for (auto _ : state) {
        content_manager->searchWithExpansion("machine learning", 10, 1);
    }
}
```

## 9. Migration-Plan

### Phase 1: Foundation (Woche 1-2)
- [ ] ContentType + ContentTypeRegistry implementieren
- [ ] IContentProcessor Interface + BinaryProcessor (Fallback)
- [ ] ContentManager Grundstruktur (ohne Processors)
- [ ] Unit Tests für ContentTypeRegistry

### Phase 2: Text-Processor (Woche 3)
- [ ] TextProcessor implementieren (extract, chunk, embedding mit Mock)
- [ ] Integration in ContentManager
- [ ] HTTP Endpoint: POST /content/upload (nur TEXT)
- [ ] Integration Tests

### Phase 3: Image/Geo/CAD-Processors (Woche 4-5)
- [ ] ImageProcessor (EXIF extraction, CLIP embedding via external service)
- [ ] GeoProcessor (GeoJSON parsing)
- [ ] CADProcessor (STEP parsing mit Open CASCADE)
- [ ] HTTP Endpoints erweitern

### Phase 4: Hybrid-Queries (Woche 6)
- [ ] AQL VECTOR_KNN() Function
- [ ] Graph-Expansion in ContentManager::searchWithExpansion()
- [ ] Benchmarks

### Phase 5: Production-Hardening (Woche 7+)
- [ ] Async-Ingestion (Job-Queue)
- [ ] External Blob-Storage (Filesystem/S3)
- [ ] Monitoring/Metrics
- [ ] Documentation

## 10. Fazit

Das Content Manager System bietet eine **skalierbare, erweiterbare Architektur** für heterogene Datentypen. Durch die Trennung von generischen Operationen (Hashing, Graph-Erstellung) und typ-spezifischer Verarbeitung (via Processors) bleibt das System wartbar und einfach erweiterbar.

**Key Benefits:**
- **Einheitliche API:** Ein Upload-Endpoint für alle Datentypen
- **Wiederverwendbare Komponenten:** Chunking-Logik, Graph-Erstellung, Deduplication
- **Typ-Sicherheit:** ContentTypeRegistry verhindert falsche Verarbeitung
- **Produktivität:** Neue Datentypen in < 1 Tag integrierbar (nur Processor implementieren)
- **RAG-Ready:** Graph-Expansion für kontextuelle Suche out-of-the-box
