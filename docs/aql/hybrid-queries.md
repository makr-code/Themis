# AQL Hybrid Queries Guide (Phase 2 + 2.5)

Dieses Dokument fasst die Syntax-Zucker für Hybrid Queries zusammen und zeigt Best Practices.

## Übersicht
- `SIMILARITY(field, [vector], k?)` für Vector+Geo Ranking
- `PROXIMITY(geoField, [lon, lat])` für Content+Geo Distanz-basierte Re-Ranking (mit `FULLTEXT` Filter)
- `SHORTEST_PATH TO "vertexKey"` für kürzeste Pfad Abfragen in Graphen mit optionalen Spatial Constraints
- LET-Unterstützung für SIMILARITY/PROXIMITY (Phase 2.5)

## Beispiele
### Vector+Geo (Direktes Sorting)
```aql
FOR doc IN hotels
  FILTER ST_Within(doc.location, [13.4,52.5,13.6,52.7])
  SORT SIMILARITY(doc.embedding, [0.12,0.08,0.33], 10) DESC
  LIMIT 10
  RETURN doc
```

### Vector+Geo mit Equality + Range Prädikaten (Index Prefilter)
```aql
FOR doc IN hotels
  FILTER ST_Within(doc.location, [13.4,52.5,13.6,52.7])
  FILTER doc.city == "Berlin" AND doc.stars >= 4 AND doc.stars <= 5
  SORT SIMILARITY(doc.embedding, [0.12,0.08,0.33], 10) DESC
  RETURN doc
```
Intern: Gleichheits- und Range-Prädikate erzeugen einen PK-Whitelist Intersect über Sekundär- & Range-Indizes.

### Vector+Geo mit Composite Index (Mehrfach-Gleichheit)
```aql
FOR doc IN hotels
  FILTER ST_Within(doc.location, [13.4,52.5,13.6,52.7])
  FILTER doc.city == "Berlin" AND doc.category == "luxury"
  SORT SIMILARITY(doc.embedding, [0.1,0.2,0.3], 10) DESC
  RETURN doc
```
Voraussetzung: Composite Index über `(city, category)` erstellt.
Intern: `scanKeysEqualComposite()` liefert PK-Intersect, Kostenmodell bevorzugt Vector-first bei hoher Selektivität.

### Vector+Geo mit LET
```aql
FOR doc IN hotels
  LET sim = SIMILARITY(doc.embedding, [0.1,0.2,0.3], 5)
  SORT sim DESC
  RETURN { doc, similarity: sim }
```

### Content+Geo (Fulltext + Nähe)
```aql
FOR doc IN places
  FILTER FULLTEXT(doc.description, "coffee", 200)
  FILTER ST_Within(doc.location, [13.4,52.5,13.6,52.7])
  SORT PROXIMITY(doc.location, [13.5,52.55]) ASC
  LIMIT 20
  RETURN doc
```

### Content+Geo mit LET
```aql
FOR doc IN places
  FILTER FULLTEXT(doc.description, "coffee", 50)
  LET prox = PROXIMITY(doc.location, [13.5,52.55])
  SORT prox ASC
  RETURN { doc, dist: prox }
```

### Graph + Geo Shortest Path
```aql
FOR v, e, p IN 1..6 OUTBOUND "city:berlin" edges
  FILTER ST_Within(v.location, @boundary)
  SHORTEST_PATH TO "city:dresden"
  RETURN p
```

## Performance Hinweise
- Verwende räumliche Bounding-Box oder Polygon Filter früh für hohe Selektivität.
- Bei stark selektiven Equality/Range-Prädikaten wird Vector-first bevorzugt (Kostenmodell).
- `overfetch` (Konfiguration) steuert Qualität vs Kosten im Vector-first Plan.

### Kostenmodell-getriebene Planwahl
- **Vector+Geo**: Wählt zwischen Spatial-first (R-Tree Filter, dann ANN) und Vector-first (ANN mit overfetch, dann Spatial) basierend auf `bboxRatio`, Prefilter-Größe und Index-Verfügbarkeit.
- **Content+Geo**: Wählt zwischen Fulltext-first (BM25, dann Spatial) und Spatial-first (R-Tree, dann naive Token-Match) basierend auf `bboxRatio` und geschätzten Fulltext-Treffern.
- **Graph+Geo**: Dynamische Branching-Faktor-Schätzung über Sampling; Frühabbruch bei geschätzter Expansion >1M Vertices.

### Tracer-Attribute für Observability
- `optimizer.plan`: gewählter Ausführungsplan (z.B. `vector_then_spatial`)
- `optimizer.cost_spatial_first`, `optimizer.cost_vector_first`: Kostenschätzungen
- `optimizer.cg.plan`: Content+Geo Plan (`fulltext_then_spatial` | `spatial_then_fulltext`)
- `optimizer.graph.branching_estimate`: geschätzter Branching-Faktor bei Graph-Queries
- `index_prefilter_size`: Anzahl Kandidaten nach Equality/Range/Composite Prefilter
- `composite_prefilter_applied`: true wenn Composite Index genutzt wurde

## Indizes
- Gleichheit: `createIndex(table, column)`
- Range: `createRangeIndex(table, column)` für numerische / lexikographische Bereiche.
- Composite: `createCompositeIndex(table, [col1, col2, ...])` für mehrfach-Gleichheit (AND-verknüpft).
- Fulltext: `createFulltextIndex(table, column)` für PROXIMITY.
- Spatial: R-Tree via `createSpatialIndex(table, geometryColumn)` (Vorarbeit Phase 1.5).
- Vector: HNSW via `VectorIndexManager::load(table.field, dim)` oder Batch-Build.

## Rückgabe & Variablen
- Derzeit werden SIMILARITY/PROXIMITY Distanzwerte nicht automatisch als Feld injiziert; Bei LET Syntax kannst du sie im RETURN explizit nutzen.
- Standard-Dispatch JSON (`executeAql`) enthält für Vector+Geo `distance` und für Content+Geo `bm25` sowie optional `geo_distance`.

## Fehlermeldungen
- Falsche Argumentanzahl führt zu klarer Translator-Error.
- Fehlende FULLTEXT bei PROXIMITY -> Fehler.
- K soll Integer Literal sein (kein Parameter-Array in Phase 2.5 für k).

## Zukunft (Roadmap)
- ✅ Composite Index Prefiltering (mehrspaltig) – Phase 2.5 abgeschlossen
- Distanz-Metriken für PROXIMITY in Metern (aktuell einfache euklidische Projektion).
- LET Rückgabe von numerischen Similarity/Proximity Werten in generischen Ausdrücken (Aggregation).
- Erweiterter Cost Estimator mit Statistikprofilen (Histogramme, kumulative Verteilungen).
- Adaptive Overfetch-Steuerung basierend auf Trefferqualität.
- Konfigurierbare Kostenmodell-Parameter (`config:hybrid_query`).

## Troubleshooting
- Leere Ergebnisliste trotz vorhandener Dokumente: Prüfe Indexexistenz & Datentypen (String vs Zahl) in Prädikaten.
- Langsame Query: Reduziere `overfetch` oder erhöhe Selektivität durch zusätzliche Gleichheitsprädikate.
- Unterschiedliche Sortierung vs Erwartung: Prüfe Vektordimension; Mixed Dimensions werden ignoriert.

## Beispiel-End-to-End (Vector+Geo Setup)
```cpp
// Index Setup
sec.createIndex("hotels", "city");
sec.createRangeIndex("hotels", "stars");
spatial.createSpatialIndex("hotels", "location");
vectorIndex.load("hotels.embedding", /*dim=*/384);

// Query
std::string q = R"(
FOR doc IN hotels
  FILTER ST_Within(doc.location, [13.4,52.5,13.6,52.7])
  FILTER doc.city == "Berlin" AND doc.stars >= 4
  SORT SIMILARITY(doc.embedding, [ /* 384 floats */ ], 10 ) DESC
  LIMIT 10
  RETURN doc
)";
auto [st, json] = executeAql(q, engine);
```

## Lizenz / Kompatibilität
Alle Phase 2 Erweiterungen sind rückwärtskompatibel; ältere AQL Queries laufen unverändert.
