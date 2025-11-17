````markdown
---
title: Changefeed Test Harness Skeleton
---

Zweck: Anleitung und ein Skelett für eine Test‑Harness, die als Basis für Unit‑ und Integrationstests des Changefeed MVP dienen kann.

1) Architektur

- Testhelper: in‑process RocksDB instance oder temp directory for DB.
- HTTP server: start server in test mode on ephemeral port.
- Event population: helper to insert deterministic events into changefeed CF.
- SSE client helper: parse `text/event-stream` payloads and expose them as objects.

2) C++ (gtest) skeleton pseudocode

```cpp
#include <gtest/gtest.h>
#include "test_helpers.h" // helper to start server, populate events

TEST(ChangefeedWriter, appends_event_on_put) {
  auto server = TestServer::Start();
  auto db = server.db();
  // perform put
  server.Put("foo/1", "{...}");
  auto events = server.GetChangefeedEvents(/*from_seq=*/0, /*limit=*/10);
  ASSERT_GT(events.size(), 0);
  EXPECT_EQ(events[0].key, "foo/1");
}

TEST(ChangefeedSSE, sse_replays_and_streams) {
  auto server = TestServer::Start();
  server.PopulateChangefeed(1,5);
  SSEClient client(server.address(), /*from_seq=*/3);
  auto evs = client.ReadInitialEvents(3);
  EXPECT_EQ(evs[0].seq, 3);
  // write new event
  server.Put("foo/6", "{...}");
  auto next = client.ReadNextEvent();
  EXPECT_EQ(next.seq, 6);
}
```

3) Utilities to implement
- `TestServer::Start()` — starts HTTP server with a fresh DB in temp dir.
- `TestServer::Put(key, value)` — helper to write and flush.
- `TestServer::GetChangefeedEvents(from_seq, limit)` — HTTP client call.
- `SSEClient` — small helper wrapping libcurl or EventSource parsing for tests.

4) Running tests in CI

- Ensure the CI image installs `libcurl`, `gtest` and `rocksdb` (or use vcpkg).
- Run via `cmake --build build --config Debug` then `ctest -V`.

````
