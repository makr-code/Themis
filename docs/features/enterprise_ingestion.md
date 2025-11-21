# Enterprise Ingestion DLL Interface Specification

**Version:** 1.0  
**Datum:** 17. November 2025  
**Zweck:** API-Spezifikation für externe Enterprise Ingestion Pipeline

---

## Übersicht

Die **Enterprise Ingestion DLL** übernimmt alle Ingestion-bezogenen Features:
- Text Extraction (PDF, DOCX, Markdown, Code)
- Chunking Pipeline (Fixed-size, Semantic, Sliding Window)
- Binary Blob Storage (>5MB → Filesystem)
- Multi-Modal Embeddings (Text + Image + Audio)
- Embedding Generation (via OpenAI/Cohere/Local Models)

ThemisDB Core stellt Storage, Indexierung und Retrieval bereit.

---

## Architektur

```
┌─────────────────────────────────────────────────────────┐
│         Enterprise Ingestion DLL (extern)               │
│  • Text Extraction (PDF/DOCX/MD)                        │
│  • Chunking (512 tokens, overlap 50)                    │
│  • Embedding Generation (OpenAI/Cohere)                 │
│  • Blob Storage (>5MB → Filesystem)                     │
└────────────────────┬────────────────────────────────────┘
                     │ JSON Import API
                     ▼
┌─────────────────────────────────────────────────────────┐
│         ThemisDB Core (Open Source)                     │
│  • ContentManager (Storage)                             │
│  • VectorIndexManager (HNSW)                            │
│  • GraphIndexManager (Chunk Relations)                  │
│  • SecondaryIndexManager (Tags, Metadata)               │
└─────────────────────────────────────────────────────────┘
```

---

## API Interface

### 1. Import Endpoint (ThemisDB → DLL)

**ThemisDB bietet:**
```http
POST /content/import
Content-Type: application/json

{
  "content": {
    "id": "uuid-1234",
    "mime_type": "application/pdf",
    "category": "TEXT",
    "original_filename": "report.pdf",
    "size_bytes": 1048576,
    "created_at": 1730120400,
    "hash_sha256": "abc123...",
    "tags": ["research", "2025"],
    "user_metadata": {"project": "Alpha"}
  },
  "chunks": [
    {
      "id": "chunk-uuid-1",
      "content_id": "uuid-1234",
      "seq_num": 0,
      "text": "Chapter 1: Introduction...",
      "start_char": 0,
      "end_char": 512,
      "embedding": [0.1, 0.2, 0.3, ...],
      "metadata": {"page": 1, "section": "intro"}
    },
    {
      "id": "chunk-uuid-2",
      "content_id": "uuid-1234",
      "seq_num": 1,
      "text": "Machine learning is...",
      "start_char": 462,
      "end_char": 974,
      "embedding": [0.4, 0.5, 0.6, ...],
      "metadata": {"page": 2, "section": "intro"}
    }
  ],
  "edges": [
    {
      "id": "edge-1",
      "_from": "chunk-uuid-1",
      "_to": "chunk-uuid-2",
      "_type": "NEXT"
    }
  ]
}
```

**Response:**
```json
{
  "ok": true,
  "message": "Content imported successfully",
  "content_id": "uuid-1234",
  "chunks_stored": 15,
  "edges_created": 14
}
```

---

### 2. DLL Workflow

**Enterprise DLL übernimmt:**

#### Step 1: Text Extraction
```cpp
// DLL Export
extern "C" __declspec(dllexport)
ExtractionResult extractText(const char* blob, size_t blob_size, const char* mime_type);

struct ExtractionResult {
    char* text;              // Extracted plain text
    int page_count;
    char* metadata_json;     // {"author": "...", "title": "..."}
};
```

**Supported MIME Types:**
- `application/pdf` → PDFium/Poppler
- `application/vnd.openxmlformats-officedocument.wordprocessingml.document` → libdocx
- `text/markdown` → Raw text
- `text/plain` → Raw text
- `application/json` → Parsed JSON

---

#### Step 2: Chunking
```cpp
// DLL Export
extern "C" __declspec(dllexport)
ChunkingResult chunkText(const char* text, const ChunkingConfig* config);

struct ChunkingConfig {
    int chunk_size;          // Default: 512 tokens
    int overlap;             // Default: 50 tokens
    bool respect_sentences;  // Default: true
    const char* tokenizer;   // "whitespace" | "tiktoken" | "sentencepiece"
};

struct ChunkingResult {
    Chunk* chunks;
    int chunk_count;
};

struct Chunk {
    char* text;
    int start_char;
    int end_char;
    int seq_num;
};
```

**Chunking Strategies:**
1. **Fixed Size:** 512 tokens per chunk, overlap 50
2. **Semantic:** Sentence/Paragraph boundaries (spaCy/NLTK)
3. **Sliding Window:** Continuous overlap

---

#### Step 3: Embedding Generation
```cpp
// DLL Export
extern "C" __declspec(dllexport)
EmbeddingResult generateEmbedding(const char* text, const char* model_name);

struct EmbeddingResult {
    float* embedding;
    int dimension;         // e.g., 1536 for text-embedding-3-small
    const char* model;     // "openai/text-embedding-3-small"
};
```

**Supported Models:**
- OpenAI: `text-embedding-3-small` (1536 dim), `text-embedding-3-large` (3072 dim)
- Cohere: `embed-english-v3.0` (1024 dim)
- Local: `sentence-transformers/all-MiniLM-L6-v2` (384 dim)

---

#### Step 4: Blob Storage (Large Files)
```cpp
// DLL Export
extern "C" __declspec(dllexport)
BlobStorageResult storeLargeBlob(const char* blob, size_t blob_size, const char* hash);

struct BlobStorageResult {
    char* storage_path;    // e.g., "data/blobs/abc123.bin"
    bool compressed;       // ZSTD compression applied
    size_t compressed_size;
};
```

**Storage Strategy:**
- `<5MB` → RocksDB (inline)
- `>=5MB` → Filesystem (`data/blobs/<sha256>.bin`)
- Compression: ZSTD Level 19 for text, skip for images/videos

---

### 3. Complete Workflow Example

**DLL Pseudocode:**
```cpp
void processPDF(const char* pdf_blob, size_t blob_size) {
    // 1. Extract text
    ExtractionResult extracted = extractText(pdf_blob, blob_size, "application/pdf");
    
    // 2. Chunk text
    ChunkingConfig cfg = {.chunk_size = 512, .overlap = 50, .respect_sentences = true};
    ChunkingResult chunks = chunkText(extracted.text, &cfg);
    
    // 3. Generate embeddings
    std::vector<EmbeddingResult> embeddings;
    for (int i = 0; i < chunks.chunk_count; i++) {
        embeddings.push_back(generateEmbedding(chunks.chunks[i].text, "openai/text-embedding-3-small"));
    }
    
    // 4. Build JSON for ThemisDB
    json import_spec = buildImportSpec(extracted, chunks, embeddings);
    
    // 5. Send to ThemisDB
    http_post("/content/import", import_spec.dump());
}
```

---

## Integration Points

### ThemisDB Core verantwortlich für:
✅ **Storage:**
- ContentMeta/ChunkMeta in RocksDB
- Blob storage (optional filesystem delegation)

✅ **Indexing:**
- Vector Index (HNSW für embeddings)
- Graph Index (Chunk relations: NEXT, PARENT)
- Secondary Index (tags, metadata, category)

✅ **Retrieval:**
- `/content/search` (Hybrid Search)
- `/content/:id` (Get metadata)
- `/content/:id/blob` (Download original)
- `/fs/:path` (Filesystem interface)

### Enterprise DLL verantwortlich für:
✅ **Ingestion:**
- Text extraction (PDF/DOCX/MD)
- Chunking pipeline
- Embedding generation
- Large blob storage strategy

---

## Configuration

**ThemisDB Config (`config.json`):**
```json
{
  "content": {
    "enable_enterprise_ingestion": true,
    "dll_path": "C:/path/to/themis_ingestion_enterprise.dll",
    "blob_storage_threshold_mb": 5,
    "default_chunk_size": 512,
    "default_overlap": 50,
    "embedding_model": "openai/text-embedding-3-small",
    "openai_api_key": "${OPENAI_API_KEY}"
  }
}
```

**Environment Variables:**
```bash
OPENAI_API_KEY=sk-...
COHERE_API_KEY=co-...
THEMIS_ENTERPRISE_DLL=/opt/themis/ingestion.so
```

---

## Performance Expectations

**Ingestion Throughput:**
- PDF (10 pages): ~2-5 seconds (extraction + chunking + embedding)
- DOCX (50 pages): ~5-10 seconds
- Markdown (100KB): ~500ms

**Embedding Generation:**
- OpenAI API: ~100ms per chunk (rate limit: 3000 RPM)
- Local Model: ~50ms per chunk (GPU), ~200ms (CPU)

**Storage:**
- RocksDB write: ~1-2ms per chunk
- HNSW insert: ~5-10ms per vector (M=16, efConstruction=200)

---

## Error Handling

**DLL Error Codes:**
```cpp
enum IngestionErrorCode {
    SUCCESS = 0,
    EXTRACTION_FAILED = 1001,
    CHUNKING_FAILED = 1002,
    EMBEDDING_FAILED = 1003,
    STORAGE_FAILED = 1004,
    INVALID_FORMAT = 1005
};
```

**ThemisDB Response:**
```json
{
  "ok": false,
  "error": "Extraction failed: Unsupported PDF version",
  "code": 1001
}
```

---

## Testing Strategy

**DLL Unit Tests:**
- PDF extraction (multi-page, Unicode, images)
- Chunking (overlap, sentence boundaries)
- Embedding generation (API mocking)
- Blob storage (compression, deduplication)

**Integration Tests:**
- End-to-end: Upload PDF → Extract → Chunk → Embed → Search
- Large file handling (>100MB PDFs)
- Multi-modal (PDF with images)

---

## Future Extensions

**Geplante Features:**
- **Image Extraction:** OCR für embedded images (Tesseract)
- **Audio Transcription:** Whisper API integration
- **Video Processing:** Frame extraction + scene detection
- **Multi-Language:** Chunking mit spaCy (DE/EN/FR)
- **Custom Models:** Fine-tuned embeddings per tenant

---

## Deployment

**DLL Packaging:**
```
themis_ingestion_enterprise.dll
├── dependencies/
│   ├── poppler.dll
│   ├── opencv.dll
│   └── libzip.dll
├── models/
│   └── sentence-transformers-all-MiniLM-L6-v2/ (optional local model)
└── config/
    └── ingestion_config.json
```

**ThemisDB Integration:**
```cpp
// In HttpServer startup
if (config_.content.enable_enterprise_ingestion) {
    ingestion_dll_ = loadLibrary(config_.content.dll_path);
    extractText = (ExtractTextFunc)getSymbol(ingestion_dll_, "extractText");
    // ... load other functions
}
```

---

**Status:** Interface-Spezifikation vollständig  
**Nächster Schritt:** DLL-Entwicklung durch Enterprise Team
