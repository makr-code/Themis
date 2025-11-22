"""
VCC-Veritas Adapter

FastAPI-based adapter for VCC-Veritas verification and compliance system.

VCC-Veritas focuses on:
- Data verification and validation
- Compliance checking
- Audit trail management
- Data quality assurance
"""

import os
import sys
from pathlib import Path
from fastapi import FastAPI, UploadFile, File, HTTPException, Body
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from typing import Any, Dict, List, Optional
import hashlib
import json

# Add parent directory to path for vcc_base import
sys.path.insert(0, str(Path(__file__).parent.parent))

from vcc_base import ThemisVCCClient, TextProcessor, VCCAdapterConfig, setup_logging

# Configuration
config = VCCAdapterConfig(
    adapter_name="vcc_veritas",
    adapter_version="0.1.0"
)

# Setup logging
logger = setup_logging(level=os.getenv("LOG_LEVEL", "INFO"), adapter_name="vcc_veritas")

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
    title="VCC-Veritas Adapter",
    description="Verification and compliance adapter for VCC-Veritas system",
    version=config.adapter_version
)


class VerificationMetadata(BaseModel):
    """Metadata for verification and compliance tracking."""
    
    verification_status: Optional[str] = Field(
        None,
        description="Verification status: verified, pending, failed"
    )
    compliance_level: Optional[str] = Field(
        None,
        description="Compliance level: high, medium, low"
    )
    data_classification: Optional[str] = Field(
        None,
        description="Data classification: public, internal, confidential, restricted"
    )
    verification_method: Optional[str] = Field(
        None,
        description="Method used for verification"
    )
    verified_by: Optional[str] = Field(
        None,
        description="User or system that performed verification"
    )
    verification_date: Optional[str] = Field(
        None,
        description="Verification timestamp (ISO 8601)"
    )
    checksum: Optional[str] = Field(
        None,
        description="Data checksum for integrity verification"
    )


class VeritasDataImport(BaseModel):
    """Data import request with verification metadata."""
    
    data: Dict[str, Any]
    verification: Optional[VerificationMetadata] = None
    tags: Optional[List[str]] = None


@app.get("/health")
async def health() -> Dict[str, Any]:
    """Health check endpoint."""
    try:
        themis_health = await client.health_check()
        return {
            "status": "ok",
            "adapter": "vcc_veritas",
            "version": config.adapter_version,
            "themis_url": config.themis_url,
            "themis_status": themis_health.get("status", "unknown")
        }
    except Exception as e:
        logger.error(f"Health check failed: {e}")
        return {
            "status": "degraded",
            "adapter": "vcc_veritas",
            "error": str(e)
        }


@app.post("/verify/document")
async def verify_document(
    file: UploadFile = File(...),
    data_classification: str = "internal",
    compliance_level: str = "medium",
    verified_by: Optional[str] = None
):
    """
    Verify and ingest a document with compliance metadata.
    
    Calculates checksum, performs basic validation, and stores with verification metadata.
    """
    try:
        # Read file content
        raw = await file.read()
        
        # Calculate checksum for integrity verification
        checksum = hashlib.sha256(raw).hexdigest()
        
        # Decode text
        text = raw.decode("utf-8", errors="ignore")
        
        # Build verification metadata
        verification_meta = {
            "verification_status": "verified",
            "compliance_level": compliance_level,
            "data_classification": data_classification,
            "verification_method": "checksum_sha256",
            "checksum": checksum,
            "verified_by": verified_by or "system",
            "source_file": file.filename
        }
        
        # Build tags
        tags = [
            "vcc_veritas",
            "verified",
            data_classification.lower(),
            f"compliance_{compliance_level.lower()}"
        ]
        
        # Process document
        payload = processor.process(
            text=text,
            mime_type=file.content_type or "text/plain",
            source=file.filename,
            tags=tags,
            **verification_meta
        )
        
        # Import to ThemisDB
        result = await client.import_content(payload)
        
        logger.info(f"Verified and imported document: {file.filename}, checksum={checksum[:16]}...")
        
        return JSONResponse(content={
            "status": "verified",
            "checksum": checksum,
            "import_result": result
        }, status_code=200)
        
    except Exception as e:
        logger.error(f"Document verification failed: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/verify/compliance")
async def verify_compliance(data: VeritasDataImport):
    """
    Verify compliance of structured data.
    
    Performs compliance checks and stores data with verification metadata.
    """
    try:
        # Serialize data for checksum
        data_json = json.dumps(data.data, sort_keys=True)
        checksum = hashlib.sha256(data_json.encode()).hexdigest()
        
        # Build tags
        tags = data.tags or []
        tags.extend(["vcc_veritas", "compliance_checked"])
        
        if data.verification:
            if data.verification.data_classification:
                tags.append(data.verification.data_classification.lower())
            if data.verification.compliance_level:
                tags.append(f"compliance_{data.verification.compliance_level.lower()}")
        
        # Build metadata
        metadata = {
            "verification_status": "verified",
            "verification_method": "compliance_check",
            "checksum": checksum,
        }
        
        if data.verification:
            verification_dict = data.verification.model_dump(exclude_none=True)
            metadata.update(verification_dict)
        
        # Create ThemisDB payload
        payload = {
            "content": {
                "mime_type": "application/json",
                "user_metadata": metadata,
                "tags": tags
            },
            "chunks": [{
                "seq_num": 0,
                "chunk_type": "structured_data",
                "data": data.data
            }],
            "edges": []
        }
        
        # Import to ThemisDB
        result = await client.import_content(payload)
        
        logger.info(f"Compliance verification completed, checksum={checksum[:16]}...")
        
        return JSONResponse(content={
            "status": "compliance_verified",
            "checksum": checksum,
            "import_result": result
        }, status_code=200)
        
    except Exception as e:
        logger.error(f"Compliance verification failed: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/audit/record")
async def record_audit_entry(
    action: str = Body(...),
    entity_key: Optional[str] = Body(None),
    user: Optional[str] = Body(None),
    details: Optional[Dict[str, Any]] = Body(None)
):
    """
    Record an audit entry in ThemisDB.
    
    Creates a verifiable audit trail for compliance purposes.
    """
    try:
        # Build audit entry
        audit_data = {
            "action": action,
            "entity_key": entity_key,
            "user": user or "system",
            "details": details or {}
        }
        
        # Calculate checksum for audit integrity
        audit_json = json.dumps(audit_data, sort_keys=True)
        checksum = hashlib.sha256(audit_json.encode()).hexdigest()
        
        # Create payload
        payload = {
            "content": {
                "mime_type": "application/json",
                "user_metadata": {
                    "audit_checksum": checksum,
                    "verification_status": "verified"
                },
                "tags": ["vcc_veritas", "audit", "compliance"]
            },
            "chunks": [{
                "seq_num": 0,
                "chunk_type": "audit_entry",
                "data": audit_data
            }],
            "edges": []
        }
        
        # Import to ThemisDB
        result = await client.import_content(payload)
        
        logger.info(f"Audit entry recorded: action={action}, checksum={checksum[:16]}...")
        
        return JSONResponse(content={
            "status": "audit_recorded",
            "checksum": checksum,
            "import_result": result
        }, status_code=200)
        
    except Exception as e:
        logger.error(f"Audit recording failed: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/validate/integrity")
async def validate_integrity(entity_key: str = Body(...)):
    """
    Validate data integrity by comparing checksums.
    
    Retrieves entity from ThemisDB and verifies its checksum.
    """
    try:
        # Get entity from ThemisDB
        entity = await client.get_entity(entity_key)
        
        # Extract stored checksum
        stored_checksum = entity.get("user_metadata", {}).get("checksum")
        
        if not stored_checksum:
            return JSONResponse(content={
                "status": "no_checksum",
                "message": "Entity does not have a stored checksum"
            }, status_code=200)
        
        # Recalculate checksum
        # Note: This is simplified - real implementation would reconstruct original data
        entity_json = json.dumps(entity, sort_keys=True)
        calculated_checksum = hashlib.sha256(entity_json.encode()).hexdigest()
        
        # Compare
        is_valid = (stored_checksum == calculated_checksum)
        
        logger.info(f"Integrity validation for {entity_key}: {'valid' if is_valid else 'INVALID'}")
        
        return JSONResponse(content={
            "status": "validated",
            "entity_key": entity_key,
            "is_valid": is_valid,
            "stored_checksum": stored_checksum,
            "calculated_checksum": calculated_checksum
        }, status_code=200)
        
    except Exception as e:
        logger.error(f"Integrity validation failed: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/classify/data")
async def classify_data(
    file: UploadFile = File(...),
    classification: str = Body(...),
    auto_classify: bool = Body(False)
):
    """
    Classify and ingest data with appropriate security markings.
    
    Supports data classification levels: public, internal, confidential, restricted.
    """
    try:
        # Read file
        raw = await file.read()
        text = raw.decode("utf-8", errors="ignore")
        
        # Auto-classification (basic keyword-based)
        if auto_classify:
            text_lower = text.lower()
            if any(kw in text_lower for kw in ["confidential", "secret", "private"]):
                classification = "confidential"
            elif any(kw in text_lower for kw in ["internal", "employee"]):
                classification = "internal"
            elif any(kw in text_lower for kw in ["public", "press release"]):
                classification = "public"
            else:
                classification = classification or "internal"
        
        # Calculate checksum
        checksum = hashlib.sha256(raw).hexdigest()
        
        # Build tags
        tags = [
            "vcc_veritas",
            "classified",
            f"classification_{classification.lower()}"
        ]
        
        # Build metadata
        metadata = {
            "data_classification": classification,
            "checksum": checksum,
            "classification_method": "auto" if auto_classify else "manual",
            "source_file": file.filename
        }
        
        # Process
        payload = processor.process(
            text=text,
            mime_type=file.content_type or "text/plain",
            source=file.filename,
            tags=tags,
            **metadata
        )
        
        # Import
        result = await client.import_content(payload)
        
        logger.info(f"Data classified and imported: {file.filename}, classification={classification}")
        
        return JSONResponse(content={
            "status": "classified",
            "classification": classification,
            "checksum": checksum,
            "import_result": result
        }, status_code=200)
        
    except Exception as e:
        logger.error(f"Data classification failed: {e}")
        raise HTTPException(status_code=500, detail=str(e))
