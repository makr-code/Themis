# Change Data Capture (CDC)

This document describes the minimal CDC implementation in Themis and how to consume it reliably.

## Overview

- Append-only change log stored in RocksDB
- Monotonic sequence numbers per event
- Event types: PUT, DELETE (transactional events reserved for future use)
- HTTP API for polling with optional long-polling
- Filters by key_prefix
- Admin endpoints for stats and retention

## HTTP API

- GET /changefeed?from_seq=SEQ&limit=N&long_poll_ms=MS&key_prefix=P
  - Returns JSON with fields:
    - events: array of events `{ sequence, type, key, value|null, timestamp_ms, metadata }`
    - count: number of events returned
    - latest_sequence: latest sequence observed in the feed
- **GET /changefeed/stream?from_seq=SEQ&key_prefix=P&keep_alive=BOOL&max_seconds=S** (SSE streaming)
  - Server-Sent Events (SSE) für Echtzeit‑Consumption.
  - Content-Type: `text/event-stream`
  - Eventformat: `data: {JSON}\n\n` (ein Event pro SSE‑Nachricht)
  - Keep‑Alive: `keep_alive=true` hält die Verbindung offen und sendet fortlaufend Events; bei Inaktivität werden Heartbeats versendet.
  - Dauerbegrenzung: `max_seconds` begrenzt die maximale Stream‑Dauer (Standard 30s, Bereich 1..60) – nützlich für Tests/Rotation.
  - Heartbeats: Wenn keine Events vorliegen, sendet der Server periodisch Kommentarzeilen `: heartbeat\n\n` zur Verbindungspflege.
  - Test/Dev: `heartbeat_ms` kann die Heartbeat‑Periode pro Request überschreiben (min. 100ms). In Produktion nicht nötig – Standard sind ~15s.
  - Checkpointing identisch zum Polling: Nach Verarbeitung größtes `sequence` als neuen Startpunkt speichern.
- GET /changefeed/stats
  - Returns `{ total_events, latest_sequence, total_size_bytes }`
- POST /changefeed/retention
  - Body: `{ "before_sequence": <uint64> }`
  - Deletes all events with sequence strictly less than `before_sequence`.

## Client pattern: Checkpointing and long-polling

A robust consumer should:

1. Persist a checkpoint `last_seq` per consumer (e.g., in your own app DB or file).
2. Poll the feed with `from_seq=last_seq` and a reasonable `limit` (e.g., 1000).
3. If `events` is empty and `long_poll_ms>0`, the server waits up to the timeout for new events.
4. After processing returned events in order, update `last_seq` to the largest `sequence` seen.
5. Repeat from step 2.

**SSE Streaming (Keep‑Alive):**

Für niedrige Latenz bei kontinuierlichem Konsum nutze `GET /changefeed/stream?from_seq=<last_seq>&key_prefix=<optional>&keep_alive=true`:

- Öffnet eine SSE‑Verbindung (Content‑Type `text/event-stream`).
- Empfängt Events als `data: {JSON}\n\n`; bei Leerlauf periodische Heartbeats `: heartbeat\n\n`.
- Der Stream kann über `max_seconds` begrenzt werden; Clients sollten nach Ablauf wieder verbinden (mit aktualisiertem `from_seq`).
- SSE Parsing: Auf `\n\n` splitten, Zeilen mit `data: ` extrahieren, JSON parsen.
- Betriebsaspekte (Timeouts/Proxies): siehe Abschnitt „Reverse Proxy und SSE/Keep‑Alive Hinweise“ in `docs/deployment.md`.

Example (pseudo):

```python
last_seq = load_checkpoint() or 0
while True:
    res = http.get(f"/changefeed?from_seq={last_seq}&limit=1000&long_poll_ms=10000")
    for ev in res.events:
        handle(ev)
        last_seq = ev.sequence
        if processed_count % 100 == 0:
            save_checkpoint(last_seq)
```

## Backpressure and limits

- Use a bounded `limit` (e.g., 1000) to control batch size.
- If the producer rate is high, loop quickly and persist checkpoints regularly.
- If idle, prefer `long_poll_ms` (e.g., 10 seconds) to reduce request churn.

## Filtering by key prefix

You can restrict the stream to a subset of keys using `key_prefix`. For example:

- `/changefeed?from_seq=0&limit=1000&key_prefix=orders:` returns only events whose keys start with `orders:`.

## Retention

To reclaim space, apply retention by sequence:

- Determine a safe lower bound based on replicated checkpoints across consumers.
- Call `POST /changefeed/retention { "before_sequence": SAFE_MIN }`.
- Verify with `GET /changefeed/stats`.

Note: Timestamp-based retention is not supported in the MVP.

## Event schema

```json
{
  "sequence": 123,
  "type": "PUT", // or "DELETE"
  "key": "users:alice",
  "value": "{...}", // null for DELETE
  "timestamp_ms": 1730250000000,
  "metadata": { "table": "users", "pk": "alice" }
}
```

## Operational notes

- Enable via server config: `feature_cdc=true`.
- Events are stored in the default Column Family under keys `changefeed:<seq>`.
- Sequence generator is persisted as `changefeed_sequence`.
- Retention does not reset the `latest_sequence` counter.
