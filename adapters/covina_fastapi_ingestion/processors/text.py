import hashlib
import os
from typing import Any, Dict, List

ENABLE_EMBEDDINGS = os.getenv("ENABLE_EMBEDDINGS", "true").lower() == "true"

try:
    from sentence_transformers import SentenceTransformer  # type: ignore
    _MODEL: Any | None = SentenceTransformer("sentence-transformers/all-MiniLM-L6-v2")
except Exception:
    _MODEL = None


def _hash_embed(text: str, dim: int = 64) -> List[float]:
    # Leichte, deterministische Fallback-Embeddings (keine Semantik, nur Demo!)
    h = hashlib.sha256(text.encode("utf-8", errors="ignore")).digest()
    vals = list(h) * ((dim + len(h) - 1) // len(h))
    vals = vals[:dim]
    norm = sum(v * v for v in vals) ** 0.5 or 1.0
    return [float(v) / norm for v in vals]


def embed_text(text: str) -> List[float] | None:
    if not ENABLE_EMBEDDINGS:
        return None
    if _MODEL is not None:
        try:
            vec = _MODEL.encode(text, convert_to_numpy=True)
            return vec.tolist()
        except Exception:
            # Fallback auf Hash-Embeddings, wenn das Modell versagt
            return _hash_embed(text)
    return _hash_embed(text)


def chunk_text(text: str, max_len: int = 800) -> List[str]:
    # Sehr einfache Chunking-Strategie: Sätze grob trennen und packen
    import re
    sentences = re.split(r"(?<=[.!?])\s+", text.strip())
    chunks: List[str] = []
    buf: List[str] = []
    size = 0
    for s in sentences:
        if size + len(s) > max_len and buf:
            chunks.append(" ".join(buf))
            buf = [s]
            size = len(s)
        else:
            buf.append(s)
            size += len(s)
    if buf:
        chunks.append(" ".join(buf))
    if not chunks and text:
        chunks = [text]
    return chunks


def process_text_payload(text: str, mime_type: str = "text/plain", source: str | None = None) -> Dict[str, Any]:
    chunks_text = chunk_text(text)
    chunks: List[Dict[str, Any]] = []
    for i, c in enumerate(chunks_text):
        emb = embed_text(c)
        chunk: Dict[str, Any] = {
            "seq_num": i,
            "chunk_type": "text",
            "text": c,
        }
        if emb is not None:
            chunk["embedding"] = emb
        chunks.append(chunk)
    payload: Dict[str, Any] = {
        "content": {
            "mime_type": mime_type,
            "user_metadata": {"source": source} if source else {},
            "tags": ["ingested", "text"],
        },
        "chunks": chunks,
        "edges": [],
    }
    return payload
