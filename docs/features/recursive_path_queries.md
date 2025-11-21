# Recursive Path Queries & Multi-Hop Reasoning

**Status:** MVP Complete (31. Oktober 2025)  
**Feature Set:** Rekursive Pfadabfragen, Multi-Hop Traversal, Temporale Graph-Queries

---

## Überblick

Das Themis Query-Engine-Modul unterstützt jetzt rekursive Pfadabfragen für Graph-Traversals mit variabler Tiefe und optionaler zeitlicher Filterung.

### Hauptfunktionen

- **Recursive Path Queries:** Variable Tiefe ohne festes Limit (1..max_depth)
- **Multi-Hop Traversal:** Automatische Pfadfindung über mehrere Knoten
- **Temporal Graph Support:** Zeitabhängige Kanten mit `valid_from`/`valid_to`
- **Shortest Path:** Dijkstra-Algorithmus für kürzeste Pfade
- **Breadth-First Search:** Alle erreichbaren Knoten bis zu einer bestimmten Tiefe

---

## API-Referenz

### RecursivePathQuery Struktur

```cpp
struct RecursivePathQuery {
    std::string start_node;              // Start-Knoten (erforderlich)
    std::string end_node;                // Ziel-Knoten (optional, leer = BFS)
    std::string edge_type;               // Kanten-Typ-Filter (reserviert)
    size_t max_depth = 5;                // Maximale Traversal-Tiefe
    std::optional<std::string> valid_from;  // Zeitfenster Start (ms)
    std::optional<std::string> valid_to;    // Zeitfenster Ende (ms)
};
```

### QueryEngine Methode

```cpp
std::pair<Status, std::vector<std::vector<std::string>>> 
executeRecursivePathQuery(const RecursivePathQuery& q) const;
```

**Rückgabe:**
- `Status`: OK oder Fehlermeldung
- `vector<vector<string>>`: Liste von Pfaden (jeder Pfad ist eine Sequenz von Knoten-IDs)

---

## Verwendungsbeispiele

### Beispiel 1: Kürzester Pfad (Single Target)

```cpp
#include "query/query_engine.h"
#include "index/graph_index.h"

// Setup
RocksDBWrapper db;
db.open("data/mydb");
SecondaryIndexManager secIdx(db);
GraphIndexManager graphIdx(db);
QueryEngine engine(db, secIdx, graphIdx);

// Query: Finde kürzesten Pfad von A nach D
RecursivePathQuery q;
q.start_node = "A";
q.end_node = "D";
q.max_depth = 10;

auto [st, paths] = engine.executeRecursivePathQuery(q);
if (st.ok && !paths.empty()) {
    // paths[0] = ["A", "B", "C", "D"]
    for (const auto& node : paths[0]) {
        std::cout << node << " -> ";
    }
}
```

### Beispiel 2: BFS - Alle erreichbaren Knoten

```cpp
// Query: Finde alle Knoten erreichbar von A (max Tiefe 3)
RecursivePathQuery q;
q.start_node = "A";
// Kein end_node = BFS-Modus
q.max_depth = 3;

auto [st, paths] = engine.executeRecursivePathQuery(q);
if (st.ok) {
    std::cout << "Erreichbare Knoten: " << paths.size() << std::endl;
    for (const auto& path : paths) {
        std::cout << "  " << path.back() << std::endl; // Ziel-Knoten
    }
}
```

### Beispiel 3: Temporale Pfadabfrage

```cpp
// Query: Finde Pfad, der zur Zeit 1600ms gültig war
RecursivePathQuery q;
q.start_node = "A";
q.end_node = "C";
q.max_depth = 5;
q.valid_from = "1600";  // Zeitstempel in Millisekunden

auto [st, paths] = engine.executeRecursivePathQuery(q);
// Nur Kanten, die bei valid_from <= 1600 <= valid_to sind, werden traversiert
```

### Beispiel 4: Zeitfenster-Query

```cpp
// Query: Finde Pfad im Zeitfenster [1000, 2000]
RecursivePathQuery q;
q.start_node = "A";
q.end_node = "D";
q.max_depth = 10;
q.valid_from = "1000";
q.valid_to = "2000";

auto [st, paths] = engine.executeRecursivePathQuery(q);
// Verwendet Mittelpunkt des Zeitfensters (1500ms) als Query-Zeitstempel
```

---

## Graphen-Schema

### Knoten-Entity

```cpp
BaseEntity node("nodes", "node_id");
node.set("name", "Node Name");
node.set("type", "Person");
// ... weitere Attribute
db.putEntity(node);
```

### Kanten-Entity (Standardgraph)

```cpp
BaseEntity edge("edges", "edge_id");
edge.set("_from", "node_a");  // Erforderlich
edge.set("_to", "node_b");    // Erforderlich
edge.set("_weight", 1.5);     // Optional für gewichtete Pfade
db.putEntity(edge);
graphIdx.addEdge(edge);
```

### Kanten-Entity (Temporaler Graph)

```cpp
BaseEntity edge("edges", "edge_id");
edge.set("_from", "node_a");
edge.set("_to", "node_b");
edge.set("valid_from", 1000);  // Millisekunden seit Epoch
edge.set("valid_to", 2000);    // Millisekunden seit Epoch
db.putEntity(edge);
graphIdx.addEdge(edge);
```

**Zeitfenster-Semantik:**
- `valid_from` = null → gültig seit Anbeginn der Zeit
- `valid_to` = null → gültig bis in alle Ewigkeit
- Query-Zeitstempel `t` muss in `[valid_from, valid_to]` liegen

---

## Interne Algorithmen

### Shortest Path (end_node angegeben)

Verwendet **Dijkstra-Algorithmus**:
- Zeit-Komplexität: O((V + E) log V)
- Gewichtete Graphen unterstützt (Feld `_weight`)
- Temporal variant: `dijkstraAtTime()` filtert Kanten nach Zeitstempel

### BFS (kein end_node)

Verwendet **Breadth-First Search**:
- Zeit-Komplexität: O(V + E)
- Findet alle Knoten bis zu `max_depth`
- Temporal variant: `bfsAtTime()` filtert Kanten nach Zeitstempel

### Temporal Filtering

```cpp
TemporalFilter filter = TemporalFilter::at(timestamp_ms);

// Für jede Kante:
bool isValid = filter.isValid(edge.valid_from, edge.valid_to);
```

---

## Performance-Charakteristiken

| Operation | Time Complexity | Space Complexity | Notes |
|-----------|----------------|------------------|-------|
| Shortest Path (Dijkstra) | O((V + E) log V) | O(V) | Mit Priority Queue |
| BFS Traversal | O(V + E) | O(V) | Breadth-First |
| Temporal Filter Check | O(1) | O(1) | Per Edge |
| Path Reconstruction | O(depth) | O(depth) | Linear in Tiefe |

**Skalierung:**
- In-Memory Graph Topology: O(1) Nachbarschaftsabfragen
- RocksDB Fallback: O(log N) bei kalten Kanten
- Empfohlenes Limit: max_depth <= 10 für interaktive Queries

---

## Tests

### Unit-Tests (test_recursive_path_query.cpp)

- ✅ `SimplePathQuery`: Kürzester Pfad A → D in linearem Graph
- ✅ `PathNotFound`: Kein Pfad in Gegenrichtung (gerichteter Graph)
- ✅ `BFSReachableNodes`: BFS findet alle erreichbaren Knoten
- ✅ `TemporalPathQuery_ValidTime`: Pfad zur Zeit 1600ms
- ✅ `TemporalPathQuery_InvalidTime`: Kein Pfad zur Zeit 500ms
- ✅ `MaxDepthLimit`: Respektiert max_depth Grenze
- ✅ `EmptyStartNode`: Fehlerbehandlung für leeren Start
- ✅ `NoGraphIndexManager`: Fehlerbehandlung ohne Graph-Index

**Test-Ausführung:**
```bash
cd build
.\Release\themis_tests.exe --gtest_filter="RecursivePathQueryTest.*"
```

---

## Einschränkungen & TODOs

### MVP Scope (Aktuell)

- ✅ Single shortest path (Dijkstra)
- ✅ BFS für erreichbare Knoten
- ✅ Temporale Filterung (valid_from/valid_to)
- ✅ Max-Tiefe-Limit

### Zukünftige Erweiterungen

- [ ] **All Paths Enumeration:** Nicht nur kürzester, sondern alle Pfade
- [ ] **Path Constraints:** Filter auf Kanten-Attributen (z.B. `edge_type`)
- [ ] **Variable-Length Patterns:** Cypher-Style `[:KNOWS*1..5]`
- [ ] **Recursive CTEs:** SQL-ähnliche WITH RECURSIVE Syntax
- [ ] **Weighted Temporal Paths:** Kombination von Zeitfenster und Gewichtung
- [ ] **Bidirectional Search:** Schnellere Pfadsuche für große Graphen
- [ ] **Graph Projection:** Virtuelle Subgraphen für spezielle Queries

---

## AQL-Integration (Geplant)

### Zukünftige AQL-Syntax

```aql
// Kürzester Pfad
FOR v, e, p IN 1..5 OUTBOUND 'nodes/A' GRAPH 'my_graph'
    FILTER p.vertices[*]._key == 'D'
    LIMIT 1
    RETURN p

// Temporale Pfadabfrage
FOR v, e, p IN 1..3 OUTBOUND 'nodes/A' GRAPH 'my_graph'
    FILTER e.valid_from <= @timestamp AND e.valid_to >= @timestamp
    FILTER p.vertices[-1]._key == 'D'
    RETURN p

// Alle erreichbaren Knoten
FOR v IN 1..3 OUTBOUND 'nodes/A' GRAPH 'my_graph'
    RETURN DISTINCT v
```

---

## Siehe auch

- `docs/architecture.md` - System-Architektur-Übersicht
- `include/index/graph_index.h` - Graph-Index API
- `include/index/temporal_graph.h` - Temporal-Filter Design
- `tests/test_graph_index.cpp` - Graph-Index Unit-Tests
- `tests/test_temporal_graph.cpp` - Temporal-Graph Tests

---

**Letzte Aktualisierung:** 31. Oktober 2025  
**Version:** 1.0.0 (MVP)  
**Status:** ✅ Production Ready
