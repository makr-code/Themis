# Sprint A Plan (RAG/CDC)

Ziel: APIs und Grundgerüste für RAG‑nahe Features (Semantic Cache, CoT Storage) und Minimal‑CDC bereitstellen. Fokus auf OpenAPI, Feature‑Flags, DoD, Tests.

## Deliverables
- OpenAPI‑Stubs für
  - Semantic Cache v1: POST /cache/query, POST /cache/put, GET /cache/stats
  - CoT Storage v1: POST /llm/interaction, GET /llm/interaction/{id}, GET /llm/interaction
  - CDC Minimal: GET /changefeed?from_seq&limit&long_poll_ms
- Server‑Feature‑Flags (konfigurierbar)
- Handler‑Skeletons (HTTP 501 Not Implemented) optional
- Docs aktualisiert (OpenAPI in `openapi/` und `docs/`)

## Scope & DoD
- Scope: Nur Schnittstellen und Validierung; keine persistente Implementierung erforderlich (kann No‑Op oder In‑Memory Mock sein)
- DoD:
  - OpenAPI linted (schematisch gültig), Beispiele vorhanden
  - Server startet mit geflaggten Endpunkten (falls Skeletons), ansonsten 404 für deaktivierte Features
  - Minimaltests: 200/400/404‑Fälle je Endpoint (Mock)

## Feature Flags
- config.json → `features`: { `semantic_cache`: true, `llm_store`: true, `cdc`: true }
- Endpoints nur aktiv, wenn Feature true (sonst 404)

## Risiken & Notizen
- LLM/CoT Inhalte potenziell sensibel → PII‑Maskierung bei Logs/Tracing
- CDC Long‑Polling: Timeouts sauber konfigurieren (server.request_timeout_ms nutzen)

## Nächste Schritte
1) Flags in Config einführen und am Router verankern
2) Handler‑Skeletons (501) mit Request‑Schema‑Validierung
3) Tests (HTTP) für Happy/Fehlerpfade
4) Iteration für Persistenzdesign (Phase B)
