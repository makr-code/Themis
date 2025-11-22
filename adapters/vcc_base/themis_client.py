"""
ThemisDB HTTP Client for VCC Adapters

Provides a unified HTTP client for all VCC adapters to interact with ThemisDB.
Supports all ThemisDB APIs: content import, query, vector search, etc.

Uses direct HTTP connections - no external frameworks required.
"""

import os
import httpx
from typing import Any, Dict, List, Optional
import logging

logger = logging.getLogger(__name__)


class ThemisVCCClient:
    """
    HTTP client for ThemisDB with VCC-specific conveniences.
    
    This client provides direct access to ThemisDB without any external framework dependencies.
    """
    
    def __init__(
        self, 
        base_url: Optional[str] = None,
        timeout_s: float = 30.0,
        namespace: str = "default",
        auth_token: Optional[str] = None
    ) -> None:
        """
        Initialize ThemisDB client.
        
        Args:
            base_url: ThemisDB base URL (default from THEMIS_URL env)
            timeout_s: Request timeout in seconds
            namespace: ThemisDB namespace for data isolation
            auth_token: Optional JWT token for authenticated requests
        """
        self.base_url = base_url or os.getenv("THEMIS_URL", "http://127.0.0.1:8765")
        self.timeout_s = timeout_s
        self.namespace = namespace
        self.auth_token = auth_token or os.getenv("THEMIS_AUTH_TOKEN")
        
        logger.info(f"Initialized ThemisVCCClient for {self.base_url}")
    
    def _get_headers(self) -> Dict[str, str]:
        """Build request headers with optional authentication."""
        headers = {"Content-Type": "application/json"}
        if self.auth_token:
            headers["Authorization"] = f"Bearer {self.auth_token}"
        if self.namespace and self.namespace != "default":
            headers["X-Themis-Namespace"] = self.namespace
        return headers
    
    async def health_check(self) -> Dict[str, Any]:
        """Check ThemisDB health status."""
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.get(f"{self.base_url}/health")
            r.raise_for_status()
            return r.json()
    
    async def import_content(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        """
        Import content into ThemisDB using /content/import endpoint.
        
        Args:
            payload: Content import payload with content, chunks, edges
            
        Returns:
            Import response with content_id and status
        """
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.post(
                f"{self.base_url}/content/import",
                json=payload,
                headers=self._get_headers()
            )
            r.raise_for_status()
            return r.json()
    
    async def query_aql(self, query: str, bind_vars: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Execute AQL query against ThemisDB.
        
        Args:
            query: AQL query string
            bind_vars: Optional query bind variables
            
        Returns:
            Query results
        """
        payload = {"query": query}
        if bind_vars:
            payload["bind_vars"] = bind_vars
            
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.post(
                f"{self.base_url}/query",
                json=payload,
                headers=self._get_headers()
            )
            r.raise_for_status()
            return r.json()
    
    async def vector_search(
        self,
        embedding: List[float],
        k: int = 10,
        filter_expr: Optional[str] = None
    ) -> Dict[str, Any]:
        """
        Perform vector similarity search.
        
        Args:
            embedding: Query vector embedding
            k: Number of nearest neighbors to return
            filter_expr: Optional AQL filter expression
            
        Returns:
            Search results with entities and distances
        """
        payload = {
            "embedding": embedding,
            "k": k
        }
        if filter_expr:
            payload["filter"] = filter_expr
            
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.post(
                f"{self.base_url}/vector/search",
                json=payload,
                headers=self._get_headers()
            )
            r.raise_for_status()
            return r.json()
    
    async def get_entity(self, key: str) -> Dict[str, Any]:
        """Get entity by key."""
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.get(
                f"{self.base_url}/entities/{key}",
                headers=self._get_headers()
            )
            r.raise_for_status()
            return r.json()
    
    async def put_entity(self, key: str, data: Dict[str, Any]) -> Dict[str, Any]:
        """Create or update entity."""
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.put(
                f"{self.base_url}/entities/{key}",
                json=data,
                headers=self._get_headers()
            )
            r.raise_for_status()
            return r.json()
    
    async def delete_entity(self, key: str) -> Dict[str, Any]:
        """Delete entity by key."""
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.delete(
                f"{self.base_url}/entities/{key}",
                headers=self._get_headers()
            )
            r.raise_for_status()
            return r.json()
    
    async def batch_import(self, entities: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Batch import multiple entities.
        
        Args:
            entities: List of entity data dictionaries
            
        Returns:
            Batch import results
        """
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.post(
                f"{self.base_url}/entities/batch",
                json={"entities": entities},
                headers=self._get_headers()
            )
            r.raise_for_status()
            return r.json()
