# OpenAPI & Endpunkte

Die vollständige API-Spezifikation liegt als YAML vor:

- `docs/openapi.yaml` (aktuell)
- optional: `openapi/openapi.yaml` (alternativ, ggf. konsolidieren)

Aktualisierungen anstehend:
- Aufnahme der neuen Endpunkte für Keys, Classification, Reports (bereits in `docs/openapi.yaml` ergänzt)
- Beispiele mit Request/Response und Fehlercodes
- Hinweis: Das SSE-Streaming `GET /changefeed/stream` ist nicht Teil der OpenAPI (Content-Type `text/event-stream`). Details siehe „Change Data Capture (CDC)“.

Hinweis: Für eine gerenderte Ansicht (Swagger/Redoc) kann später ein MkDocs-Plugin ergänzt werden. Bis dahin bitte die YAML-Datei direkt öffnen oder mit einem lokalen Viewer betrachten.

## SSE-Streaming (Changefeed)

Dieser Endpoint ist nicht Teil der OpenAPI-Spezifikation, da er `text/event-stream` zurückliefert.

- Endpoint: `GET /changefeed/stream`
- Query-Parameter:
	- `from_seq` (optional, uint64): Start-Sequenz (exklusiv); Standard 0
	- `key_prefix` (optional, string): Filtert Events nach Schlüsselpräfix
	- `keep_alive` (optional, bool): Hält die Verbindung offen und streamt fortlaufend; Standard `true`
	- `max_seconds` (optional, int): Maximale Streamdauer, 1..60 Sekunden (Standard 30) – für Rotation/Tests
	- `heartbeat_ms` (optional, int): Test-Override für Heartbeat-Intervall (min. 100ms); in Produktion nicht nötig

Antwort:
- Content-Type: `text/event-stream`
- Format: jeweils eine Zeile pro Event `data: {JSON}\n\n`; bei Leerlauf Heartbeats `: heartbeat\n\n`

Beispiel (curl, Einmal-Stream für 10s):

```bash
curl -N "http://localhost:8765/changefeed/stream?from_seq=0&keep_alive=true&max_seconds=10"
```

Weitere Details und Best Practices (Checkpointing, Heartbeats, Reverse Proxy) siehe „Change Data Capture (CDC)“ unter Deployment & Betrieb.
