# Release Notes - AQL Fulltext Integration (v1.2)

**Datum:** 03. November 2025  
**Feature:** BM25-basierte Fulltext-Suche in AQL

## Übersicht

Die Fulltext-Suche ist jetzt vollständig in die AQL-Query-Language integriert. Entwickler können BM25-gerankte Volltextsuchen direkt in AQL-Queries verwenden, ohne auf separate HTTP-Endpoints zurückgreifen zu müssen.

## Neue Features

### FULLTEXT() Funktion

```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "machine learning")
  LIMIT 10
  RETURN doc
```

**Syntax:**
- `FULLTEXT(field, query [, limit])`
- `field`: Spalte mit Fulltext-Index
- `query`: Suchquery (Multi-Term oder `"phrase"`)
- `limit`: Optional (default: 1000)

**Features:**
- ✅ BM25-Ranking (k1=1.2, b=0.75)
- ✅ Automatische Sortierung nach Relevanz
- ✅ Stemming (EN/DE) aus Index-Konfiguration
- ✅ Stopword-Filtering
- ✅ Phrasensuche mit `"exact phrase"`
- ✅ Deutsche Umlaut-Normalisierung (ä→a, ö→o, ü→u, ß→ss)

## Implementierte Komponenten

### Parser (src/query/aql_parser.cpp)
- ✅ FunctionCallExpr bereits vorhanden
- Keine Änderungen nötig

### Translator (src/query/aql_translator.cpp)
- ✅ FULLTEXT-Funktionserkennung in `extractPredicates()`
- ✅ Argument-Validierung (2-3 Args)
- ✅ Column/Query/Limit-Extraktion
- ✅ `PredicateFulltext` Erstellung

### Query Engine (src/query/query_engine.cpp)
- ✅ Fulltext-Branch in `executeAndKeys()`
- ✅ Integration mit `scanFulltextWithScores()`
- ✅ PK-Extraktion aus BM25-Ergebnissen
- ✅ Tracing-Spans für Observability

### Tests (tests/test_aql_fulltext.cpp)
- ✅ 10 umfassende Tests
- ✅ Parser-Tests
- ✅ Translator-Tests  
- ✅ End-to-End Execution-Tests
- ✅ Phrasensuche-Tests
- ✅ Error-Handling-Tests

**Test-Ergebnisse:** 10/10 PASSED ✅

## Beispiele

### Einfache Multi-Term-Suche
```aql
FOR article IN articles
  FILTER FULLTEXT(article.content, "deep learning neural networks")
  LIMIT 20
  RETURN {title: article.title, year: article.year}
```

### Phrasensuche
```aql
FOR paper IN research_papers
  FILTER FULLTEXT(paper.abstract, '"machine learning"')
  RETURN paper
```

### Mit benutzerdefiniertem Limit
```aql
FOR doc IN documents
  FILTER FULLTEXT(doc.body, "AI optimization", 50)
  RETURN doc.title
```

## Einschränkungen (v1.2)

### Aktuell nicht unterstützt:
- ❌ Kombination mit AND/OR (in Entwicklung)
  ```aql
  // Wird noch nicht unterstützt:
  FILTER FULLTEXT(doc.content, "AI") AND doc.year >= 2023
  ```

- ❌ Score-Ausgabe in RETURN (geplant für v1.3)
  ```aql
  // Geplant:
  RETURN {doc, score: FULLTEXT_SCORE()}
  ```

- ❌ Highlighting (auf Roadmap)

### Workarounds:
- Für Filterung nach Fulltext: Erst FULLTEXT-Query, dann client-side Filter
- Für Scores: Nutze HTTP `/search/fulltext` Endpoint direkt

## Migration

**Keine Breaking Changes!** Bestehende AQL-Queries funktionieren unverändert.

**Neue Index-Anforderung:** Für FULLTEXT-Queries muss ein Fulltext-Index erstellt sein:

```bash
POST /index/create
{
  "table": "articles",
  "column": "content",
  "type": "fulltext",
  "config": {
    "stemming_enabled": true,
    "language": "en",
    "stopwords_enabled": true
  }
}
```

## Performance-Hinweise

- **BM25-Ranking:** O(N) wobei N = Kandidaten aus Token-Intersection
- **Phrasensuche:** Post-Filter via Substring-Check (keine Positional-Index noch)
- **Empfehlung:** Nutze `limit`-Parameter für große Corpora
- **Index-Nutzung:** Invertierter Index für Token-Lookup

## Dokumentation

- **AQL-Syntax:** `docs/aql_syntax.md` - Vollständige FULLTEXT-Dokumentation
- **Fulltext-API:** `docs/search/fulltext_api.md` - Index-Erstellung und Konfiguration
- **Tests:** `tests/test_aql_fulltext.cpp` - Beispiel-Code

## Nächste Schritte (Roadmap)

1. **FULLTEXT + AND/OR Kombinationen** (v1.3)
   - Erlaubt: `FILTER FULLTEXT(...) AND doc.year >= 2023`
   - Optimierung: Index-Filter vor BM25-Scoring

2. **Score-Funktion** (v1.3)
   - `FULLTEXT_SCORE()` in RETURN-Expression
   - Ermöglicht Scoring-basierte Logik

3. **Positional Index** (v2.0)
   - Echte Phrase-Matching mit Positionslisten
   - Proximity-Queries (NEAR/n)

4. **Highlighting** (v2.1)
   - `FULLTEXT_HIGHLIGHT(field, query)` 
   - Matched Terms markieren

## Credits

- **Implementierung:** makr-code
- **Tests:** Comprehensive coverage mit GTest
- **Dokumentation:** aql_syntax.md, fulltext_api.md aktualisiert
- **Integration:** Parser/Translator/Engine vollständig

---

**Versionen:**
- AQL: v1.2
- Fulltext-API: v1.2
- Themis Core: Compatible mit v1.x

**Status:** ✅ Production Ready (mit dokumentierten Einschränkungen)
