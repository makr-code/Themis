import json
from typing import Any, Dict

import httpx
import pytest

from themis import (
    BatchGetResult,
    BatchWriteResult,
    QueryResult,
    ThemisClient,
    TopologyError,
)


def test_refresh_topology_populates_shards() -> None:
    metadata_url = "http://meta.service/topology"

    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL(metadata_url):
            return httpx.Response(
                200,
                json={
                    "version": 1,
                    "shards": [
                        {"id": "s1", "http_endpoint": "http://shard-a:8080"},
                        {"id": "s2", "http_endpoint": "http://shard-b:8080"},
                    ],
                },
            )
        if request.url.host in {"shard-a", "shard-b"}:
            assert request.url.path.startswith("/entities/")
            return httpx.Response(
                200,
                json={
                    "key": request.url.path.split("/entities/")[-1],
                    "blob": json.dumps({"name": "Alice"}),
                },
            )
        raise AssertionError(f"unexpected request: {request.url}")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        metadata_endpoint=metadata_url,
        transport=httpx.MockTransport(handler),
    )

    user_uuid = "550e8400-e29b-41d4-a716-446655440000"
    response = client.get("relational", "users", user_uuid)

    assert response == {"name": "Alice"}
    assert client._topology_cache is not None
    assert client._shard_endpoints == ["http://shard-a:8080", "http://shard-b:8080"]

    client.close()


def test_refresh_topology_raises_on_invalid_payload() -> None:
    metadata_url = "http://meta.service/topology"

    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL(metadata_url):
            return httpx.Response(200, json={"foo": "bar"})
        raise AssertionError("metadata handler should only receive topology fetch")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        metadata_endpoint=metadata_url,
        transport=httpx.MockTransport(handler),
    )

    with pytest.raises(TopologyError):
        client._refresh_topology()

    assert client._shard_endpoints == ["http://bootstrap:8080"]
    client.close()


def test_get_falls_back_when_topology_unavailable() -> None:
    metadata_url = "http://meta.service/topology"
    calls = {"metadata": 0, "entity": 0}

    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL(metadata_url):
            calls["metadata"] += 1
            return httpx.Response(503, json={"error": "temporarily unavailable"})
        if request.url.host == "bootstrap":
            calls["entity"] += 1
            return httpx.Response(
                200,
                json={
                    "key": request.url.path.split("/entities/")[-1],
                    "blob": json.dumps({"name": "Bob"}),
                },
            )
        raise AssertionError(f"unexpected request: {request.url}")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        metadata_endpoint=metadata_url,
        transport=httpx.MockTransport(handler),
        max_retries=1,
    )

    user_uuid = "123e4567-e89b-12d3-a456-426614174000"
    response = client.get("relational", "users", user_uuid)

    assert response == {"name": "Bob"}
    assert calls == {"metadata": 1, "entity": 1}

    client.close()


def test_refresh_topology_uses_default_relative_path() -> None:
    calls: Dict[str, int] = {"metadata": 0, "entity": 0}

    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL("http://bootstrap:8080/_admin/cluster/topology"):
            calls["metadata"] += 1
            return httpx.Response(
                200,
                json={
                    "version": 1,
                    "shards": [
                        {"id": "s1", "http_endpoint": "http://shard-a:8080"}
                    ],
                },
            )
        if request.url.host == "shard-a":
            calls["entity"] += 1
            key = request.url.path.split("/entities/")[-1]
            return httpx.Response(
                200,
                json={"key": key, "blob": json.dumps({"name": "Alice"})},
            )
        raise AssertionError(f"unexpected request: {request.url}")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        transport=httpx.MockTransport(handler),
    )

    entity = client.get("relational", "users", "550e8400-e29b-41d4-a716-446655440000")

    assert entity == {"name": "Alice"}
    assert calls == {"metadata": 1, "entity": 1}

    client.close()


def test_batch_get_handles_missing_and_errors() -> None:
    metadata_url = "http://meta.service/topology"
    store: Dict[str, Dict[str, int]] = {
        "urn:themis:relational:default:users:1": {"value": 1},
        "urn:themis:relational:default:users:2": {"value": 2},
    }

    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL(metadata_url):
            return httpx.Response(
                200,
                json={
                    "version": 1,
                    "shards": [
                        {"id": "s1", "http_endpoint": "http://shard-a:8080"}
                    ],
                },
            )
        if request.url.host == "shard-a":
            key = request.url.path.split("/entities/")[-1]
            urn = f"urn:themis:relational:default:users:{key.split(':')[-1]}"
            if urn in store:
                return httpx.Response(
                    200,
                    json={"key": key, "blob": json.dumps(store[urn])},
                )
            return httpx.Response(404, json={"error": "not found"})
        raise AssertionError(f"unexpected request: {request.url}")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        metadata_endpoint=metadata_url,
        transport=httpx.MockTransport(handler),
        max_workers=4,
    )

    result = client.batch_get("relational", "users", ["1", "2", "9"])

    assert isinstance(result, BatchGetResult)
    assert set(result.found.keys()) == {"1", "2"}
    assert result.missing == ["9"]
    assert result.errors == {}

    client.close()


def test_health_endpoint_returns_status() -> None:
    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL("http://bootstrap:8080/health"):
            return httpx.Response(200, json={"status": "healthy"})
        raise AssertionError(f"unexpected request: {request.url}")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        transport=httpx.MockTransport(handler),
    )

    payload = client.health()

    assert payload == {"status": "healthy"}

    client.close()


def test_batch_put_serializes_payloads() -> None:
    metadata_url = "http://meta.service/topology"
    received: Dict[str, Dict[str, Any]] = {}

    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL(metadata_url):
            return httpx.Response(
                200,
                json={
                    "version": 1,
                    "shards": [
                        {"id": "s1", "http_endpoint": "http://shard-a:8080"}
                    ],
                },
            )
        if request.url.host == "shard-a":
            assert request.method == "PUT"
            key = request.url.path.split("/entities/")[-1]
            payload = json.loads(request.content.decode())
            received[key] = payload
            return httpx.Response(
                201,
                json={"success": True, "key": key, "blob_size": len(payload["blob"])},
            )
        raise AssertionError(f"unexpected request: {request.url}")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        metadata_endpoint=metadata_url,
        transport=httpx.MockTransport(handler),
        max_workers=4,
    )

    result = client.batch_put(
        "relational",
        "users",
        {"1": {"name": "Alice"}, "2": {"name": "Bob"}},
    )

    assert isinstance(result, BatchWriteResult)
    assert set(result.succeeded) == {"1", "2"}
    assert result.failed == {}
    assert all("blob" in payload for payload in received.values())
    # Blob payloads are JSON strings, not dicts
    assert all(isinstance(payload["blob"], str) for payload in received.values())

    client.close()


def test_query_returns_paginated_result() -> None:
    metadata_url = "http://meta.service/topology"

    def handler(request: httpx.Request) -> httpx.Response:
        if request.url == httpx.URL(metadata_url):
            return httpx.Response(
                200,
                json={
                    "version": 1,
                    "shards": [
                        {"id": "s1", "http_endpoint": "http://shard-a:8080"}
                    ],
                },
            )
        if request.url.host == "shard-a":
            body = json.loads(request.content.decode())
            assert body.get("use_cursor") is True
            return httpx.Response(
                200,
                json={
                    "items": [json.dumps({"name": "Alice"})],
                    "has_more": True,
                    "next_cursor": "cursor-123",
                },
            )
        raise AssertionError(f"unexpected request: {request.url}")

    client = ThemisClient(
        ["http://bootstrap:8080"],
        metadata_endpoint=metadata_url,
        transport=httpx.MockTransport(handler),
    )

    result = client.query(
        "FOR u IN users FILTER u._key == 'urn:themis:relational:default:users:1' RETURN u",
        use_cursor=True,
        batch_size=1,
    )

    assert isinstance(result, QueryResult)
    assert result.items == [{"name": "Alice"}]
    assert result.has_more is True
    assert result.next_cursor == "cursor-123"

    client.close()
