# Developers Guide

Dieser Leitfaden richtet sich an Entwicklerinnen und Entwickler, die am Themis Multi‑Modell‑Datenbanksystem mitarbeiten.

## Inhalte

- Lokales Setup (Windows)
- Build & Test
- Projektstruktur
- Server starten & nützliche Endpunkte
- AQL / Traversal, EXPLAIN/PROFILE
- Coding‑Guidelines & Stil
- Debugging & Troubleshooting
- Benchmarks

---

## Lokales Setup (Windows, PowerShell)

Voraussetzungen:
- Windows 10/11
- CMake >= 3.20
- MSVC (Visual Studio 2019+)
- vcpkg

Setup (aus dem Repo‑Root):

```powershell
# Dependencies und Toolchain einrichten
./setup.ps1

# Build (Release)
./build.ps1
```

Optional: Manuelles CMake

```powershell
mkdir build; cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build . --config Release
ctest -C Release
```

## Tests ausführen

Nach einem Build liegen die Binaries in `build/Release/`.

```powershell
# Unit/Integration Tests
& "c:\\VCC\\VCCDB\\build\\Release\\themis_tests.exe"
```

Der aktuelle Stand (28.10.2025): Alle Tests grün. Siehe `todo.md` für Details zu stabilisierten Testbereichen.

## Server starten

```powershell
# Start Server
& "c:\\VCC\\VCCDB\\build\\Release\\themis_server.exe" --config config\\config.json
```

Nützliche Endpunkte:
- GET `/health` – Liveness Probe
- GET `/stats` – Strukturierte Server‑ & RocksDB‑Statistiken (JSON)
- GET `/metrics` – Prometheus Text Format
- POST `/query` – JSON‑Query (Equality/Range)
- POST `/query/aql` – AQL Query (FOR/FILTER/SORT/LIMIT/RETURN, Traversals)
- POST `/graph/traverse` – Low‑Level Graph‑Traversal (BFS)

## AQL & Traversal, EXPLAIN/PROFILE

- AQL unterstützt u. a. FOR/FILTER/SORT/LIMIT/RETURN.
- Traversals: `FOR v,e,p IN min..max OUTBOUND start GRAPH 'g' RETURN v|e|p`
- Filterfunktionen: ABS, CEIL, FLOOR, ROUND, POW, DATE_TRUNC, DATE_ADD/SUB, NOW
- Boolesche Logik inkl. XOR, Short‑Circuit Evaluation.
- Konservatives Pruning am letzten Level (v/e‑Prädikate vor Enqueue)
- Konstanten‑Vorprüfung: FILTER ohne v/e‑Referenzen werden einmalig geprüft.

Profiling:
- Request‑Body Flag `"explain": true` aktivieren.
- Siehe `docs/aql_explain_profile.md` für die Metriken (z. B. edges_expanded,
  pruned_last_level, filter_evaluations_total, filter_short_circuits,
  frontier_limit_hits, result_limit_reached, per‑depth‑Frontier).
- Designnotiz zu sicheren Pfad‑Constraints: `docs/path_constraints.md`.

## Projektstruktur

```
include/        # Öffentliche Header (storage, index, query, server, utils)
src/            # Implementierung (api, storage, index, query, server)
docs/           # Dokumentation
benchmarks/     # Benchmarks
build/          # Build‑Artefakte
config/         # Beispielkonfigurationen
```

Wichtige Einstiegspunkte:
- `src/server/http_server.cpp` – HTTP Routen & AQL Ausführung
- `include/query/query_engine.h` / `src/query/query_engine.cpp` – Query Engine
- `include/index/*` – Indizes (Graph, Secondary, Vector)
- `include/storage/*` – Base Entity & RocksDB Wrapper

## Coding‑Guidelines

- C++20, konsequente Nutzung von `string_view`, `span` wo sinnvoll
- Fehlerpfade mit `StatusOr<T>`/`tl::expected` (falls vorhanden) oder klare
  Rückgabewerte + Logging
- Threadsicherheit: Keine globalen Singletons ohne Schutz; bevorzugt
  RAII und klare Besitzverhältnisse
- Tests: GoogleTest; jeweils Happy‑Path + 1–2 Edge‑Cases
- Metriken: Für neue öffentliche Operationen sinnvolle Counters/Gauges

## Debugging & Troubleshooting

- Logs: `themis_server.log`, `vccdb_server.log` im Repo‑Root
- RocksDB Pfade in `config/config.json` prüfen (relative Pfade unter Windows empfohlen)
- Bei `/stats` und `/metrics` prüfen, ob RocksDB‑Werte plausibel sind
- Graph/AQL: Bei großen Traversals `max_frontier_size` und `max_results` im AQL‑Request setzen

## Benchmarks

```powershell
# CRUD Benchmark
& "c:\\VCC\\VCCDB\\build\\Release\\bench_crud.exe"

# Query Benchmark
& "c:\\VCC\\VCCDB\\build\\Release\\bench_query.exe"

# Vector Search Benchmark
& "c:\\VCC\\VCCDB\\build\\Release\\bench_vector_search.exe"
```

## Beiträge & Workflow

- Branch vom `main` erstellen, PR mit kurzen, fokussierten Commits
- PR‑Checks: Build + Tests müssen PASS sein
- Dokumentation aktualisieren (README, docs/*, ggf. developers.md)
- Roadmap‑Updates in `todo.md` (Abschnitt thematisch verlinken)
