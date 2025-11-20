# AQL Pattern Matching - Implementation Guide

**Datum:** 19. November 2025  
**Status:** Design Complete - Nutzt existierende AQL-Syntax

---

## Konzept: Pattern-Matching ohne neue Syntax

**Philosophie:** Cypher-ähnliche Pattern-Matching-Queries können **vollständig** mit existierender AQL-Syntax ausgedrückt werden durch:
1. Verschachtelte `FOR`-Loops für Multi-Hop-Traversals
2. `FILTER` auf Vertex/Edge-Properties
3. `TYPE` keyword für Edge-Type-Matching

**Vorteil:** Keine Spezialsyntax, konsistent mit AQL-Prinzipien, wiederverwendet Graph-Infrastruktur.

---

## Pattern-Matching-Beispiele

### 1. Einfaches Pattern: (a)-[:FOLLOWS]->(b)

**Cypher-Style:**
```cypher
MATCH (a:Person)-[:FOLLOWS]->(b:Person)
WHERE a.name == "Alice"
RETURN b
```

**AQL-Äquivalent:**
```aql
FOR a IN persons
  FILTER a.name == "Alice"
  FOR e IN edges
    FILTER e._from == a._id AND e._type == "FOLLOWS"
    FOR b IN persons
      FILTER b._id == e._to
      RETURN b
```

**Optimierte AQL (mit Graph-Traversal):**
```aql
FOR b IN 1..1 OUTBOUND "persons/Alice" TYPE "FOLLOWS" GRAPH "social"
  RETURN b
```

---

### 2. Multi-Hop Pattern: (a)-[:FOLLOWS]->(b)-[:LIKES]->(c)

**Cypher-Style:**
```cypher
MATCH (a:Person)-[:FOLLOWS]->(b:Person)-[:LIKES]->(c:Product)
WHERE a.name == "Alice" AND c.category == "Books"
RETURN b, c
```

**AQL-Äquivalent (verschachtelte Traversals):**
```aql
FOR b IN 1..1 OUTBOUND "persons/Alice" TYPE "FOLLOWS" GRAPH "social"
  FOR c IN 1..1 OUTBOUND b._id TYPE "LIKES" GRAPH "social"
    FILTER c.category == "Books"
    RETURN {person: b, product: c}
```

---

### 3. Variable Pfadlänge: (a)-[:KNOWS*1..3]->(b)

**Cypher-Style:**
```cypher
MATCH (a:Person)-[:KNOWS*1..3]->(b:Person)
WHERE a.name == "Alice"
RETURN b
```

**AQL:**
```aql
FOR b IN 1..3 OUTBOUND "persons/Alice" TYPE "KNOWS" GRAPH "social"
  RETURN DISTINCT b
```

---

### 4. Komplexes Pattern mit Constraints

**Cypher-Style:**
```cypher
MATCH (a:Person)-[r1:FOLLOWS]->(b:Person)-[r2:LIKES]->(c:Product)
WHERE a.name == "Alice" 
  AND r1.since > "2024-01-01"
  AND b.age > 25
  AND c.price < 100
RETURN b, c
```

**AQL:**
```aql
FOR v1, e1, p1 IN 1..1 OUTBOUND "persons/Alice" TYPE "FOLLOWS" GRAPH "social"
  FILTER e1.since > "2024-01-01"
  FILTER v1.age > 25
  FOR v2, e2, p2 IN 1..1 OUTBOUND v1._id TYPE "LIKES" GRAPH "social"
    FILTER v2.price < 100
    RETURN {person: v1, product: v2}
```

---

## Implementierte Features (bereits vorhanden)

### ✅ Graph Traversal Syntax
- `FOR v, e, p IN min..max DIRECTION startVertex TYPE edgeType GRAPH graphName`
- Richtungen: `OUTBOUND`, `INBOUND`, `ANY`
- Variable Tiefe: `1..3`, `2..5`, etc.

### ✅ Edge-Type-Filtering
- `TYPE "FOLLOWS"` - filtert Kanten nach Typ während Traversal
- Bereits im Parser: `src/query/aql_parser.cpp:502`

### ✅ Pfad-Variablen
- `v` - Current Vertex
- `e` - Current Edge (letzte Kante zum Vertex)
- `p` - Full Path (vertices + edges)

### ✅ Property Filtering
- `FILTER v.age > 25` - Vertex-Properties
- `FILTER e.weight > 10` - Edge-Properties
- `FILTER p.vertices[0].name == "Alice"` - Pfad-Zugriff

---

## Fehlende Features (TODO)

### ⏳ PATH-Prädikate für Constraints

**Ziel:** Pfad-Constraints wie in Cypher:

```aql
FOR v, e, p IN 1..5 OUTBOUND 'user1' GRAPH 'social'
  FILTER ALL(edge IN p.edges WHERE edge.active == true)
  FILTER NONE(vertex IN p.vertices WHERE vertex.blocked == true)
  RETURN v
```

**Implementation:**
- Neue Expression-Typen: `PathPredicateExpr`
- Evaluierung in `let_evaluator.cpp` via Pfad-Iteration
- **Aufwand:** 1 Tag

---

### ⏳ Shortest-Path-Syntax

**Aktuell (umständlich):**
```aql
FOR v, e, p IN 1..10 OUTBOUND 'A' GRAPH 'network'
  FILTER v._id == 'B'
  SORT LENGTH(p.edges) ASC
  LIMIT 1
  RETURN p
```

**Geplant (Syntaxzucker):**
```aql
FOR p IN SHORTEST_PATH 'A' TO 'B' GRAPH 'network'
  RETURN p
```

**Implementation:**
- Parser-Erweiterung für `SHORTEST_PATH` keyword
- Translator nutzt `graphMgr_->dijkstra()`
- **Aufwand:** 0.5 Tage

---

## Performance-Optimierungen

### Edge-Type-Index
**Problem:** `TYPE "FOLLOWS"` erfordert derzeit Edge-Loading während Traversal.

**Lösung:** Separate Adjacency-Listen pro Edge-Type:
```
graph:out:<edgeType>:<fromPk>:<edgeId> -> <toPk>
```

**Benefit:** 10x schnelleres Pattern-Matching für type-spezifische Queries.

**Aufwand:** 0.5 Tage

---

## Zusammenfassung

**Status:** Pattern-Matching ist **bereits möglich** mit existierender AQL-Syntax!

**Kern-Features:**
- ✅ Multi-Hop Traversals (verschachtelte FOR)
- ✅ Edge-Type-Filtering (TYPE keyword)
- ✅ Property-Constraints (FILTER)
- ✅ Variable Pfadlängen (min..max)

**Empfohlene Erweiterungen:**
1. PATH-Prädikate (ALL/ANY/NONE) - 1 Tag
2. SHORTEST_PATH Syntaxzucker - 0.5 Tage
3. Edge-Type-Index - 0.5 Tage

**Total:** 2 Tage für vollständiges Cypher-Parity Pattern-Matching

**Nächster Schritt:** Dokumentation + Beispiele statt neue Syntax!
