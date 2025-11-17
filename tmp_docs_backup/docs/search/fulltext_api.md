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
  "type": "fulltext",
  "config": {
    "stemming_enabled": true,
    "language": "de",  // en | de | none
    "stopwords_enabled": true,
    "stopwords": ["z.b."]  // optional, zusätzliche Stopwords (lowercase)
    ,"normalize_umlauts": true  // de: ä->a, ö->o, ü->u, ß->ss
  }
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

- Whitespace-basiert: Tokens werden bei Leerzeichen/Satzzeichen getrennt
- Lowercase: Alle Tokens in Kleinbuchstaben konvertiert
- Optionales Stemming (pro Index konfigurierbar):
  - Aktivieren via `POST /index/create` mit `type: "fulltext"` und `config.stemming_enabled=true`
  - Unterstützte Sprachen: `en` (Porter-Subset), `de` (vereinfachtes Suffix-Stemming)
  - Query-Tokenisierung nutzt immer dieselbe Konfiguration wie der Index
  
- Optionales Stopword-Filtering (pro Index konfigurierbar):
  - Aktivieren via `config.stopwords_enabled=true`
  - Standard-Listen für `en` und `de`; bei `language: "none"` wird nur die Custom-Liste angewendet
  - Eigene Stopwords via `config.stopwords: ["foo", "bar"]`
  - Stopwords werden vor dem Stemming entfernt

- Optionale Normalisierung (DE):
  - Aktivieren via `config.normalize_umlauts=true`
  - Ersetzt `ä→a`, `ö→o`, `ü→u`, `ß→ss` vor Tokenisierung/Stemming
  - Beispiel: "läuft" → "lauft" (erleichtert Suchanfragen ohne Sonderzeichen)

## Query-Semantik

- **AND-Logik**: Alle Query-Tokens müssen im Dokument vorkommen (Schnittmenge)
- **Scoring**: Dokumente mit höherer Termfrequenz und besserer Übereinstimmung erhalten höhere Scores
- **Sortierung**: Ergebnisse absteigend nach BM25-Score sortiert

### Phrasensuche ("…")

- Quoted Phrases im Query werden als exakte Phrasen interpretiert, z. B.:
  - `"deep learning" optimization`
- Kandidatenbildung erfolgt weiterhin über Tokens außerhalb der Anführungszeichen (AND-Logik).
- Danach werden Kandidaten per Post-Filter behalten, wenn alle Phrasen im Originalfeldtext als Substring vorkommen.
  - Case-insensitive Vergleich
  - Optional mit `normalize_umlauts=true`: `ä→a`, `ö→o`, `ü→u`, `ß→ss`
- Phrasen sind von Stemming/Stopwords nicht betroffen (Vergleich gegen den Feld-String, nicht gegen Tokens).

Einschränkungen (v1):
- Keine Positionslisten im Index – die Phrasenprüfung ist ein nachgelagerter Substring-Check und daher langsamer bei sehr großen Kandidatenmengen.
- Keine Wortgrenzen-/Satzzeichen-Logik; die Suche prüft eine einfache Teilzeichenkette nach Normalisierung/Lowercasing.

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
- ✅ Hybrid Search: Text + Vector Fusion (RRF/Weighted)
- ✅ Analyzer: Stemming (EN/DE) pro Index konfigurierbar
- ✅ Umlaut-/ß-Normalisierung (DE) optional pro Index
- ✅ Phrase Search: "exact match" Queries (v1, ohne Positionsindex)
- ✅ AQL Integration v1.3: `FILTER FULLTEXT(...) AND <predicates>`, `SORT BM25(doc) DESC`, `RETURN {doc, score: BM25(doc)}`
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

---

## AQL-Integration (v1.3)

**Status:** ✅ Implementiert (03.11.2025)

Fulltext-Suche kann auch über die AQL-Query-Language verwendet werden:

### Syntax

```aql
FOR doc IN table
  FILTER FULLTEXT(doc.column, "query" [, limit])
  // optional weitere Prädikate per AND
  // z. B. AND doc.year >= 2023
  RETURN doc
```

### Beispiele

**Einfache Suche:**
```aql
FOR article IN articles
  FILTER FULLTEXT(article.content, "machine learning")
  LIMIT 10
  RETURN {title: article.title, abstract: article.abstract}
```

**Phrasensuche:**
```aql
FOR paper IN research_papers
  FILTER FULLTEXT(paper.abstract, '"neural networks"')
  LIMIT 20
  RETURN paper
```

**Mit benutzerdefiniertem Limit:**
```aql
FOR doc IN documents
  FILTER FULLTEXT(doc.body, "AI optimization", 50)
  RETURN doc.title
```

**Sortierung nach Relevanz (BM25 in AQL):**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "machine learning")
  SORT BM25(doc) DESC
  LIMIT 10
  RETURN {title: doc.title, score: BM25(doc)}
```

**HTTP API-Aufruf:**
```bash
POST /query/aql
{
  "query": "FOR doc IN articles FILTER FULLTEXT(doc.content, \"machine learning\") LIMIT 10 RETURN doc"
}
```

### Funktionsdetails

- **Argumente:**
  - `field`: Spaltenname (muss Fulltext-Index haben)
  - `query`: Suchquery (Multi-Term mit AND-Logik, oder `"phrase"` für exakte Phrasen)
  - `limit`: Optional, default 1000 (max. Kandidaten für BM25-Ranking)
  
- **Ranking:** Automatisch nach BM25-Score sortiert (höchster zuerst)
- **Index-Requirement:** Fulltext-Index muss via `POST /index/create` erstellt sein
- **Features:** Nutzt Index-Konfiguration (Stemming, Stopwords, Normalisierung)

### Hinweise (v1.3)

- FULLTEXT kann mit AND kombiniert werden. OR-Kombinationen werden über DNF-Übersetzung unterstützt (ein FULLTEXT pro Disjunkt).
- BM25-Scores sind in AQL über `BM25(doc)` zugreifbar; sie werden bereitgestellt, wenn die Query den FULLTEXT-Ausführungspfad nutzt. Die End-to-End-Verdrahtung im AQL-Handler stellt dies sicher.

### Siehe auch

- **AQL-Syntax:** `docs/aql_syntax.md` - Vollständige AQL-Dokumentation
- **Index-Erstellung:** Abschnitt "Index-Erstellung" oben
- **Performance:** Abschnitt "Roadmap" unten für geplante Optimierungen

