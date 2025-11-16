```markdown
# Changefeed (CDC) — Dokumente

Dieser Ordner enthält alle Changefeed/CDC bezogenen Entwicklungsdokumente.

Enthaltene Dateien:
- `changefeed_openapi.md` — OpenAPI‑Snippets für GET `/changefeed`, SSE `/changefeed/stream`, `/changefeed/stats`, `/changefeed/retention`.
- `changefeed_tests.md` — Testspezifikation und Akzeptanzkriterien (Unit, SSE, Retention).
- `changefeed_sse_examples.md` — SSE client Beispiele (curl, Node.js, C++).
- `changefeed_cmake_patch.md` — CMake / Testregistration Snippet für Changefeed Tests.
- `changefeed_openapi_auth.md` — OpenAPI Auth Beispiele (JWT/API Key).
- `changefeed_test_harness.md` — gtest Test‑Harness Skeleton / Test‑Utilities.

Zweck:
- Zentraler Einstiegspunkt für Entwickler, die am Changefeed‑MVP arbeiten oder Tests implementieren.

Nächste Schritte:
- Review die Testskripte und OpenAPI‑Snippets; falls OK kann ich einen PR mit minimaler HTTP‑Handler‑Implementierung vorbereiten.

```
