"""ThemisDB Python SDK with topology-aware routing and batch helpers."""

from __future__ import annotations

import json
import hashlib
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Optional, Sequence

import httpx

__all__ = [
    "ThemisClient",
    "TopologyError",
    "QueryResult",
    "BatchGetResult",
    "BatchWriteResult",
    "__version__",
]

__version__ = "0.1.0a0"

_DEFAULT_METADATA_PATH = "/_admin/cluster/topology"
_HEALTH_PATH = "/health"


class TopologyError(RuntimeError):
    """Raised when shard topology cannot be resolved."""


@dataclass
class QueryResult:
    items: List[Any]
    has_more: bool
    next_cursor: Optional[str]
    raw: Dict[str, Any]
    count: Optional[int] = None
    table: Optional[str] = None


@dataclass
class BatchGetResult:
    found: Dict[str, Any]
    missing: List[str]
    errors: Dict[str, str]


@dataclass
class BatchWriteResult:
    succeeded: List[str]
    failed: Dict[str, str]


def _normalize_endpoint(endpoint: str) -> str:
    return endpoint.rstrip("/")


def _stable_hash(value: str) -> int:
    digest = hashlib.blake2b(value.encode("utf-8"), digest_size=4).digest()
    return int.from_bytes(digest, "big")


def _extract_endpoints(payload: Dict[str, Any]) -> List[str]:
    shards = payload.get("shards")
    if not isinstance(shards, Iterable):
        return []

    result: List[str] = []
    for shard in shards:
        if isinstance(shard, str):
            normalized = _normalize_endpoint(shard)
            if normalized not in result:
                result.append(normalized)
            continue
        if not isinstance(shard, dict):
            continue
        candidates: List[str] = []
        if "endpoint" in shard:
            candidates.append(str(shard["endpoint"]))
        if "http_endpoint" in shard:
            candidates.append(str(shard["http_endpoint"]))
        if "endpoints" in shard and isinstance(shard["endpoints"], Iterable):
            for item in shard["endpoints"]:
                if isinstance(item, str):
                    candidates.append(item)
        for candidate in candidates:
            normalized = _normalize_endpoint(candidate)
            if normalized not in result:
                result.append(normalized)
    return result


def _decode_blob(blob: Any) -> Any:
    if isinstance(blob, str):
        try:
            return json.loads(blob)
        except ValueError:
            return blob
    return blob


def _encode_blob(payload: Any) -> str:
    if isinstance(payload, str):
        return payload
    return json.dumps(payload)


class ThemisClient:
    """Python client for ThemisDB."""

    def __init__(
        self,
        endpoints: Sequence[str],
        *,
        namespace: str = "default",
        timeout: float = 30.0,
        max_retries: int = 3,
        metadata_endpoint: Optional[str] = None,
        metadata_path: str = _DEFAULT_METADATA_PATH,
        max_workers: Optional[int] = None,
        transport: Optional[httpx.BaseTransport] = None,
    ) -> None:
        if not endpoints:
            raise ValueError("endpoints must not be empty")
        if max_retries < 1:
            raise ValueError("max_retries must be >= 1")

        self.namespace = namespace
        self.timeout = timeout
        self.max_retries = max_retries
        self.metadata_endpoint = metadata_endpoint
        self._metadata_path = metadata_path or _DEFAULT_METADATA_PATH
        self._max_workers = max_workers
        self._transport = transport

        self.endpoints = [_normalize_endpoint(ep) for ep in endpoints]
        self._shard_endpoints = list(self.endpoints)
        self._topology_cache: Optional[Dict[str, Any]] = None

        self._http_client = httpx.Client(
            timeout=self.timeout,
            transport=transport,
            headers={"User-Agent": "themis-python-sdk/0.1"},
        )

    def close(self) -> None:
        self._http_client.close()

    def __enter__(self) -> "ThemisClient":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def get(self, model: str, collection: str, uuid: str) -> Optional[Any]:
        urn = self._build_urn(model, collection, uuid)
        key = self._build_entity_key(model, collection, uuid)
        endpoint = self._resolve_endpoint(urn)
        response = self._request("GET", f"{endpoint}/entities/{key}")
        if response.status_code == 404:
            return None
        response.raise_for_status()
        payload = response.json()
        if "entity" in payload:
            # decrypt=true payload
            return payload.get("entity")
        blob = payload.get("blob")
        return _decode_blob(blob)

    def put(self, model: str, collection: str, uuid: str, data: Any) -> bool:
        urn = self._build_urn(model, collection, uuid)
        key = self._build_entity_key(model, collection, uuid)
        endpoint = self._resolve_endpoint(urn)
        body = {"blob": _encode_blob(data)}
        response = self._request("PUT", f"{endpoint}/entities/{key}", json=body)
        if response.status_code in (200, 201):
            return True
        response.raise_for_status()
        return False

    def delete(self, model: str, collection: str, uuid: str) -> bool:
        urn = self._build_urn(model, collection, uuid)
        key = self._build_entity_key(model, collection, uuid)
        endpoint = self._resolve_endpoint(urn)
        response = self._request("DELETE", f"{endpoint}/entities/{key}")
        if response.status_code == 200:
            return True
        if response.status_code == 404:
            return False
        response.raise_for_status()
        return False

    def health(self, endpoint: Optional[str] = None) -> Dict[str, Any]:
        target = _normalize_endpoint(endpoint or self.endpoints[0])
        response = self._request("GET", f"{target}{_HEALTH_PATH}")
        response.raise_for_status()
        return response.json()

    def batch_get(self, model: str, collection: str, uuids: Sequence[str], *, raise_on_error: bool = False) -> BatchGetResult:
        result = BatchGetResult(found={}, missing=[], errors={})
        if not uuids:
            return result
        if self._should_parallelize(len(uuids)):
            with ThreadPoolExecutor(max_workers=self._batch_worker_count(len(uuids))) as executor:
                future_map = {
                    executor.submit(self.get, model, collection, uuid): uuid
                    for uuid in uuids
                }
                for future in as_completed(future_map):
                    uuid = future_map[future]
                    try:
                        entity = future.result()
                    except Exception as exc:  # pragma: no cover - surfaced via tests
                        if raise_on_error:
                            raise
                        result.errors[uuid] = str(exc)
                    else:
                        if entity is None:
                            result.missing.append(uuid)
                        else:
                            result.found[uuid] = entity
        else:
            for uuid in uuids:
                try:
                    entity = self.get(model, collection, uuid)
                except Exception as exc:  # pragma: no cover - sequential path
                    if raise_on_error:
                        raise
                    result.errors[uuid] = str(exc)
                else:
                    if entity is None:
                        result.missing.append(uuid)
                    else:
                        result.found[uuid] = entity
        return result

    def batch_put(
        self,
        model: str,
        collection: str,
        items: Dict[str, Any],
        *,
        raise_on_error: bool = False,
    ) -> BatchWriteResult:
        result = BatchWriteResult(succeeded=[], failed={})
        if not items:
            return result
        if self._should_parallelize(len(items)):
            with ThreadPoolExecutor(max_workers=self._batch_worker_count(len(items))) as executor:
                future_map = {
                    executor.submit(self.put, model, collection, uuid, payload): uuid
                    for uuid, payload in items.items()
                }
                for future in as_completed(future_map):
                    uuid = future_map[future]
                    try:
                        status = future.result()
                    except Exception as exc:
                        if raise_on_error:
                            raise
                        result.failed[uuid] = str(exc)
                    else:
                        if status:
                            result.succeeded.append(uuid)
                        else:
                            result.failed[uuid] = "operation returned False"
        else:
            for uuid, payload in items.items():
                try:
                    status = self.put(model, collection, uuid, payload)
                except Exception as exc:
                    if raise_on_error:
                        raise
                    result.failed[uuid] = str(exc)
                else:
                    if status:
                        result.succeeded.append(uuid)
                    else:
                        result.failed[uuid] = "operation returned False"
        return result

    def batch_delete(
        self,
        model: str,
        collection: str,
        uuids: Sequence[str],
        *,
        raise_on_error: bool = False,
    ) -> BatchWriteResult:
        result = BatchWriteResult(succeeded=[], failed={})
        if not uuids:
            return result
        if self._should_parallelize(len(uuids)):
            with ThreadPoolExecutor(max_workers=self._batch_worker_count(len(uuids))) as executor:
                future_map = {
                    executor.submit(self.delete, model, collection, uuid): uuid
                    for uuid in uuids
                }
                for future in as_completed(future_map):
                    uuid = future_map[future]
                    try:
                        status = future.result()
                    except Exception as exc:
                        if raise_on_error:
                            raise
                        result.failed[uuid] = str(exc)
                    else:
                        if status:
                            result.succeeded.append(uuid)
                        else:
                            result.failed[uuid] = "operation returned False"
        else:
            for uuid in uuids:
                try:
                    status = self.delete(model, collection, uuid)
                except Exception as exc:
                    if raise_on_error:
                        raise
                    result.failed[uuid] = str(exc)
                else:
                    if status:
                        result.succeeded.append(uuid)
                    else:
                        result.failed[uuid] = "operation returned False"
        return result

    def query(
        self,
        aql: str,
        *,
        params: Optional[Dict[str, Any]] = None,
        use_cursor: bool = False,
        cursor: Optional[str] = None,
        batch_size: Optional[int] = None,
    ) -> QueryResult:
        payload: Dict[str, Any] = {"query": aql}
        if params:
            payload["params"] = params
        if use_cursor:
            payload["use_cursor"] = True
        if cursor:
            payload["cursor"] = cursor
        if batch_size is not None:
            payload["batch_size"] = batch_size

        endpoints = (
            [self._resolve_query_endpoint(aql)]
            if self._is_single_shard_query(aql)
            else list(self._current_endpoints())
        )

        partials: List[QueryResult] = []
        for endpoint in endpoints:
            response = self._request("POST", f"{endpoint}/query/aql", json=payload)
            response.raise_for_status()
            data = response.json()
            partials.append(self._parse_query_payload(data))

        if not partials:
            return QueryResult(items=[], has_more=False, next_cursor=None, raw={})
        if len(partials) == 1:
            return partials[0]

        merged_items: List[Any] = []
        any_has_more = False
        for part in partials:
            merged_items.extend(part.items)
            any_has_more = any_has_more or part.has_more
        raw = {"partials": [part.raw for part in partials]}
        return QueryResult(items=merged_items, has_more=any_has_more, next_cursor=None, raw=raw)

    def graph_traverse(self, start_node: str, max_depth: int = 3, edge_type: Optional[str] = None) -> List[str]:
        endpoint = self._resolve_endpoint(start_node)
        response = self._request(
            "POST",
            f"{endpoint}/graph/traverse",
            json={"start": start_node, "max_depth": max_depth, "edge_type": edge_type},
        )
        response.raise_for_status()
        payload = response.json()
        if "nodes" in payload:
            return payload.get("nodes", [])
        if "visited" in payload:
            return payload.get("visited", [])
        return []

    def vector_search(
        self,
        embedding: List[float],
        top_k: int = 10,
        metadata_filter: Optional[Dict[str, Any]] = None,
        *,
        use_cursor: bool = False,
        cursor: Optional[str] = None,
    ) -> Dict[str, Any]:
        request_body: Dict[str, Any] = {"vector": embedding, "k": top_k}
        if metadata_filter is not None:
            request_body["filter"] = metadata_filter
        if use_cursor:
            request_body["use_cursor"] = True
        if cursor:
            request_body["cursor"] = cursor

        responses: List[Dict[str, Any]] = []
        for endpoint in self._current_endpoints():
            response = self._request("POST", f"{endpoint}/vector/search", json=request_body)
            if response.status_code == 200:
                responses.append(response.json())
        if not responses:
            return {"results": []}
        if len(responses) == 1:
            return responses[0]

        merged_hits: List[Dict[str, Any]] = []
        for payload in responses:
            if "results" in payload:
                merged_hits.extend(payload.get("results", []))
            elif "items" in payload:
                merged_hits.extend(payload.get("items", []))
        merged_hits.sort(key=lambda item: item.get("score") or item.get("distance", 0), reverse=True)
        return {
            "results": merged_hits[:top_k],
            "partials": responses,
        }

    def _current_endpoints(self) -> List[str]:
        return self._shard_endpoints or self.endpoints

    def _metadata_url(self) -> str:
        if self.metadata_endpoint:
            if self.metadata_endpoint.startswith("http"):
                return self.metadata_endpoint
            bootstrap = self.endpoints[0]
            return f"{bootstrap}{self.metadata_endpoint}"
        bootstrap = self.endpoints[0]
        return f"{bootstrap}{self._metadata_path}"

    def _refresh_topology(self) -> None:
        try:
            response = self._request("GET", self._metadata_url())
        except httpx.HTTPError as exc:
            raise TopologyError("failed to fetch shard topology") from exc
        try:
            payload = response.json()
        except ValueError as exc:
            raise TopologyError("invalid topology payload") from exc
        endpoints = _extract_endpoints(payload)
        if not endpoints:
            raise TopologyError("no shard endpoints found in topology response")
        self._topology_cache = payload
        self._shard_endpoints = endpoints

    def _resolve_endpoint(self, urn: str) -> str:
        self._ensure_topology()
        endpoints = self._current_endpoints()
        if not endpoints:
            raise TopologyError("no endpoints available for request")
        index = _stable_hash(urn) % len(endpoints)
        return endpoints[index]

    def _resolve_query_endpoint(self, aql: str) -> str:
        self._ensure_topology()
        endpoints = self._current_endpoints()
        if not endpoints:
            raise TopologyError("no endpoints available for query")
        index = _stable_hash(aql) % len(endpoints)
        return endpoints[index]

    def _request(self, method: str, url: str, **kwargs: Any) -> httpx.Response:
        last_error: Optional[httpx.HTTPError] = None
        for attempt in range(1, self.max_retries + 1):
            try:
                response = self._http_client.request(method, url, **kwargs)
            except httpx.HTTPError as exc:
                last_error = exc
            else:
                if response.status_code >= 500:
                    if attempt == self.max_retries:
                        response.raise_for_status()
                    continue
                return response
        if last_error is not None:
            raise last_error
        raise TopologyError("request failed without specific error")

    def _parse_query_payload(self, payload: Dict[str, Any]) -> QueryResult:
        if "entities" in payload:
            items = [_decode_blob(entry) for entry in payload.get("entities", [])]
            return QueryResult(
                items=items,
                has_more=False,
                next_cursor=None,
                raw=payload,
                count=payload.get("count"),
                table=payload.get("table"),
            )
        if "items" in payload:
            items = [_decode_blob(entry) for entry in payload.get("items", [])]
            return QueryResult(
                items=items,
                has_more=bool(payload.get("has_more")),
                next_cursor=payload.get("next_cursor"),
                raw=payload,
                table=payload.get("table"),
            )
        return QueryResult(items=[], has_more=False, next_cursor=None, raw=payload)

    def _batch_worker_count(self, task_count: int) -> int:
        if task_count <= 0:
            return 1
        if self._max_workers is not None:
            return max(1, min(self._max_workers, task_count))
        return max(1, min(4, task_count))

    def _should_parallelize(self, task_count: int) -> bool:
        if task_count <= 1:
            return False
        if self._transport is not None:
            return False
        return self._batch_worker_count(task_count) > 1

    def _is_single_shard_query(self, aql: str) -> bool:
        return "urn:themis:" in aql.lower()

    def _build_urn(self, model: str, collection: str, uuid: str) -> str:
        return f"urn:themis:{model}:{self.namespace}:{collection}:{uuid}"

    def _build_entity_key(self, model: str, collection: str, uuid: str) -> str:
        table = f"{model}.{self.namespace}.{collection}"
        return f"{table}:{uuid}"

    def _ensure_topology(self) -> None:
        if self._topology_cache is not None:
            return
        try:
            self._refresh_topology()
        except TopologyError:
            self._shard_endpoints = list(self.endpoints)
