# Pagination Benchmarks: Offset vs Cursor

Dieser Leitfaden beschreibt zwei Microbenchmarks zur Pagination-Performance:

- Offset-basierte Pagination (ORDER BY + LIMIT offset,count mit Post-Slicing)
- Cursor-basierte Pagination (Anchor-based Start-after mit LIMIT count+1)

Quelle: `benchmarks/bench_query.cpp`

## Setup

Beim ersten Lauf wird eine Testdatenbank unter `data/themis_bench_query` erzeugt. Es werden `bench_users`-Entities mit Range-Index auf `age` angelegt.

## Benchmarks

- `BM_Pagination_Offset(page_size=50, pages=50)`
- `BM_Pagination_Cursor(page_size=50, pages=50)`

Offset-Variante setzt `orderBy.limit = offset + count` und schneidet die Seite nachträglich (wie der HTTP-Pfad). Cursor-Variante nutzt `(cursor_value, cursor_pk)` als Anchor und `LIMIT count+1` zur `has_more`-Erkennung.

## Ausführen (optional)

```powershell
# Reconfigure to ensure benchmarks are built (once)
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release -DTHEMIS_BUILD_BENCHMARKS=ON
cmake --build . --config Release --parallel

# Run only pagination benchmarks
.\Release\themis_benchmarks.exe --benchmark_filter=BM_Pagination_.*
```

Hinweis: Zeiten können durch I/O-Cache und Hardware variieren. Für reproduzierbare Messungen ggf. mehrfach ausführen und Mittelwerte bilden.

## Interpretation

- Offset-Pagination: Aufwand wächst mit `offset` (Index muss die Einträge bis zur Seite traversieren).
- Cursor-Pagination: Konstante Arbeit pro Seite (Start-after) – stabil bei großen Datenmengen.

In Einzelfällen kann die Cursor-Variante durch zusätzliche Entity-Loads (Anchor-Ermittlung) leicht höhere CPU-Zeit zeigen; mit warmem Cache und realistischen Daten ist sie bei großen Offsets typischerweise überlegen.
