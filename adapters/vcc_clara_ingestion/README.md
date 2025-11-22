# VCC-Clara Ingestion Adapter

FastAPI-based ingestion adapter for the VCC-Clara system. This adapter complements the existing VCC-Clara Export API (documented in `docs/api/VCC_CLARA_EXPORT_API.md`) by providing data ingestion capabilities.

## Overview

VCC-Clara is a legal AI system that works with:
- **Rechtssprechung** (Case Law)
- **Immissionsschutz** (Environmental Protection Law)
- Other legal and regulatory documentation

This adapter provides specialized ingestion endpoints for VCC-Clara content, preparing it for:
1. Storage in ThemisDB
2. Later export for LLM training (via Export API)
3. Semantic search and retrieval

**NO UDS3 DEPENDENCY** - Uses direct HTTP connections to ThemisDB via the vcc_base library.

## Features

- ✅ Specialized legal document ingestion (`/ingest/legal`)
- ✅ Environmental law document ingestion (`/ingest/environmental`)
- ✅ Generic file upload (`/ingest/file`)
- ✅ Direct JSON import (`/ingest/json`)
- ✅ Batch import (`/batch/legal`)
- ✅ Thematic classification (theme, domain, subject)
- ✅ Optional embedding generation for vector search
- ✅ Quality rating support
- ✅ Case number and source tracking

## Prerequisites

- Python 3.10+
- Running ThemisDB instance (default: http://127.0.0.1:8765)
- vcc_base library (in parent directory)

## Quick Start

### Windows PowerShell

```powershell
cd adapters\vcc_clara_ingestion

# Create virtual environment
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Install dependencies (including vcc_base)
pip install -r requirements.txt
cd ..\vcc_base
pip install -r requirements.txt
cd ..\vcc_clara_ingestion

# Configure environment
$env:THEMIS_URL = "http://127.0.0.1:8765"
$env:ENABLE_EMBEDDINGS = "true"
$env:LOG_LEVEL = "INFO"

# Run adapter
uvicorn app:app --host 127.0.0.1 --port 8002 --reload
```

### Linux/macOS

```bash
cd adapters/vcc_clara_ingestion

# Create virtual environment
python3 -m venv .venv
source .venv/bin/activate

# Install dependencies
pip install -r requirements.txt
cd ../vcc_base
pip install -r requirements.txt
cd ../vcc_clara_ingestion

# Configure environment
export THEMIS_URL="http://127.0.0.1:8765"
export ENABLE_EMBEDDINGS="true"
export LOG_LEVEL="INFO"

# Run adapter
uvicorn app:app --host 127.0.0.1 --port 8002 --reload
```

## API Endpoints

### Health Check

```bash
curl http://localhost:8002/health
```

### Ingest Legal Document (Rechtssprechung)

```bash
curl -X POST http://localhost:8002/ingest/legal \
  -F "file=@legal_case.txt" \
  -F "theme=Rechtssprechung" \
  -F "domain=environmental_law" \
  -F "case_number=BVerwG 7 C 25.20" \
  -F "source=BVerwG" \
  -F "rating=4.5"
```

### Ingest Environmental Document (Immissionsschutz)

```bash
curl -X POST http://localhost:8002/ingest/environmental \
  -F "file=@ta_luft_excerpt.txt" \
  -F "subject=luftqualität" \
  -F "domain=environmental_law" \
  -F "source=TA Luft" \
  -F "rating=5.0"
```

### Generic File Upload

```bash
curl -X POST http://localhost:8002/ingest/file \
  -F "file=@document.txt" \
  -F "theme=Datenschutz" \
  -F "domain=privacy_law" \
  -F "tags=gdpr,dsgvo"
```

### Direct JSON Import

```bash
curl -X POST http://localhost:8002/ingest/json \
  -H "Content-Type: application/json" \
  -d '{
    "content": {
      "mime_type": "text/plain",
      "tags": ["vcc_clara", "legal"]
    },
    "chunks": [
      {
        "seq_num": 0,
        "chunk_type": "text",
        "text": "Legal content here..."
      }
    ],
    "edges": [],
    "metadata": {
      "theme": "Rechtssprechung",
      "domain": "environmental_law",
      "rating": 4.5
    }
  }'
```

### Batch Legal Import

```bash
curl -X POST http://localhost:8002/batch/legal \
  -H "Content-Type: application/json" \
  -d '[
    {
      "text": "Case law text 1...",
      "metadata": {
        "theme": "Rechtssprechung",
        "case_number": "Case-001",
        "source": "BVerwG"
      }
    },
    {
      "text": "Case law text 2...",
      "metadata": {
        "theme": "Rechtssprechung",
        "case_number": "Case-002",
        "source": "BGH"
      }
    }
  ]'
```

## Configuration

### Environment Variables

- `THEMIS_URL` - ThemisDB base URL (default: http://127.0.0.1:8765)
- `THEMIS_AUTH_TOKEN` - Optional JWT authentication token
- `ENABLE_EMBEDDINGS` - Enable embedding generation (default: true)
- `LOG_LEVEL` - Logging level: DEBUG, INFO, WARNING, ERROR (default: INFO)

### VCC-Clara Metadata Fields

All endpoints support these metadata fields:

- `theme` - Main topic (Rechtssprechung, Immissionsschutz, Datenschutz, etc.)
- `domain` - Domain classification (environmental_law, labor_law, privacy_law, etc.)
- `subject` - Fine-grained subject (immissionsschutz, luftqualität, lärmschutz, etc.)
- `case_number` - Legal case identifier (for Rechtssprechung)
- `source` - Document source (BVerwG, BGH, TA Luft, etc.)
- `rating` - Quality rating 0.0-5.0 (for later export filtering)

## Integration with VCC-Clara Export

This ingestion adapter is designed to work seamlessly with the VCC-Clara Export API:

1. **Ingest** legal/environmental documents via this adapter
2. **Query** and filter by theme, domain, temporal boundaries
3. **Export** training data via `/api/export/jsonl_llm` (see docs/api/VCC_CLARA_EXPORT_API.md)

Example workflow:

```python
import requests

# Step 1: Ingest legal documents
with open("rechtssprechung.txt", "rb") as f:
    requests.post(
        "http://localhost:8002/ingest/legal",
        files={"file": f},
        data={
            "theme": "Rechtssprechung",
            "domain": "environmental_law",
            "rating": 4.5
        }
    )

# Step 2: Export for training (later)
export_response = requests.post(
    "http://localhost:8765/api/export/jsonl_llm",
    json={
        "theme": "Rechtssprechung",
        "domain": "environmental_law",
        "from_date": "2020-01-01",
        "to_date": "2024-12-31",
        "format": "instruction_tuning",
        "field_mapping": {
            "instruction_field": "question",
            "output_field": "answer"
        }
    }
)
```

## Architecture

```
┌─────────────────────────────┐
│   VCC-Clara Frontend        │
│   (Document Upload UI)      │
└─────────────┬───────────────┘
              │ HTTP POST
              ▼
┌─────────────────────────────┐
│ VCC-Clara Ingestion Adapter │
│ (FastAPI, Port 8002)        │
│  ┌───────────────────────┐  │
│  │ Legal Ingestion       │  │
│  │ Environmental Ing.    │  │
│  │ Generic File Upload   │  │
│  │ Batch Processing      │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ Uses vcc_base
              ▼
┌─────────────────────────────┐
│   VCC Base Library          │
│  ┌───────────────────────┐  │
│  │ Text Processing       │  │
│  │ Chunking & Embeddings │  │
│  │ ThemisDB Client       │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ HTTP (no UDS3)
              ▼
┌─────────────────────────────┐
│   ThemisDB Backend          │
│   (Port 8765)               │
└─────────────────────────────┘
```

## Python Integration Example

```python
import httpx
import asyncio

async def ingest_legal_corpus(directory: str):
    """Ingest a directory of legal documents."""
    
    async with httpx.AsyncClient() as client:
        for filepath in Path(directory).glob("*.txt"):
            with open(filepath, "rb") as f:
                response = await client.post(
                    "http://localhost:8002/ingest/legal",
                    files={"file": f},
                    data={
                        "theme": "Rechtssprechung",
                        "domain": "environmental_law",
                        "source": "BVerwG",
                        "rating": 4.0
                    }
                )
                
                if response.status_code == 200:
                    print(f"✓ Imported {filepath.name}")
                else:
                    print(f"✗ Failed to import {filepath.name}: {response.text}")

asyncio.run(ingest_legal_corpus("/data/legal_docs"))
```

## Embeddings

The adapter supports optional embedding generation:

- **With sentence-transformers**: Install `pip install sentence-transformers` for semantic embeddings
- **Without**: Uses lightweight hash-based embeddings (sufficient for testing, not semantically meaningful)

For production VCC-Clara deployments, semantic embeddings are recommended for vector search capabilities.

## Monitoring

The adapter logs all operations:

```
2024-11-22 10:30:45 - vcc_clara - INFO - Imported legal document: case_001.txt, theme=Rechtssprechung
2024-11-22 10:30:46 - vcc_clara - INFO - Imported environmental document: ta_luft.txt, subject=luftqualität
```

Set `LOG_LEVEL=DEBUG` for detailed debugging information.

## Troubleshooting

**Connection Error to ThemisDB:**
```
Failed to connect to ThemisDB at http://127.0.0.1:8765
```
→ Ensure ThemisDB is running: `./themis_server --port 8765`

**Module not found: vcc_base:**
```
ModuleNotFoundError: No module named 'vcc_base'
```
→ Install vcc_base dependencies: `cd ../vcc_base && pip install -r requirements.txt`

**Empty embeddings:**
→ Install sentence-transformers: `pip install sentence-transformers`

## Related Documentation

- [VCC-Clara Export API](../../docs/api/VCC_CLARA_EXPORT_API.md) - Complementary export API
- [VCC Base Library](../vcc_base/README.md) - Shared adapter library
- [ThemisDB Content Pipeline](../../docs/content_pipeline.md) - Content architecture

## License

See main ThemisDB project license.
