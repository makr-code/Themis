# VCC Base Adapter Library

Common Python library for all VCC (Virtual Compliance Center) adapters connecting to ThemisDB.

## Overview

This library provides shared functionality for VCC adapters:
- **ThemisDB HTTP Client** - Unified API client for all ThemisDB operations
- **Data Processors** - Text processing, chunking, and embedding generation
- **Configuration Management** - Environment-based configuration with Pydantic
- **Utilities** - Logging, connection validation, error handling

**IMPORTANT:** This library uses **direct HTTP connections** to ThemisDB. **NO UDS3 framework dependency** is required.

## Features

- ✅ Direct HTTP access to ThemisDB (no UDS3)
- ✅ Content import with automatic chunking
- ✅ Optional embedding generation (sentence-transformers)
- ✅ AQL query execution
- ✅ Vector similarity search
- ✅ Entity CRUD operations
- ✅ Batch operations support
- ✅ Namespace isolation
- ✅ JWT authentication support

## Installation

```bash
cd adapters/vcc_base
pip install -r requirements.txt
```

For semantic embeddings (optional):
```bash
pip install sentence-transformers
```

## Usage

### Basic Client

```python
from vcc_base import ThemisVCCClient
import asyncio

async def main():
    client = ThemisVCCClient(
        base_url="http://localhost:8765",
        namespace="vcc_data"
    )
    
    # Health check
    health = await client.health_check()
    print(health)
    
    # Import content
    payload = {
        "content": {
            "mime_type": "text/plain",
            "tags": ["vcc", "test"]
        },
        "chunks": [
            {
                "seq_num": 0,
                "chunk_type": "text",
                "text": "Hello from VCC adapter"
            }
        ],
        "edges": []
    }
    
    result = await client.import_content(payload)
    print(f"Imported content: {result}")

asyncio.run(main())
```

### Text Processing

```python
from vcc_base import TextProcessor

processor = TextProcessor(
    enable_embeddings=True,
    chunk_size=800
)

# Process text into ThemisDB payload
text = "This is a sample document. It has multiple sentences."
payload = processor.process(
    text=text,
    source="document.txt",
    tags=["sample", "vcc"]
)

# payload is ready for client.import_content()
```

### Configuration

```python
from vcc_base import VCCAdapterConfig

# Load from environment variables or .env file
config = VCCAdapterConfig()

print(config.themis_url)  # http://127.0.0.1:8765
print(config.enable_embeddings)  # True
print(config.chunk_size)  # 800

# Override defaults
config = VCCAdapterConfig(
    themis_url="http://prod-themis:8765",
    themis_namespace="production",
    enable_embeddings=False
)
```

### Complete Example

```python
from vcc_base import ThemisVCCClient, TextProcessor, VCCAdapterConfig
import asyncio

async def ingest_document(filename: str):
    # Load configuration
    config = VCCAdapterConfig()
    
    # Initialize client and processor
    client = ThemisVCCClient(
        base_url=config.themis_url,
        namespace=config.themis_namespace
    )
    processor = TextProcessor(
        enable_embeddings=config.enable_embeddings,
        chunk_size=config.chunk_size
    )
    
    # Read and process document
    with open(filename, 'r', encoding='utf-8') as f:
        text = f.read()
    
    payload = processor.process(
        text=text,
        source=filename,
        tags=["vcc", "document"]
    )
    
    # Import to ThemisDB
    result = await client.import_content(payload)
    print(f"Imported {filename}: {result}")

asyncio.run(ingest_document("sample.txt"))
```

## Environment Variables

- `THEMIS_URL` - ThemisDB base URL (default: http://127.0.0.1:8765)
- `THEMIS_AUTH_TOKEN` - Optional JWT authentication token
- `ENABLE_EMBEDDINGS` - Enable embedding generation (default: true)

## API Reference

### ThemisVCCClient

**Methods:**
- `health_check()` - Check ThemisDB health
- `import_content(payload)` - Import content with chunks
- `query_aql(query, bind_vars)` - Execute AQL query
- `vector_search(embedding, k, filter_expr)` - Vector similarity search
- `get_entity(key)` - Get entity by key
- `put_entity(key, data)` - Create/update entity
- `delete_entity(key)` - Delete entity
- `batch_import(entities)` - Batch import entities

### TextProcessor

**Methods:**
- `process(text, mime_type, source, tags)` - Process text to payload
- `chunk_text(text)` - Split text into semantic chunks
- `embed_text(text)` - Generate embedding for text

### VCCAdapterConfig

**Fields:**
- `themis_url` - ThemisDB base URL
- `themis_namespace` - Namespace for data isolation
- `themis_auth_token` - Optional JWT token
- `enable_embeddings` - Enable embedding generation
- `embedding_model` - Sentence transformer model name
- `chunk_size` - Maximum chunk size (chars)
- `batch_size` - Batch operation size

## Architecture

```
┌─────────────────────────────┐
│   VCC Adapter (Clara,       │
│   Covina, Veritas, etc.)    │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│   VCC Base Library          │
│  ┌───────────────────────┐  │
│  │ ThemisVCCClient       │  │
│  │ TextProcessor         │  │
│  │ VCCAdapterConfig      │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ HTTP (no UDS3)
              ▼
┌─────────────────────────────┐
│   ThemisDB Backend          │
│   (http://host:8765)        │
└─────────────────────────────┘
```

## Development

This is a shared library used by all VCC adapters. When making changes:

1. Ensure backward compatibility with existing adapters
2. Add tests for new functionality
3. Update this README with new features
4. Version appropriately (semver)

## Related Adapters

- `vcc_covina_ingestion` - Covina system adapter (production)
- `vcc_clara_ingestion` - Clara system adapter
- `vcc_veritas` - Veritas system adapter

## License

See main ThemisDB project license.
