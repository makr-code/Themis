# Fulltext Search API

**Status:** ✅ Implementiert (v1) – BM25 Ranking mit HTTP Endpoint

## Übersicht

Die Fulltext-Suche in Themis nutzt BM25 (Okapi BM25) für relevanzbasiertes Ranking. Der Index wird automatisch bei Entity-Operationen (PUT/DELETE) gepflegt.

## Index-Erstellung

```bash
POST /index/create
{
  "table": "articles",
  "column": "content",
  "type": "fulltext"
}
```

## Fulltext-Suche mit BM25 Scores

```bash
POST /search/fulltext
{
  "table": "articles",
  "column": "content",
  "query": "machine learning optimization",
  "limit": 100
}
```

**Response:**
```json
{
  "count": 42,
  "table": "articles",
  "column": "content",
  "query": "machine learning optimization",
  "results": [
    {"pk": "art_123", "score": 8.42},
    {"pk": "art_456", "score": 7.91},
    {"pk": "art_789", "score": 6.15}
  ]
}
```

## BM25-Parameter

- **k1 = 1.2**: Term saturation (höhere Werte erhöhen Gewicht wiederholter Terms)
- **b = 0.75**: Document length normalization (0 = keine Normalisierung, 1 = volle Normalisierung)
- **IDF-Formel**: `log((N - df + 0.5) / (df + 0.5) + 1.0)` (stabilisiert)

**N** und **avgdl** werden aus dem Kandidaten-Universum (Vereinigung aller Token-Sets) berechnet (v1 Approximation).

## Tokenisierung

- **Whitespace-basiert**: Tokens werden bei Leerzeichen/Satzzeichen getrennt
- **Lowercase**: Alle Tokens in Kleinbuchstaben konvertiert
- **Keine Stemming** (v1): "running" ≠ "run" – Stemming geplant für v2

## Query-Semantik

- **AND-Logik**: Alle Query-Tokens müssen im Dokument vorkommen (Schnittmenge)
- **Scoring**: Dokumente mit höherer Termfrequenz und besserer Übereinstimmung erhalten höhere Scores
- **Sortierung**: Ergebnisse absteigend nach BM25-Score sortiert

## Index-Struktur

Der Fulltext-Index speichert:
- **Presence**: `ftidx:table:column:token:PK` → "" (Inverted Index)
- **Term Frequency**: `fttf:table:column:token:PK` → TF-Count
- **Doc Length**: `ftdlen:table:column:PK` → Total Tokens in Doc

## Backward Compatibility

Die alte API `scanFulltext()` (C++ intern) liefert weiterhin nur PKs ohne Scores. Für Score-basierte Suche `scanFulltextWithScores()` verwenden.

## Performance

- **Kandidaten-basiert**: BM25 wird nur für Kandidaten (Token-Schnittmenge) berechnet
- **O(|tokens| × |candidates|)**: Skaliert mit Query-Komplexität und Kandidatenmenge
- **Limit-Parameter**: Nutze `limit` für Top-k Retrieval (default: 1000)

## Roadmap

- ✅ BM25 v1 mit HTTP API
- 🔲 AQL Integration: `SORT BM25(doc) DESC`
- 🔲 Hybrid Search: Text + Vector Fusion (RRF/Weighted)
- 🔲 Analyzer: Stemming (Porter/Snowball für DE/EN)
- 🔲 Phrase Search: "exact match" Queries
- 🔲 Highlighting: Matched Terms in Response markieren

## Beispiel-Workflow

```bash
# 1. Index erstellen
POST /index/create {"table": "docs", "column": "text", "type": "fulltext"}

# 2. Dokumente einfügen
PUT /entities/docs/doc1 {"text": "Machine learning and deep neural networks"}
PUT /entities/docs/doc2 {"text": "Deep learning for computer vision"}
PUT /entities/docs/doc3 {"text": "Neural network optimization techniques"}

# 3. Suche mit Relevanz
POST /search/fulltext {
  "table": "docs",
  "column": "text", 
  "query": "deep learning neural",
  "limit": 10
}

# Ergebnis: doc2 > doc1 > doc3 (nach BM25 Score sortiert)
```
