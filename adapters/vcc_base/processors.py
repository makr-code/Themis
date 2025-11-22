"""
Data processors for VCC adapters.

Processors transform input data (files, JSON, etc.) into ThemisDB-compatible payloads.
"""

import hashlib
import re
from typing import Any, Dict, List, Optional
from abc import ABC, abstractmethod
import logging

logger = logging.getLogger(__name__)

# Try to import sentence-transformers, fall back to hash embeddings
try:
    from sentence_transformers import SentenceTransformer  # type: ignore
    _EMBEDDING_MODEL: Optional[Any] = None
    
    def _get_embedding_model(model_name: str = "sentence-transformers/all-MiniLM-L6-v2"):
        global _EMBEDDING_MODEL
        if _EMBEDDING_MODEL is None:
            logger.info(f"Loading embedding model: {model_name}")
            _EMBEDDING_MODEL = SentenceTransformer(model_name)
        return _EMBEDDING_MODEL
        
except ImportError:
    logger.warning("sentence-transformers not available, using hash-based embeddings")
    _EMBEDDING_MODEL = None
    
    def _get_embedding_model(model_name: str = ""):
        return None


class BaseProcessor(ABC):
    """Abstract base class for data processors."""
    
    def __init__(self, enable_embeddings: bool = True, embedding_model: str = "sentence-transformers/all-MiniLM-L6-v2"):
        self.enable_embeddings = enable_embeddings
        self.embedding_model_name = embedding_model
        self._model = _get_embedding_model(embedding_model) if enable_embeddings else None
    
    @abstractmethod
    def process(self, data: Any, **kwargs) -> Dict[str, Any]:
        """
        Process input data into ThemisDB payload.
        
        Args:
            data: Input data (varies by processor type)
            **kwargs: Additional processing parameters
            
        Returns:
            ThemisDB-compatible payload with content, chunks, edges
        """
        pass
    
    def _hash_embed(self, text: str, dim: int = 64) -> List[float]:
        """
        Generate deterministic hash-based pseudo-embeddings.
        
        This is a lightweight fallback when sentence-transformers is not available.
        Not semantically meaningful, but allows testing vector operations.
        """
        h = hashlib.sha256(text.encode("utf-8", errors="ignore")).digest()
        vals = list(h) * ((dim + len(h) - 1) // len(h))
        vals = vals[:dim]
        norm = sum(v * v for v in vals) ** 0.5 or 1.0
        return [float(v) / norm for v in vals]
    
    def embed_text(self, text: str) -> Optional[List[float]]:
        """
        Generate embedding for text.
        
        Uses sentence-transformers if available, otherwise hash-based fallback.
        """
        if not self.enable_embeddings:
            return None
            
        if self._model is not None:
            try:
                vec = self._model.encode(text, convert_to_numpy=True)
                return vec.tolist()
            except Exception as e:
                logger.warning(f"Embedding generation failed: {e}, using hash fallback")
                return self._hash_embed(text)
        
        return self._hash_embed(text)


class TextProcessor(BaseProcessor):
    """
    Text data processor.
    
    Handles text files, splitting into chunks and generating embeddings.
    """
    
    def __init__(self, enable_embeddings: bool = True, chunk_size: int = 800, **kwargs):
        super().__init__(enable_embeddings, **kwargs)
        self.chunk_size = chunk_size
    
    def chunk_text(self, text: str) -> List[str]:
        """
        Split text into semantic chunks.
        
        Uses sentence boundaries for natural splits.
        """
        # Split into sentences (simple regex-based approach)
        sentences = re.split(r"(?<=[.!?])\s+", text.strip())
        
        chunks: List[str] = []
        buffer: List[str] = []
        current_size = 0
        
        for sentence in sentences:
            sentence_len = len(sentence)
            
            if current_size + sentence_len > self.chunk_size and buffer:
                # Flush current buffer
                chunks.append(" ".join(buffer))
                buffer = [sentence]
                current_size = sentence_len
            else:
                buffer.append(sentence)
                current_size += sentence_len
        
        # Flush remaining
        if buffer:
            chunks.append(" ".join(buffer))
        
        # Handle edge case: empty or very short text
        if not chunks and text:
            chunks = [text]
        
        return chunks
    
    def process(
        self,
        text: str,
        mime_type: str = "text/plain",
        source: Optional[str] = None,
        tags: Optional[List[str]] = None,
        **kwargs
    ) -> Dict[str, Any]:
        """
        Process text into ThemisDB content payload.
        
        Args:
            text: Input text content
            mime_type: MIME type of the content
            source: Optional source identifier (filename, URL, etc.)
            tags: Optional tags for categorization
            **kwargs: Additional metadata
            
        Returns:
            ThemisDB content import payload
        """
        text_chunks = self.chunk_text(text)
        
        chunks: List[Dict[str, Any]] = []
        for i, chunk_text in enumerate(text_chunks):
            emb = self.embed_text(chunk_text)
            
            chunk: Dict[str, Any] = {
                "seq_num": i,
                "chunk_type": "text",
                "text": chunk_text,
            }
            
            if emb is not None:
                chunk["embedding"] = emb
            
            chunks.append(chunk)
        
        # Build content metadata
        user_metadata: Dict[str, Any] = {}
        if source:
            user_metadata["source"] = source
        user_metadata.update(kwargs)
        
        # Build tags
        content_tags = tags or []
        if "ingested" not in content_tags:
            content_tags.append("ingested")
        if "text" not in content_tags:
            content_tags.append("text")
        
        payload: Dict[str, Any] = {
            "content": {
                "mime_type": mime_type,
                "user_metadata": user_metadata,
                "tags": content_tags,
            },
            "chunks": chunks,
            "edges": [],
        }
        
        return payload
