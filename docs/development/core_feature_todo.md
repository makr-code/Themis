# Core Feature TODO (Stand: 10. November 2025)

Diese Liste fasst die nächsten Core-Implementierungsschritte zusammen. Jede Aufgabe enthält betroffene Bereiche und empfohlene Artefakte für Tests und Dokumentation.

## Höchste Priorität

- [x] **Prometheus-Histogramme korrigieren**
  - Betroffene Dateien: `src/server/http_server.cpp`
  - Aufgaben: Histogramm-Updates kumulativ registrieren oder Prometheus-Client-Hilfsfunktionen nutzen; `/metrics`-Tests anpassen.
  - Tests/Doku: `tests/test_metrics_api.cpp`, Abschnitt in `docs/operations_runbook.md` aktualisieren.

- [x] **AQL LET & Join-Unterstützung**
  - Betroffene Dateien: `src/query/query_engine.cpp`, `tests/test_query_engine_join.cpp`.
  - Aufgaben: `LetNode`-Bindings im Engine-Kontext auswerten, doppelte `FOR`+`FILTER` Joins inklusive LET-Filtern unterstützen, neue Query-Engine-Tests ergänzt.
  - Tests/Doku: `tests/test_query_engine_join.cpp`, bestehende HTTP-AQL-Tests laufen unverändert.

- [x] **AQL OR/NOT Planner**
  - Betroffene Dateien: `src/query/aql_translator.cpp`, `tests/test_aql_or.cpp`.
  - Aufgaben: De-Morgan-Rewrite für NOT, Disjunktive Expansion für `NOT ==`, erweiterte Fallback-Strategie bei komplexen Ausdrücken.
  - Tests/Doku: `tests/test_aql_or.cpp` (NOT Pushdown), Dokumentation in `docs/aql_syntax.md` aktualisiert.

- [x] **AQL RETURN DISTINCT**
  - Betroffene Dateien: `include/query/aql_parser.h`, `src/query/aql_parser.cpp`, `src/query/query_engine.cpp`, `src/server/http_server.cpp`.
  - Aufgaben: `RETURN DISTINCT` parsen, Engine-Deduplizierung implementieren, HTTP-Antworten anpassen.
  - Tests/Doku: `tests/test_aql_parser.cpp`, `tests/test_query_engine_join.cpp`, `tests/test_http_aql.cpp`, Abschnitt in `docs/aql_syntax.md` ergänzt.

## Mittlere Priorität

- [ ] **AQL COLLECT erweitern**
  - Betroffene Dateien: `src/query/aql_translator.cpp`, `src/query/query_executor.cpp`.
  - Aufgaben: Mehrspaltige GROUP BY, HAVING-Unterstützung, Cursor-Pagination kompatibel machen.
  - Fortschritt: Mehrspaltige GROUP BY und HAVING umgesetzt (Nov 2025); Cursor-Pagination weiterhin offen.
  - Tests/Doku: Unit- und HTTP-Tests, Doku-Erweiterung `docs/aql_syntax.md`.

- [ ] **Vector Batch & Cursor APIs**
  - Betroffene Dateien: `src/index/vector_index.cpp`, `src/server/http_server.cpp`.
  - Aufgaben: Batch-Ingestion Endpoint (`POST /vector/batch_insert`), delete-by-filter, Score-basiertes Paging.
  - Tests/Doku: Neue Tests in `tests/http/test_vector_api.cpp`, Doku `docs/vector_ops.md`.

- [ ] **HNSW-Parameter persistieren**
  - Betroffene Dateien: `src/index/vector_index.cpp`, `include/index/vector_index.h`, `data/vector_index/meta.txt` (Format).
  - Aufgaben: M/ef-Werte beim Save/Load speichern, Validierung beim Startup ergänzen.
  - Tests/Doku: Persistenztests, Abschnitt in `docs/vector_ops.md` ergänzen.

- [ ] **Client SDK APIs (Python/JavaScript/Java/Rust/C++)**
  - Betroffene Dateien: `clients/python/`, `clients/js/`, `clients/java/`, `clients/rust/`, `clients/cpp/`, HTTP-Dokumentation.
  - Aufgaben: Gemeinsame Auth/Config-Basis implementieren, Query/Insert/Search Endpoints abbilden, Topologie- und Health-Checks kapseln, Beispiel-Workflows und Language-spezifische Build-Setups ergänzen.
  - Fortschritt: Python-SDK enthält Topologie-Fetch, Batch-Helper, Cursor-Query & Tests (`clients/python/themis/__init__.py`, `clients/python/tests/`), Quickstart `docs/clients/python_sdk_quickstart.md`. JavaScript-SDK besitzt funktionsfähigen Client mit Query-, Vector- und Batch-Funktionalität (`clients/javascript/src/index.ts`), ESLint/TSC-Setup und aktualisiertem Quickstart `docs/clients/javascript_sdk_quickstart.md`; weitere Sprachen offen.
  - Tests/Doku: Language-spezifische Unit-Tests (Vitest-Suite für JS steht noch aus), Integration gegen `docker-compose`-Stack, SDK-Abschnitt in `docs/infrastructure_roadmap.md` erweitern sowie weitere Quickstart-Guides erstellen.

- [ ] **OpenTelemetry-Instrumentierung aktivieren**
  - Betroffene Dateien: `src/server/http_server.cpp`, `src/query/query_engine.cpp`, `utils/tracing.cpp`.
  - Aufgaben: Spans für HTTP-Handler und Query-Pipeline, Attribute für Query-Typen.
  - Tests/Doku: Manuelle Validierung gegen Jaeger, Doku `docs/tracing.md`.

## Niedrigere Priorität (folgende Sprints)

- [ ] **Content/Filesystem-Phase starten**
  - Betroffene Dateien: `include/content/content_manager.h`, neue Implementierung `src/content/content_manager.cpp`.
  - Aufgaben: Upload, Chunking, Extraktions-Pipeline, Hybrid-Query-Beispiele.
  - Tests/Doku: Unit-Tests für Chunking, HTTP-Tests, Doku `docs/content_architecture.md` aktualisieren.

- [ ] **PKI-Signaturen verhärten**
  - Betroffene Dateien: `src/utils/pki_client.cpp`, `include/utils/pki_client.h`.
  - Aufgaben: OpenSSL-basierte Signatur/Verifikation, echte Zertifikate, Unit-Tests aktualisieren.
  - Tests/Doku: Tests in `tests/utils/test_pki_client.cpp`, Hinweis in `docs/compliance_audit.md`.

- [ ] **Dokumentation synchronisieren**
  - Betroffene Dateien: `docs/development/todo.md`, `docs/development/implementation_status.md`.
  - Aufgaben: Erledigte/fehlende Features korrekt markieren, neue TODO-Liste verlinken.
  - Tests/Doku: Review durch Team, Querverweise prüfen.
