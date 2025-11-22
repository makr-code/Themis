# ThemisDB Adapters

This directory contains adapters for integrating ThemisDB with external systems and data sources.

**IMPORTANT:** All VCC adapters use **direct HTTP connections** to ThemisDB. **NO UDS3 framework dependency** is required.

## Available Adapters

### VCC Base Library ✅
**Path:** `vcc_base/`  
**Type:** Python Library  
**Status:** Production Ready

Shared library for all VCC (Virtual Compliance Center) adapters:
- ThemisDB HTTP client with full API support
- Text processing and chunking
- Optional embedding generation (sentence-transformers)
- Configuration management
- Utilities and logging

**Use this library** when building new VCC adapters to ensure consistency and reduce code duplication.

---

### Covina FastAPI Ingestion ✅
**Path:** `covina_fastapi_ingestion/`  
**Type:** FastAPI Application  
**Port:** 8001  
**Status:** Production Ready

FastAPI-based ingestion adapter for the Covina system:
- File upload endpoint (`/ingest/file`)
- Direct JSON import (`/ingest/json`)
- Text processing with automatic chunking
- Optional embedding generation
- Integration with ThemisDB's content pipeline

**Endpoints:**
- `POST /ingest/file` - Upload and process files
- `POST /ingest/json` - Direct structured payload import
- `GET /health` - Health check

---

### VCC-Clara Ingestion ✅
**Path:** `vcc_clara_ingestion/`  
**Type:** FastAPI Application  
**Port:** 8002  
**Status:** Production Ready

Specialized ingestion adapter for VCC-Clara legal AI system:
- Legal document ingestion (Rechtssprechung)
- Environmental law documentation (Immissionsschutz)
- Thematic classification (theme, domain, subject)
- Quality rating support
- Batch import capabilities

**Endpoints:**
- `POST /ingest/legal` - Ingest legal documents
- `POST /ingest/environmental` - Ingest environmental law docs
- `POST /ingest/file` - Generic file upload
- `POST /ingest/json` - Direct JSON import with Clara metadata
- `POST /batch/legal` - Batch import legal documents
- `GET /health` - Health check

**Complements:** VCC-Clara Export API (see `docs/api/VCC_CLARA_EXPORT_API.md`)

---

### VCC-Veritas ✅
**Path:** `vcc_veritas/`  
**Type:** FastAPI Application  
**Port:** 8003  
**Status:** Production Ready

Verification and compliance adapter for VCC-Veritas system:
- Document verification with SHA-256 checksums
- Compliance checking and validation
- Data classification (public, internal, confidential, restricted)
- Audit trail recording
- Data integrity validation

**Endpoints:**
- `POST /verify/document` - Verify and ingest document with checksum
- `POST /verify/compliance` - Verify compliance data
- `POST /audit/record` - Record audit entry
- `POST /validate/integrity` - Validate data integrity
- `POST /classify/data` - Classify and ingest data
- `GET /health` - Health check

---

## Architecture

All VCC adapters follow a common architecture:

```
┌─────────────────────────────┐
│   VCC Applications          │
│   (Covina, Clara, Veritas)  │
└─────────────┬───────────────┘
              │ HTTP
              ▼
┌─────────────────────────────┐
│   VCC Adapters              │
│   (FastAPI, Ports 8001-8003)│
│  ┌───────────────────────┐  │
│  │ Domain-specific logic │  │
│  │ Specialized endpoints │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ Uses vcc_base
              ▼
┌─────────────────────────────┐
│   VCC Base Library          │
│  ┌───────────────────────┐  │
│  │ ThemisDB Client       │  │
│  │ Text Processing       │  │
│  │ Configuration         │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ HTTP (no UDS3!)
              ▼
┌─────────────────────────────┐
│   ThemisDB Backend          │
│   (Port 8765)               │
└─────────────────────────────┘
```

## Quick Start

Each adapter can be run independently:

```bash
# VCC-Covina (existing)
cd covina_fastapi_ingestion
pip install -r requirements.txt
uvicorn app:app --port 8001

# VCC-Clara
cd vcc_clara_ingestion
pip install -r requirements.txt
pip install -r ../vcc_base/requirements.txt
uvicorn app:app --port 8002

# VCC-Veritas
cd vcc_veritas
pip install -r requirements.txt
pip install -r ../vcc_base/requirements.txt
uvicorn app:app --port 8003
```

## Configuration

All adapters support environment variables:

- `THEMIS_URL` - ThemisDB base URL (default: http://127.0.0.1:8765)
- `THEMIS_AUTH_TOKEN` - Optional JWT authentication token
- `ENABLE_EMBEDDINGS` - Enable embedding generation (default: true)
- `LOG_LEVEL` - Logging level: DEBUG, INFO, WARNING, ERROR

## Creating New VCC Adapters

To create a new VCC adapter:

1. Create a new directory: `adapters/vcc_yourname/`
2. Add `requirements.txt` with FastAPI dependencies
3. Import and use `vcc_base` library:
   ```python
   from vcc_base import ThemisVCCClient, TextProcessor, VCCAdapterConfig
   ```
4. Create FastAPI app with domain-specific endpoints
5. Document in README.md following existing adapter patterns

See `vcc_base/README.md` for library documentation.

## UDS3 Framework Status

**UDS3 is NOT required.** All adapters use direct HTTP connections to ThemisDB:

- ✅ Covina adapter: Direct HTTP (no UDS3)
- ✅ Clara adapter: Direct HTTP (no UDS3)
- ✅ Veritas adapter: Direct HTTP (no UDS3)
- ✅ VCC base library: Direct HTTP (no UDS3)

This design decision simplifies deployment, reduces dependencies, and improves maintainability.

## Usage

Each adapter directory contains its own README with specific usage instructions and configuration details.

## Documentation

For integration and ingestion documentation, see:
- [Content Pipeline](../docs/content_pipeline.md)
- [Content Architecture](../docs/content_architecture.md)
- [Ingestion Documentation](../docs/ingestion/)
- [VCC-Clara Export API](../docs/api/VCC_CLARA_EXPORT_API.md)
- [Ecosystem Overview](../docs/architecture/ecosystem_overview.md)
