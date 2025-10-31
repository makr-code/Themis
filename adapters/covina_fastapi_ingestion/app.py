import os
from fastapi import FastAPI, UploadFile, File, HTTPException
from fastapi.responses import JSONResponse
from pydantic import BaseModel
from typing import Any, Dict, List, Optional

from themis_client import ThemisClient
from processors.text import process_text_payload

THEMIS_URL = os.getenv("THEMIS_URL", "http://127.0.0.1:8765")
client = ThemisClient(THEMIS_URL)

app = FastAPI(title="Covina → THEMIS Ingestion Adapter", version="0.1.0")


class ContentImport(BaseModel):
    content: Dict[str, Any]
    chunks: List[Dict[str, Any]] = []
    edges: List[Dict[str, Any]] = []
    blob: Optional[str] = None


@app.get("/health")
async def health() -> Dict[str, Any]:
    return {"status": "ok", "themis_url": THEMIS_URL}


@app.post("/ingest/json")
async def ingest_json(body: ContentImport):
    try:
        res = await client.import_content(body.model_dump())
        return JSONResponse(content=res, status_code=200)
    except Exception as e:
        raise HTTPException(status_code=502, detail=f"THEMIS import failed: {e}")


@app.post("/ingest/file")
async def ingest_file(file: UploadFile = File(...)):
    mime = file.content_type or "application/octet-stream"
    try:
        raw = await file.read()
        text: Optional[str] = None
        # Minimal Routing: Nur text/plain direkt, alles andere könnte über weitere Prozessoren laufen
        if mime.startswith("text/"):
            try:
                text = raw.decode("utf-8", errors="ignore")
            except Exception:
                text = raw.decode("latin-1", errors="ignore")
            payload = process_text_payload(text, mime_type=mime, source=file.filename)
        else:
            # Platzhalter: Für andere Modalitäten hier eigenen Processor integrieren
            payload = {
                "content": {"mime_type": mime, "user_metadata": {"source": file.filename}, "tags": ["ingested"]},
                "chunks": [
                    {"seq_num": 0, "chunk_type": "blob_ref", "data": {"note": "binary payload omitted"}}
                ],
                "edges": [],
            }
        res = await client.import_content(payload)
        return JSONResponse(content=res, status_code=200)
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
