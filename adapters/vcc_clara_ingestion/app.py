"""
VCC-Clara Ingestion Adapter

FastAPI-based ingestion adapter for VCC-Clara system.
Complements the existing VCC-Clara export API documented in docs/api/VCC_CLARA_EXPORT_API.md

This adapter handles:
- Legal document ingestion (Rechtssprechung)
- Environmental law documentation (Immissionsschutz)
- Training data preparation
- Thematic content organization
"""

import os
import sys
from pathlib import Path
from fastapi import FastAPI, UploadFile, File, HTTPException
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from typing import Any, Dict, List, Optional

# Add parent directory to path for vcc_base import
sys.path.insert(0, str(Path(__file__).parent.parent))

from vcc_base import ThemisVCCClient, TextProcessor, VCCAdapterConfig, setup_logging

# Configuration
config = VCCAdapterConfig(
    adapter_name="vcc_clara",
    adapter_version="0.1.0"
)

# Setup logging
logger = setup_logging(level=os.getenv("LOG_LEVEL", "INFO"), adapter_name="vcc_clara")

# Initialize ThemisDB client
client = ThemisVCCClient(
    base_url=config.themis_url,
    namespace=config.themis_namespace,
    auth_token=config.themis_auth_token
)

# Initialize processor
processor = TextProcessor(
    enable_embeddings=config.enable_embeddings,
    chunk_size=config.chunk_size,
    embedding_model=config.embedding_model
)

# FastAPI app
app = FastAPI(
    title="VCC-Clara Ingestion Adapter",
    description="Ingestion adapter for VCC-Clara legal and environmental documentation",
    version=config.adapter_version
)


class ContentMetadata(BaseModel):
    """Metadata for VCC-Clara content."""
    
    theme: Optional[str] = Field(
        None,
        description="Main topic (Rechtssprechung, Immissionsschutz, etc.)"
    )
    domain: Optional[str] = Field(
        None,
        description="Specific domain (environmental_law, labor_law, etc.)"
    )
    subject: Optional[str] = Field(
        None,
        description="Fine-grained subject (immissionsschutz, luftqualität, etc.)"
    )
    source: Optional[str] = Field(
        None,
        description="Document source (BVerwG, TA Luft, etc.)"
    )
    date: Optional[str] = Field(
        None,
        description="Document date (ISO 8601)"
    )
    case_number: Optional[str] = Field(
        None,
        description="Case number for legal documents"
    )
    rating: Optional[float] = Field(
        None,
        ge=0.0,
        le=5.0,
        description="Quality rating (0.0-5.0)"
    )


class ClaraContentImport(BaseModel):
    """Content import request for VCC-Clara."""
    
    content: Dict[str, Any]
    chunks: List[Dict[str, Any]] = []
    edges: List[Dict[str, Any]] = []
    metadata: Optional[ContentMetadata] = None


@app.get("/health")
async def health() -> Dict[str, Any]:
    """Health check endpoint."""
    try:
        themis_health = await client.health_check()
        return {
            "status": "ok",
            "adapter": "vcc_clara",
            "version": config.adapter_version,
            "themis_url": config.themis_url,
            "themis_status": themis_health.get("status", "unknown")
        }
    except Exception as e:
        logger.error(f"Health check failed: {e}")
        return {
            "status": "degraded",
            "adapter": "vcc_clara",
            "error": str(e)
        }


@app.post("/ingest/legal")
async def ingest_legal_document(
    file: UploadFile = File(...),
    theme: str = "Rechtssprechung",
    domain: Optional[str] = None,
    case_number: Optional[str] = None,
    source: Optional[str] = None,
    rating: Optional[float] = None
):
    """
    Ingest legal document (case law, regulations, etc.).
    
    Specialized endpoint for VCC-Clara legal content.
    """
    try:
        # Read file content
        raw = await file.read()
        text = raw.decode("utf-8", errors="ignore")
        
        # Build tags
        tags = ["vcc_clara", "legal", theme.lower()]
        if domain:
            tags.append(domain.lower())
        
        # Build metadata
        metadata = {
            "theme": theme,
            "case_number": case_number,
            "source_file": file.filename
        }
        if domain:
            metadata["domain"] = domain
        if source:
            metadata["source"] = source
        if rating:
            metadata["rating"] = rating
        
        # Process document
        payload = processor.process(
            text=text,
            mime_type=file.content_type or "text/plain",
            source=file.filename,
            tags=tags,
            **metadata
        )
        
        # Import to ThemisDB
        result = await client.import_content(payload)
        
        logger.info(f"Imported legal document: {file.filename}, theme={theme}")
        return JSONResponse(content=result, status_code=200)
        
    except Exception as e:
        logger.error(f"Failed to ingest legal document: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/ingest/environmental")
async def ingest_environmental_document(
    file: UploadFile = File(...),
    subject: str = "immissionsschutz",
    domain: str = "environmental_law",
    source: Optional[str] = None,
    rating: Optional[float] = None
):
    """
    Ingest environmental law documentation.
    
    Specialized endpoint for Immissionsschutz and related content.
    """
    try:
        # Read file content
        raw = await file.read()
        text = raw.decode("utf-8", errors="ignore")
        
        # Build tags
        tags = ["vcc_clara", "environmental", subject.lower(), domain.lower()]
        
        # Build metadata
        metadata = {
            "theme": "Immissionsschutz",
            "domain": domain,
            "subject": subject,
            "source_file": file.filename
        }
        if source:
            metadata["source"] = source
        if rating:
            metadata["rating"] = rating
        
        # Process document
        payload = processor.process(
            text=text,
            mime_type=file.content_type or "text/plain",
            source=file.filename,
            tags=tags,
            **metadata
        )
        
        # Import to ThemisDB
        result = await client.import_content(payload)
        
        logger.info(f"Imported environmental document: {file.filename}, subject={subject}")
        return JSONResponse(content=result, status_code=200)
        
    except Exception as e:
        logger.error(f"Failed to ingest environmental document: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/ingest/file")
async def ingest_file(
    file: UploadFile = File(...),
    theme: Optional[str] = None,
    domain: Optional[str] = None,
    subject: Optional[str] = None,
    tags: Optional[str] = None
):
    """
    Generic file ingestion endpoint.
    
    Handles any text-based document with optional thematic classification.
    """
    try:
        # Read file content
        raw = await file.read()
        mime = file.content_type or "application/octet-stream"
        
        # Handle text content
        if mime.startswith("text/"):
            text = raw.decode("utf-8", errors="ignore")
            
            # Build tags
            tag_list = ["vcc_clara", "ingested"]
            if tags:
                tag_list.extend([t.strip() for t in tags.split(",")])
            if theme:
                tag_list.append(theme.lower())
            
            # Build metadata
            metadata = {"source_file": file.filename}
            if theme:
                metadata["theme"] = theme
            if domain:
                metadata["domain"] = domain
            if subject:
                metadata["subject"] = subject
            
            # Process and import
            payload = processor.process(
                text=text,
                mime_type=mime,
                source=file.filename,
                tags=tag_list,
                **metadata
            )
            
            result = await client.import_content(payload)
            logger.info(f"Imported file: {file.filename}")
            return JSONResponse(content=result, status_code=200)
        else:
            # Binary content - minimal placeholder
            payload = {
                "content": {
                    "mime_type": mime,
                    "user_metadata": {"source_file": file.filename},
                    "tags": ["vcc_clara", "binary"]
                },
                "chunks": [{
                    "seq_num": 0,
                    "chunk_type": "blob_ref",
                    "data": {"note": "binary content not processed"}
                }],
                "edges": []
            }
            
            result = await client.import_content(payload)
            logger.info(f"Imported binary file: {file.filename}")
            return JSONResponse(content=result, status_code=200)
            
    except Exception as e:
        logger.error(f"Failed to ingest file: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/ingest/json")
async def ingest_json(body: ClaraContentImport):
    """
    Direct JSON import endpoint.
    
    Accepts pre-structured ThemisDB payloads with VCC-Clara metadata.
    """
    try:
        # Extract payload
        payload = body.model_dump(exclude={"metadata"})
        
        # Merge Clara-specific metadata if provided
        if body.metadata:
            meta_dict = body.metadata.model_dump(exclude_none=True)
            if "user_metadata" not in payload["content"]:
                payload["content"]["user_metadata"] = {}
            payload["content"]["user_metadata"].update(meta_dict)
        
        # Import to ThemisDB
        result = await client.import_content(payload)
        
        logger.info("Imported JSON content")
        return JSONResponse(content=result, status_code=200)
        
    except Exception as e:
        logger.error(f"Failed to ingest JSON: {e}")
        raise HTTPException(status_code=502, detail=f"ThemisDB import failed: {e}")


@app.post("/batch/legal")
async def batch_ingest_legal(documents: List[Dict[str, Any]]):
    """
    Batch import legal documents.
    
    Efficient bulk ingestion for VCC-Clara legal content.
    """
    try:
        results = []
        errors = []
        
        for doc in documents:
            try:
                text = doc.get("text", "")
                metadata = doc.get("metadata", {})
                
                # Build tags
                tags = ["vcc_clara", "legal", "batch"]
                if "theme" in metadata:
                    tags.append(metadata["theme"].lower())
                
                # Process document
                payload = processor.process(
                    text=text,
                    source=metadata.get("source"),
                    tags=tags,
                    **metadata
                )
                
                # Import
                result = await client.import_content(payload)
                results.append(result)
                
            except Exception as e:
                logger.error(f"Failed to import document in batch: {e}")
                errors.append({"error": str(e), "document": doc.get("source", "unknown")})
        
        logger.info(f"Batch import completed: {len(results)} successful, {len(errors)} errors")
        
        return JSONResponse(content={
            "status": "completed",
            "imported": len(results),
            "errors": len(errors),
            "results": results,
            "error_details": errors
        }, status_code=200)
        
    except Exception as e:
        logger.error(f"Batch import failed: {e}")
        raise HTTPException(status_code=500, detail=str(e))
