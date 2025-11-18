# Kostenmodelle für Hybrid Queries (Phase 2.5)

Diese Entwickler-Dokumentation beschreibt die vereinfachten Kostenmodelle, die zur Planwahl für Hybrid Queries in ThemisDB eingesetzt werden.

## Vector+Geo Kostenmodell

Eingabeparameter (`QueryOptimizer::VectorGeoCostInput`):
- `hasVectorIndex`: HNSW/ANN Index verfügbar
- `hasSpatialIndex`: R-Tree verfügbar
- `bboxRatio`: Fläche des Bounding Box Filters relativ zur Gesamtfläche (Selectivity)
- `prefilterSize`: Kandidatenmenge nach Equality/Range Prefilter (Index-Intersect)
- `spatialIndexEntries`: Anzahl der räumlich indexierten Entities
- `k`: gewünschte Top-k
- `vectorDim`: Vektordimension (Skalierung der Distanzkosten)
- `overfetch`: Multiplikator beim Vector-first Plan

Kostenformeln (abstrakte Einheiten):
```
C_vec = C_vec_base * (vectorDim / 128)
Spatial-first: cost = spatialCandidates * C_index_spatial + spatialCandidates * C_vec
Vector-first: cost = ANN(log(N)*dimScale) + (k * overfetch) * C_spatial_eval
```
Prefilter-Rabatt reduziert beide Kosten wenn `prefilterSize < 0.1 * spatialIndexEntries`.

Planwahl: `VectorThenSpatial`, wenn `costVectorFirst < costSpatialFirst`, sonst `SpatialThenVector`.

## Content+Geo Kostenmodell (Erweitertes Heuristikmodell)
Ziel: Auswahl zwischen zwei Plänen
1. Fulltext-Then-Spatial (FT zuerst, dann Geometrie-Filter + optional Distanz-Boost)
2. Spatial-Then-Fulltext (räumliche Kandidaten über SpatialIndex, dann naive Token-Match Evaluierung)

Eingaben (`ContentGeoCostInput`):
- `hasFulltextIndex`
- `hasSpatialIndex`
- `fulltextHits` (Schätzung, Fallback: `limit`)
- `bboxRatio` (Flächenverhältnis BBox/Total)
- `limit`

Konstanten (Tuning):
- `C_fulltext_base` Basis für logarithmische FT-Kosten
- `C_spatial_eval` Kosten pro räumlicher Kandidat ohne Index
- `C_spatial_index` Kosten pro Kandidat mit Index
- `smallBBoxBoost` Rabatt für sehr kleine BBox (<1%)

Formeln:
```
hits = max(1, fulltextHits or limit)
ftPhase = C_fulltext_base * ln(hits + 5)
spatialPhase_fulltext_first = hits * (hasSpatialIndex ? C_spatial_index : C_spatial_eval) * bboxRatio
costFulltextThenSpatial = ftPhase + spatialPhase_fulltext_first

spatialCandidates ≈ max(1, hits * bboxRatio)
spatialFetch = spatialCandidates * (hasSpatialIndex ? C_spatial_index : C_spatial_eval)
ftEvalCandidates = spatialCandidates * 0.25
costSpatialThenFulltext = spatialFetch + ftEvalCandidates

if bboxRatio < 0.01:
	costSpatialThenFulltext *= smallBBoxBoost

chooseFulltextFirst = costFulltextThenSpatial <= costSpatialThenFulltext
```

Tracer Attribute:
- `optimizer.cg.plan` (`fulltext_then_spatial` | `spatial_then_fulltext`)
- `optimizer.cg.cost_fulltext_first`
- `optimizer.cg.cost_spatial_first`

Grenzen:
- Spatial-first verwendet naiven Token-AND ohne BM25 Score.
- Bei großem `bboxRatio` kaum Vorteil gegenüber Fulltext-first.
- Sehr selektive BBox (<1%) bevorzugt Spatial-first.

Ausblick:
- Postinglisten-Längen speichern zur besseren `fulltextHits` Schätzung.
- Bloom-Filter / inverted skip lists für schnellere Token-Tests.
- Kostenmodell Einbezug von Distanz-Boost (falls aktiviert).

## Graph Shortest Path Kostenmodell (Dynamische Heuristik)

Ziel: Schätzung der Vertex-Expansion und frühzeitiger Abbruch bei Explosion.

Eingaben (`GraphPathCostInput`):
- `maxDepth`: maximale Traversierungstiefe
- `branchingFactor`: geschätzte durchschnittliche ausgehende Kanten pro Vertex
- `hasSpatialConstraint`: räumlicher Filter aktiv
- `spatialSelectivity`: Anteil der Vertices, die räumliche Bedingung erfüllen (0.0–1.0)

Dynamische Branching-Faktor Schätzung:
- Sampling über erste 2 Tiefen vom Startknoten
- Zähle ausgehende Kanten pro besuchtem Vertex
- `branchingEstimate = sampledEdges / sampledVertices`
- Fallback: 1.0 wenn keine Kanten gefunden

Formeln:
```
expanded = 1  // start node
for d = 1 to maxDepth:
    expanded += branchingFactor^d

if hasSpatialConstraint:
    expanded *= spatialSelectivity

timeMs = expanded * 0.02  // heuristic constant
```

Frühabbruch-Heuristik:
- Schwellwert: `ABORT_THRESHOLD = 1e6` (1 Million geschätzte Vertices)
- Wenn `estimatedExpandedVertices > ABORT_THRESHOLD`: Rückgabe leerer Pfadliste
- Attribut `optimizer.graph.aborted = true` gesetzt

Tracer Attribute:
- `optimizer.graph.branching_estimate`: gemessener Branching-Faktor
- `optimizer.graph.expanded_estimate`: geschätzte Anzahl expandierter Vertices
- `optimizer.graph.time_ms_estimate`: geschätzte Zeit in Millisekunden
- `optimizer.graph.aborted`: true wenn Frühabbruch erfolgt

Grenzen:
- Branching-Faktor-Schätzung berücksichtigt nur erste 2 Tiefen (kann bei heterogenen Graphen ungenau sein)
- Spatial Selectivity vereinfacht als globales bbox-Ratio (keine lokale Vertex-Dichte)
- Frühabbruch-Schwellwert statisch (nicht konfigurierbar)

Ausblick:
- Adaptive Schwellwerte basierend auf verfügbarem Memory
- Vertex-Dichte-Histogramme für präzisere Expansion-Schätzung
- Edge-Typ-spezifische Branching-Faktoren
- Inkrementelle Tiefenbegrenzung (iterative deepening) bei unbekanntem Branching

## Erweiterte Predicate Normalisierung
- Gleichheiten: Intersection von PK-Mengen über Einzel-Indizes.
- Ranges: Zusammenführung mehrfacher Bounds pro Feld, Scan über Range-Index.
- Composite-Indizes (zukünftig): Erkennung AND-Ketten aller beteiligten Spalten -> `scanKeysEqualComposite()`.

## Tuning-Punkte
- `C_vec_base`, `C_spatial_eval`, `C_index_spatial`, `prefilterDiscountFactor` aktuell hartkodiert.
- Geplante Migration zu konfigurierbaren Parametern in `config:hybrid_query`.

## Observability
- Span-Attribute: `optimizer.plan`, `optimizer.cost_spatial_first`, `optimizer.cost_vector_first`.
- Ermöglicht Analyse in Tracing Backend für Plan-Qualität.

## Zukunft
- Statistische Modelle (Histogramme, Kumulative Verteilung der Distanzwerte) zur verbesserten Annäherung.
- Adaptive Überfetch-Steuerung basierend auf Trefferqualität.
- Dynamische Branching-Faktor Schätzung für Graph Queries anhand realer Nachbarschaftsgrößen.