# ThemisDB Python SDK Quickstart

_Stand: 10. November 2025_

Dieses Dokument fasst die wichtigsten Schritte zusammen, um die Python-Clientbibliothek gegen eine ThemisDB-Instanz zu verwenden. Die SDK befindet sich noch im Alpha-Status (`themis.__version__ == "0.1.0a0"`). Breaking Changes sind jederzeit möglich.

## Voraussetzungen

- Python ≥ 3.11
- Laufende ThemisDB-Instanz (lokal oder Remote, z. B. via `docker-compose`)
- Zugriff auf mindestens einen HTTP-Endpunkt der Cluster-Knoten (z. B. `http://127.0.0.1:8765`)
- Optional: Zugriff auf den Metadaten-Service (z. B. etcd), falls der Topology-Endpunkt nicht über die Knoten selbst erreichbar ist

## Installation

| Ziel | Befehl |
| --- | --- |
| Entwicklung im Repo | `pip install -e clients/python` |
| Reine Nutzung (z. B. CI) | `pip install clients/python` (lokales Artefakt; Veröffentlichung in Paketindex steht noch aus) |

Stellen Sie sicher, dass die Abhängigkeit `httpx>=0.26` verfügbar ist (wird über `pyproject.toml` installiert).

## Minimalbeispiel

```python
from themis import ThemisClient

client = ThemisClient(
    endpoints=["http://127.0.0.1:8765"],
    namespace="default",
    metadata_endpoint="/_admin/cluster/topology",  # optional: vollständige URL möglich
)

print(client.health())  # {'status': 'healthy', 'version': '0.1.0', ...}
client.close()
```

## Konfiguration & Topologie

| Parameter | Beschreibung |
| --- | --- |
| `endpoints` | Liste von HTTP-Basen (z. B. `http://shard-1:8080`) – dient als Bootstrap für Requests. |
| `metadata_endpoint` | Relative Pfadangabe (Default `/_admin/cluster/topology`) **oder** vollständige URL zu etcd/Consul. Bei Fehlschlag erfolgt ein Fallback auf die initiale Endpunktliste. |
| `timeout` | HTTP Timeouts in Sekunden (Default 30 s). |
| `max_retries` | Anzahl der Retries für 5xx-Fehler (Default 3). |
| `max_workers` | Obergrenze für parallele Batch-Requests (Default min(4, Anzahl Tasks)). |

Der Client lädt beim ersten Request die Shard-Topologie. Ist weder ein Pfad noch eine vollständige URL verfügbar, wird mit der Bootstrap-Liste weitergearbeitet. Fehler im Topologie-Fetch lösen `TopologyError` aus.

## CRUD & Batch-Operationen

```python
user_id = "550e8400-e29b-41d4-a716-446655440000"

client.put("relational", "users", user_id, {"name": "Alice"})
user = client.get("relational", "users", user_id)
client.delete("relational", "users", user_id)

batch = client.batch_get("relational", "users", ["1", "2", "999"])
print(batch.found)    # {'1': {...}, '2': {...}}
print(batch.missing)  # ['999']
print(batch.errors)   # {} oder Fehler pro UUID

client.batch_put("relational", "users", {
    "1": {"name": "Alice"},
    "2": {"name": "Bob"},
})
```

**Hinweis:** Bei Verwendung eines `httpx.MockTransport` (Tests) schaltet das SDK automatisch auf sequenzielle Batch-Verarbeitung um, um parallele Aufrufe innerhalb des Mocks zu vermeiden.

## Cursor-AQL & Scatter-Gather

```python
page = client.query(
    "FOR u IN users RETURN u",
    use_cursor=True,
    batch_size=100,
)

while page.has_more:
    page = client.query(
        "FOR u IN users RETURN u",
        use_cursor=True,
        cursor=page.next_cursor,
    )
```

Das SDK erkennt automatisch, ob die Query URN-basiert ist (`urn:themis:`) und damit in einen Single-Shard-Plan fällt. Ansonsten erfolgt Scatter-Gather über alle bekannten Endpunkte. Die Rückgabe `QueryResult` enthält sowohl legacy- (`entities`) als auch Cursor-Felder (`items`, `has_more`, `next_cursor`).

## Spezielle Endpunkte

- `health()` → `/health` (Status, Version, Laufzeit)
- `graph_traverse()` → `/graph/traverse`
- `vector_search()` → `/vector/search` (unterstützt `cursor` & `use_cursor`)

## Fehlerbehandlung

- Topologieprobleme → `TopologyError`
- HTTP-Fehler ≥ 400 → `httpx.HTTPStatusError`
- Netzwerkausfälle → Exceptions aus `httpx`

Empfohlen: Aufrufer fangen `TopologyError`, um ggf. Retry-Logik oder alternative Endpunkte zu verwenden.

## Tests & Qualitätssicherung

```bash
pytest clients/python/tests
```

Die Tests verwenden `httpx.MockTransport`, um Topologie-Fetch, Batch-Fallbacks und Cursor-Flows abzudecken. Integrationstests gegen `docker-compose` sind vorgesehen, sobald die API-Stabilität erreicht ist.

## Roadmap

- Verpackung & Release-Prozess (z. B. `build`/`twine` Pipeline)
- Authentifizierungs- und Secrets-Management
- Cursor-Pagination für COLLECT/Cursor-Routen, sobald Server-Seite verfügbar
- Beispiel-Notebooks & CLI-Wrapper
