"""
Configuration management for VCC adapters.
"""

import os
from typing import Optional
from pydantic import BaseModel, Field


class VCCAdapterConfig(BaseModel):
    """
    Base configuration for VCC adapters.
    
    All VCC adapters (Covina, Clara, Veritas) inherit from this.
    """
    
    # ThemisDB connection settings
    themis_url: str = Field(
        default_factory=lambda: os.getenv("THEMIS_URL", "http://127.0.0.1:8765"),
        description="ThemisDB base URL"
    )
    themis_namespace: str = Field(
        default="default",
        description="ThemisDB namespace for data isolation"
    )
    themis_auth_token: Optional[str] = Field(
        default_factory=lambda: os.getenv("THEMIS_AUTH_TOKEN"),
        description="Optional JWT authentication token"
    )
    themis_timeout: float = Field(
        default=30.0,
        description="HTTP request timeout in seconds"
    )
    
    # Embedding settings (shared across adapters)
    enable_embeddings: bool = Field(
        default_factory=lambda: os.getenv("ENABLE_EMBEDDINGS", "true").lower() == "true",
        description="Enable embedding generation for vector search"
    )
    embedding_model: str = Field(
        default="sentence-transformers/all-MiniLM-L6-v2",
        description="Sentence transformer model for embeddings"
    )
    embedding_dim: int = Field(
        default=384,
        description="Embedding dimension (depends on model)"
    )
    
    # Processing settings
    chunk_size: int = Field(
        default=800,
        description="Maximum chunk size for text splitting"
    )
    batch_size: int = Field(
        default=100,
        description="Batch size for bulk operations"
    )
    
    # Adapter-specific metadata
    adapter_name: str = Field(
        default="vcc_base",
        description="Name of the VCC adapter (covina, clara, veritas, etc.)"
    )
    adapter_version: str = Field(
        default="0.1.0",
        description="Adapter version"
    )
    
    class Config:
        """Pydantic configuration."""
        env_file = ".env"
        env_file_encoding = "utf-8"
