```markdown
---
title: Changefeed Tests and Acceptance Criteria
---

Zweck: Testspezifikation für das Changefeed‑MVP. Ziel ist es, klare Unit‑ und Integrationstests zu definieren und Akzeptanzkriterien bereitzustellen.

1) Unit Tests (Changefeed Writer)
- Test: `writer_appends_event_on_put`
  - Setup: In‑memory RocksDB oder Test‑DB, writer instanziiert.
  - Aktion: Schreibe ein Entity (Put/Update) mit key `foo/1`.
  - Erwartung: Ein neues ChangeEvent in CF `cf_changefeed` mit seq > 0, op == 'insert', key == 'foo/1'.

- Test: `seq_increments_monotonic`
  - Schreibe mehrere Entities, prüfe, dass `seq` strikt monoton steigt.

2) Reader Tests (GET /changefeed)
- Test: `get_from_seq_returns_events`
  - Setup: Populate changefeed with known events with seq 1..N.
  - Aktion: HTTP GET `/changefeed?from_seq=3&limit=2`.
  - Erwartung: Events with seq 3 and 4 returned, `last_seq` reflects highest stored.

3) SSE Integration Test (GET /changefeed/stream)
- Test: `sse_stream_replays_and_streams_new_events`
  - Setup: Start server in test mode; populate initial events up to seq=5.
  - Aktion: Client connects to `/changefeed/stream?from_seq=3` and listens.
  - Aktion2: Server writes new event seq=6 while client connected.
  - Erwartung: Client receives events seq 3,4,5 (replay) and then seq=6 (stream), heartbeats at configured interval.

4) Retention API Tests
- Test: `post_retention_deletes_up_to_seq`
  - Setup: Existing events up to seq 100.
  - Aktion: POST `/changefeed/retention` with `up_to_seq: 50`.
  - Erwartung: Events <= 50 removed; subsequent GET from_seq=1 returns first event with seq 51.

5) Concurrency / Stress Tests
- Test: `writer_scale_writes_without_seq_collision`
  - Simulate high write concurrency; ensure seq generation is atomic and events not lost.

6) Acceptance Criteria
- Replay correctness (from_seq works for any seq <= last_seq)
- SSE stream provides replay + live stream without duplication
- Retention removes events deterministisch und ist idempotent
- Tests run in CI via `ctest` / `make test`

7) Test Utilities
- Provide helper to populate changefeed with deterministic events (for tests)
- Provide in‑process HTTP client helper to read SSE events and assert sequences

```
