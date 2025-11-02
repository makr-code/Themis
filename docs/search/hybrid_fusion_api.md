# Hybrid Fusion Search API

**Status:** ✅ Implementiert (v1) – Text+Vector Fusion mit RRF und Weighted Modes

## Übersicht

Die Hybrid Fusion Search kombiniert Fulltext-Suche (BM25) und Vektor-Suche (HNSW/Brute-Force) in einer einheitlichen Ergebnisliste. Zwei Fusion-Modi werden unterstützt: RRF (Reciprocal Rank Fusion) und Weighted (gewichtete Score-Fusion).

## Endpoint

```
POST /search/fusion
```

## Fusion-Modi

### 1. RRF (Reciprocal Rank Fusion)

**Formel:** `score = Σ 1/(k_rrf + rank)`

**Eigenschaften:**
- Rank-basiert (keine Score-Normalisierung erforderlich)
- Robust gegen unterschiedliche Score-Skalen
- Bevorzugt Dokumente, die in beiden Listen hoch ranken
- Standard `k_rrf = 60` (empfohlen für Balance)

**Vorteile:**
- ✅ Keine Annahmen über Score-Verteilungen
- ✅ Einfach zu parametrisieren
- ✅ Bewährt in Information Retrieval (TREC)

**Nachteile:**
- ❌ Ignoriert absolute Score-Unterschiede
- ❌ Kann relevante Dokumente mit hohem Score in nur einer Modalität benachteiligen

### 2. Weighted (Gewichtete Fusion)

**Formel:** `score = α × normalize(BM25) + (1-α) × normalize(VectorSim)`

**Normalisierung:** Min-Max per Modalität
- Text: `(score - min_text) / (max_text - min_text)`
- Vector: `1 - (distance - min_dist) / (max_dist - min_dist)` (Distance → Similarity)

**Eigenschaften:**
- Score-basiert mit konfigurierbarer Gewichtung
- `α = weight_text` (0.0 bis 1.0, default: 0.5)
- Berücksichtigt Score-Magnitudes innerhalb jeder Modalität

**Vorteile:**
- ✅ Flexibles Tuning der Modalitäts-Gewichte
- ✅ Nutzt Score-Informationen für feinere Diskriminierung

**Nachteile:**
- ❌ Sensibel gegenüber Score-Verteilungen
- ❌ Erfordert Tuning von `α` für optimale Ergebnisse

## Request-Beispiele

### RRF: Text + Vector Fusion

```json
POST /search/fusion
{
  "table": "articles",
  "text_query": "machine learning optimization",
  "text_column": "content",
  "vector_query": [0.123, 0.456, 0.789, ...],
  "fusion_mode": "rrf",
  "k_rrf": 60,
  "k": 10,
  "text_limit": 1000,
  "vector_limit": 1000
}
```

### Weighted: Text-dominiert (70/30)

```json
POST /search/fusion
{
  "table": "docs",
  "text_query": "neural network architectures",
  "text_column": "description",
  "vector_query": [0.3, 0.1, ...],
  "fusion_mode": "weighted",
  "weight_text": 0.7,
  "k": 20
}
```

### Text-only (Weighted mit α=1.0)

```json
POST /search/fusion
{
  "table": "papers",
  "text_query": "deep learning survey",
  "text_column": "abstract",
  "fusion_mode": "weighted",
  "weight_text": 1.0,
  "k": 50
}
```

### Vector-only (Weighted mit α=0.0)

```json
POST /search/fusion
{
  "table": "images",
  "vector_query": [0.5, 0.2, ...],
  "fusion_mode": "weighted",
  "weight_text": 0.0,
  "k": 100
}
```

## Request-Parameter

| Parameter | Typ | Required | Default | Beschreibung |
|-----------|-----|----------|---------|--------------|
| `table` | string | ✅ | - | Tabellenname |
| `text_query` | string | ⚠️ | - | Fulltext-Query (mind. 1 Query erforderlich) |
| `text_column` | string | ⚠️ | - | Spalte mit Fulltext-Index |
| `vector_query` | float[] | ⚠️ | - | Query-Vektor (mind. 1 Query erforderlich) |
| `fusion_mode` | string | ❌ | `"rrf"` | Fusion-Modus: `"rrf"` oder `"weighted"` |
| `k` | int | ❌ | `10` | Top-k Ergebnisse nach Fusion |
| `k_rrf` | int | ❌ | `60` | RRF-Parameter (nur bei `fusion_mode="rrf"`) |
| `weight_text` | float | ❌ | `0.5` | Text-Gewicht 0.0-1.0 (nur bei `fusion_mode="weighted"`) |
| `text_limit` | int | ❌ | `1000` | Kandidaten-Limit für Text-Suche |
| `vector_limit` | int | ❌ | `1000` | Kandidaten-Limit für Vektor-Suche |

⚠️ **Mindestens eines erforderlich:** `text_query`+`text_column` ODER `vector_query`

## Response-Format

```json
{
  "count": 10,
  "fusion_mode": "rrf",
  "table": "articles",
  "text_count": 42,
  "vector_count": 87,
  "results": [
    {
      "pk": "art_123",
      "score": 0.0547
    },
    {
      "pk": "art_456",
      "score": 0.0423
    }
  ]
}
```

**Response-Felder:**
- `count`: Anzahl fusionierter Ergebnisse
- `fusion_mode`: Verwendeter Modus (`"rrf"` oder `"weighted"`)
- `table`: Tabelle
- `text_count`: Anzahl Text-Kandidaten (falls Text-Query vorhanden)
- `vector_count`: Anzahl Vektor-Kandidaten (falls Vector-Query vorhanden)
- `results`: Top-k Ergebnisse sortiert nach Fusion-Score (absteigend)

## Anwendungsfälle

### 1. Semantische Suche mit Keyword-Boost
**Szenario:** Vektor-basierte Ähnlichkeit mit Keyword-Filter

```json
{
  "fusion_mode": "weighted",
  "weight_text": 0.3,
  "text_query": "machine learning",
  "vector_query": [...],
  "k": 20
}
```

### 2. Robuste Multi-Modal Retrieval
**Szenario:** Gleichgewichtige Kombination ohne Score-Tuning

```json
{
  "fusion_mode": "rrf",
  "k_rrf": 60,
  "text_query": "optimization algorithms",
  "vector_query": [...],
  "k": 50
}
```

### 3. Keyword-dominierte Suche mit semantischem Fallback
**Szenario:** Primär BM25, Vektor als Secondary Signal

```json
{
  "fusion_mode": "weighted",
  "weight_text": 0.8,
  "text_query": "exact technical term",
  "vector_query": [...],
  "k": 10
}
```

### 4. Pure Vector Search via Fusion API
**Szenario:** Nur Vektor-Suche (konsistente API)

```json
{
  "fusion_mode": "weighted",
  "weight_text": 0.0,
  "vector_query": [...],
  "k": 100
}
```

## Performance-Hinweise

1. **Kandidaten-Limits:** `text_limit` und `vector_limit` kontrollieren Pre-Fusion-Kandidaten
   - Höhere Werte: bessere Recall, langsamere Fusion
   - Empfehlung: 10-100× des finalen `k`

2. **Fusion-Komplexität:**
   - RRF: O(|text_results| + |vector_results|) für Hash-Map + Sort
   - Weighted: gleiche Komplexität, zusätzlich Min-Max Normalisierung

3. **Index-Optimierung:**
   - Fulltext: Nutze `limit` in `scanFulltextWithScores` (bereits implementiert)
   - Vector: HNSW `efSearch` Parameter für Speed/Quality Trade-off

## Tuning-Empfehlungen

### RRF `k_rrf` Parameter

- **k_rrf = 10-30:** Starke Bevorzugung hoher Ranks (streng)
- **k_rrf = 60:** Balanced (Standardwert, TREC-empfohlen)
- **k_rrf = 100+:** Smoothere Fusion, weniger Rank-Penalisierung

### Weighted `weight_text` Parameter

- **0.0-0.2:** Vector-dominiert (semantische Suche)
- **0.3-0.5:** Balanced (Standard: 0.5)
- **0.6-0.8:** Text-dominiert (Keyword-Suche mit semantischem Boost)
- **0.9-1.0:** Primär BM25, minimaler Vektor-Einfluss

**Tuning-Strategie:**
1. Starte mit RRF (robust, keine Parameter)
2. Falls Modalität dominieren soll: wechsle zu Weighted mit `α`-Tuning
3. Evaluiere auf Representative Queries mit Relevance Judgments

## Limitationen & Roadmap

### Aktuelle Limitationen (v1)

- ❌ Keine Post-Fusion Reranking
- ❌ Keine Query-Zeit Feature-Weights (nur globales `α`)
- ❌ Keine Custom Similarity Functions
- ❌ Keine Cross-Encoder Integration

### Geplante Erweiterungen (v2+)

- 🔲 Learned Fusion: ML-basierte Score-Kombination
- 🔲 Query-dependent Weights: `α` basierend auf Query-Typ
- 🔲 Multi-Stage Retrieval: Fusion → Rerank Pipeline
- 🔲 Distribution-Aware Normalization (z.B. Z-Score statt Min-Max)
- 🔲 AQL Integration: `SEARCH FUSION ... USING TEXT ... VECTOR ...`

## Vergleich mit Alternativen

| Ansatz | Pros | Cons | Use Case |
|--------|------|------|----------|
| **RRF** | Robust, keine Tuning, rank-based | Ignoriert Score-Magnitudes | Default für Multi-Modal |
| **Weighted** | Flexibles Tuning, score-aware | Sensibel, braucht Normalisierung | Wenn Modalität dominieren soll |
| **CombSUM** | Einfach | Keine Normalisierung, skalenanfällig | ❌ Nicht empfohlen |
| **CombMNZ** | Bevorzugt Multi-Modal Matches | Komplex, wenig robust | ❌ Nicht implementiert |

## Beispiel-Workflow

```bash
# 1. Indizes erstellen
POST /index/create {"table": "papers", "column": "abstract", "type": "fulltext"}
POST /vector/index/config {"table": "papers", "dimension": 768, "metric": "COSINE"}

# 2. Dokumente einfügen mit Text + Vector
PUT /entities/papers/p1 {
  "abstract": "Deep learning for computer vision",
  "embedding": [0.1, 0.2, ..., 0.768]
}
PUT /entities/papers/p2 {
  "abstract": "Neural network optimization techniques",
  "embedding": [0.3, 0.4, ..., 0.768]
}

# 3. Hybrid Suche mit RRF
POST /search/fusion {
  "table": "papers",
  "text_query": "deep learning vision",
  "text_column": "abstract",
  "vector_query": [0.15, 0.25, ..., 0.768],
  "fusion_mode": "rrf",
  "k": 10
}

# Ergebnis: Dokumente die SOWOHL semantisch als auch keyword-basiert matchen ranken hoch
```

## Referenzen

- **RRF:** Cormack, Clarke, Büttcher. "Reciprocal Rank Fusion outperforms Condorcet and individual Rank Learning Methods." SIGIR 2009
- **Score Normalization:** Lee. "Analyses of Multiple Evidence Combination." SIGIR 1997
- **Hybrid Retrieval:** Ma et al. "A Hybrid Ranking Approach to E-Commerce Search." KDD 2019
