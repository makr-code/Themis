# Hybrid Search – Design (Phase 4)

Kombiniert Vektorähnlichkeit (Chunks) mit Graph-Expansion und optionalen Filtern, um robuste Ergebnisse über Content-Chunks zu liefern.

## Ziele
- Semantische Suche (Vector Top-K) + Kontext-Expansion (Graph n-hop)
- Score-Fusion aus Embedding-Similarity und Graph-Distanz/Topologie
- Filterbarkeit (category, mime_type, metadata-*), Pagination

## Ablauf
1) Query-Embedding (dim wie Index; z. B. 768D)
2) Vector Top-K über Namespace "chunks" (Whitelist optional)
3) Graph-Expansion: für gefundene Chunks → n-Hop Nachbarn (prev/next/parent/geo)
4) Re-Scoring:
   - final = alpha * sim - beta * graph_distance - gamma * hop
   - Deduplikation pro Content (Top-Chunk + Bonus für konsistente Mehrfachtreffer)
5) Filter anwenden (serverseitig vor/nach Fusion, je nach Kosten)
6) Sortierung + Pagination (limit/offset oder Cursor)

## API (Skizze)
- POST /search/hybrid
```json
{
  "query": "text or vector",
  "embedding": [..optional..],
  "k": 20,
  "expand": {"hops": 1, "edges": ["parent","next","prev","geo"]},
  "filters": {"category": ["TEXT","IMAGE","GEO"], "mime_type": ["image/jpeg"], "metadata": {"dataset": "LSG"}},
  "scoring": {"alpha": 1.0, "beta": 0.2, "gamma": 0.1}
}
```
- Response: Liste von Chunks (mit parent content meta), Score, ggf. Pfad/Expansion-Evidence

## Storage/Index-Annahmen
- Vector-Index: "chunks" (dim=768 für Text/Image; Geo 128D → ggf. getrennte Namespaces)
- Graph: Kanten parent/child, next/prev (Dokument-Order), geo (räumliche Nähe)
- Sekundärindizes: category, mime_type, metadata.dataset

## Edge Cases
- Leeres/kurzes Query → Fallback auf Filter/Graph-only
- Heterogene Dimensionen (Text 768D, Geo 128D) → getrennte Indizes + späte Fusion
- Große Hops → harte Limits, Zeitouts, Soft-Cutoff

## Tests (Skizze)
- Top-K stabil, Fusion deterministisch bei fixierten Parametern
- Filter wirksam (before/after Fusion), Paginierung korrekt
- Graph-Expansion erhöht Recall (nachweisbar an Testdaten)
