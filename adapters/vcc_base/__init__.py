"""
VCC Base Adapter Library

Common functionality shared across all VCC (Virtual Compliance Center) adapters
for connecting to ThemisDB backend.

This library provides:
- ThemisDB HTTP client wrapper
- Common data transformation utilities
- Shared configuration management
- Error handling and logging utilities

NO UDS3 FRAMEWORK DEPENDENCY - Direct HTTP connections to ThemisDB.
"""

from .themis_client import ThemisVCCClient
from .config import VCCAdapterConfig
from .processors import BaseProcessor, TextProcessor
from .utils import setup_logging, validate_themis_connection

__version__ = "0.1.0"
__all__ = [
    "ThemisVCCClient",
    "VCCAdapterConfig", 
    "BaseProcessor",
    "TextProcessor",
    "setup_logging",
    "validate_themis_connection",
]
