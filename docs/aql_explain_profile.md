# AQL EXPLAIN & PROFILE

**Version:** 1.0  
**Datum:** 28. Oktober 2025  
**Zweck:** Dokumentation der Query-Analyse und Performance-Metriken

---

## Überblick

THEMIS bietet `explain=true` zur Abfrage von Query-Plänen und Performance-Metriken für AQL-Queries. Dies ist nützlich für:

- Query-Optimierung und Index-Auswahl
- Performance-Debugging
- BFS-Traversal Pruning-Effektivität
- Filter Short-Circuit Analyse

---

## HTTP API Usage

### Request

```http
POST /query/aql
Content-Type: application/json

{
  "query": "FOR v IN 1..3 OUTBOUND 'user1' GRAPH 'social' FILTER v.age > 18 RETURN v",
  "explain": true
}
```

### Response (Traversal)

```json
{
  "table": "graph",
  "count": 42,
  "entities": [...],
  "metrics": {
    "constant_filter_precheck": false,
    "edges_expanded": 156,
    "pruned_last_level": 23,
    "filter_evaluations_total": 89,
    "filter_short_circuits": 12,
    "frontier_processed_per_depth": {
      "0": 1,
      "1": 5,
      "2": 18,
      "3": 65
    },
    "enqueued_per_depth": {
      "1": 5,
      "2": 18,
      "3": 65
    }
  }
}
```

### Response (Relational Query)

```json
{
  "table": "users",
  "count": 25,
  "entities": [...],
  "query": "FOR u IN users FILTER u.age > 18 AND u.city == 'Berlin' RETURN u",
  "ast": {...},
  "plan": {
    "mode": "index_optimized",
    "order": [
      {"column": "city", "value": "Berlin"},
      {"column": "age", "value": "18"}
    ],
    "estimates": [
      {"column": "city", "value": "Berlin", "estimatedCount": 100, "capped": false},
      {"column": "age", "value": "18", "estimatedCount": 500, "capped": false}
    ]
  }
}
```

---

## Traversal Metrics

### constant_filter_precheck
- **Typ:** `boolean`
- **Beschreibung:** Wurde ein konstanter FILTER (ohne v/e-Referenzen) vorab evaluiert?
- **Nutzung:** Zeigt, ob die Query vor BFS abgebrochen wurde (wenn false ergibt)

**Beispiel:**
```aql
FOR v IN 1..3 OUTBOUND 'user1' GRAPH 'social' 
  FILTER 1 == 2  -- konstant false
  RETURN v
```
→ `constant_filter_precheck: true`, Ergebnis sofort leer ohne BFS

---

### edges_expanded
- **Typ:** `int`
- **Beschreibung:** Anzahl der inspizierten Adjazenz-Kanten (out/in) während BFS
- **Nutzung:** Indikator für Traversal-Kosten

**Interpretation:**
- Niedrig: Gut pruned oder kleiner Graph
- Hoch: Großer Frontier oder fehlende Prädikate

---

### pruned_last_level
- **Typ:** `int`
- **Beschreibung:** Anzahl der am letzten Level (depth == maxDepth) durch v/e-Prädikate weggeschnittenen Nachbarn
- **Nutzung:** Wirksamkeit des konservativen Prunings messen

**Beispiel:**
```aql
FOR v IN 1..3 OUTBOUND 'user1' GRAPH 'social' 
  FILTER v.age > 30
  RETURN v
```
→ `pruned_last_level: 23` – 23 Vertices am letzten Level hatten age <= 30 und wurden nicht eingereiht

---

### filter_evaluations_total
- **Typ:** `int`
- **Beschreibung:** Anzahl der FILTER-Evaluierungen pro BFS-Zeile (Knoten + eingehende Kante)
- **Nutzung:** Overhead durch komplexe Filter tracken

**Interpretation:**
- Sollte <= frontier_processed_per_depth (Summe) sein
- Hoch bei komplexen Bool-Ausdrücken

---

### filter_short_circuits
- **Typ:** `int`
- **Beschreibung:** Anzahl der Short-Circuits bei AND/OR (Early Exit)
- **Nutzung:** Effizienz der Bool-Logik messen

**Beispiel:**
```aql
FILTER v.age > 18 AND v.city == "Berlin"
```
→ Wenn `v.age > 18` false ist, wird `v.city == "Berlin"` nicht evaluiert → `filter_short_circuits++`

---

### frontier_processed_per_depth
- **Typ:** `object` (depth → count)
- **Beschreibung:** Anzahl der verarbeiteten Knoten pro Tiefe
- **Nutzung:** BFS-Expansion visualisieren

**Beispiel:**
```json
{
  "0": 1,     // Startknoten
  "1": 5,     // 1. Hop: 5 Nachbarn
  "2": 18,    // 2. Hop: 18 Nachbarn
  "3": 65     // 3. Hop: 65 Nachbarn
}
```

**Interpretation:**
- Exponentielles Wachstum: Dichte Graphen
- Lineares Wachstum: Sparse oder gut gefiltert

---

### enqueued_per_depth
- **Typ:** `object` (depth → count)
- **Beschreibung:** Anzahl der eingereihten Knoten je (depth+1) während Expansion
- **Nutzung:** Neue Frontier-Größe tracken (vor visited-Check)

**Beispiel:**
```json
{
  "1": 5,
  "2": 18,
  "3": 42    // 23 wurden später durch Pruning gedroppt (siehe pruned_last_level)
}
```

---

## Relational Query Plan

### mode
- **index_optimized:** Optimizer-gesteuerter Plan mit Kardinalitätsschätzung
- **index_parallel:** Parallele Index-Scans mit AND-Merge
- **full_scan_fallback:** Sequential Scan (wenn allow_full_scan=true und kein Index)
- **index_rangeaware:** Range-Prädikate/ORDER BY nutzen Range-Index direkt

### order
Array von Prädikaten in Evaluierungsreihenfolge (sortiert nach Selektivität bei `index_optimized`)

### estimates
Kardinalitätsschätzung pro Prädikat:
- **estimatedCount:** Geschätzte Anzahl Treffer
- **capped:** Wurde die Schätzung bei MAX_ESTIMATE_LIMIT gecappt?

---

## Best Practices

### 1. Pruning-Effektivität prüfen
```bash
curl -X POST http://localhost:8080/query/aql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "FOR v IN 1..3 OUTBOUND \"user1\" GRAPH \"social\" FILTER v.age > 30 RETURN v",
    "explain": true
  }' | jq '.metrics'
```

**Erwartetes Ergebnis:**
- `pruned_last_level > 0` → Pruning greift
- `edges_expanded < enqueued_per_depth (Summe)` → Effizienz durch visited-Set

---

### 2. Filter Short-Circuits optimieren
Stelle selektive Prädikate zuerst:
```aql
-- Gut (Stadt zuerst, sehr selektiv)
FILTER v.city == "Smalltown" AND v.age > 18

-- Schlecht (Alter zuerst, wenig selektiv)
FILTER v.age > 18 AND v.city == "Smalltown"
```

→ Mehr `filter_short_circuits` bei optimaler Reihenfolge

---

### 3. Frontier-Explosion vermeiden
Bei `frontier_processed_per_depth` mit exponentiellem Wachstum:
- maxDepth reduzieren
- Stärkere Prädikate (z. B. Edge-Filter) hinzufügen
- Index auf v/e-Felder anlegen

---

### 4. Konstante Filter vorab eliminieren
```aql
-- Ineffizient (BFS läuft, obwohl Ergebnis immer leer)
FOR v IN 1..3 OUTBOUND 'user1' GRAPH 'social' 
  FILTER 1 == 2
  RETURN v
```

→ `constant_filter_precheck: true`, `edges_expanded: 0`

---

## Metriken erweitern (Roadmap)

Geplante Erweiterungen:
- [ ] `filter_evaluations_per_depth` (Granularität pro Level)
- [ ] `max_frontier_size` (Peak Memory-Indikator)
- [ ] `path_reconstructions` (bei RETURN p)
- [ ] `entity_loads` (RocksDB Get-Calls)
- [ ] `timing_ms` (BFS-Dauer, Filter-Dauer, Serialisierung)

---

## Zusammenfassung

| Metrik | Bedeutung | Optimierungsziel |
|--------|-----------|------------------|
| `edges_expanded` | BFS-Kosten | Minimieren (durch Filter/Pruning) |
| `pruned_last_level` | Pruning-Erfolg | Maximieren (zeigt Filtereffizienz) |
| `filter_evaluations_total` | Filter-Overhead | Minimieren (einfache Prädikate bevorzugen) |
| `filter_short_circuits` | Bool-Logik-Effizienz | Maximieren (selektive Prädikate zuerst) |
| `frontier_processed_per_depth` | BFS-Expansion | Kontrolliert wachsen (nicht exponentiell) |
| `enqueued_per_depth` | Neue Frontier | Niedrig halten (visited-Set wirkt) |

**Faustregel:**  
`pruned_last_level / edges_expanded` sollte > 10% sein für effektives Pruning.

---

**Siehe auch:**
- [AQL Syntax](aql_syntax.md)
- [Indexes](indexes.md)
- [Graph Traversal](architecture.md#graph-layer)
