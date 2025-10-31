import os
import httpx
from typing import Any, Dict

THEMIS_URL = os.getenv("THEMIS_URL", "http://127.0.0.1:8765")

class ThemisClient:
    def __init__(self, base_url: str | None = None, timeout_s: float = 30.0) -> None:
        self.base_url = base_url or THEMIS_URL
        self.timeout_s = timeout_s

    async def import_content(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=self.timeout_s) as client:
            r = await client.post(f"{self.base_url}/content/import", json=payload)
            r.raise_for_status()
            return r.json()
