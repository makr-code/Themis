# Suche & Relevanz – Gap-Analyse (Stand: 2025-11-02)

**Status Update (09.11.2025):** BM25 v1 inkl. HTTP-API implementiert (Commit 94af141) und AQL BM25(doc) Funktion integriert (Tests ✅)

Ziel: Abgleich Dokumentation (Kapitel „Suche & Relevanz") mit dem aktuellen Quellcode. Fokus auf BM25/TF‑IDF, Hybrid (RRF / gewichtete Fusion) und Fulltext-Funktionalität.

## Zusammenfassung

- ✅ Fulltext mit BM25 Scoring: **Implementiert** (v1)
  - Inverted Index: vorhanden (SecondaryIndexManager::createFulltextIndex)
  - Tokenisierung: vorhanden (Whitespace + lowercase; keine Analyzer/Stemming)
  - TF/IDF Storage: TF pro (token, doc), DocLength pro doc – automatische Pflege bei put/delete
  - BM25 Ranking: scanFulltextWithScores liefert {pk, score} sortiert nach Relevanz (k1=1.2, b=0.75)
  - HTTP API: POST /search/fulltext mit Score-Antwort
  - Backward-kompatibel: scanFulltext (ohne Scores) weiterhin verfügbar
- ✅ Hybrid-Search (Vector + Text Fusion): **Implementiert** (v1)
  - POST /search/fusion mit RRF und Weighted Modi
  - RRF: Reciprocal Rank Fusion (rank-based, robust)
  - Weighted: α*BM25 + (1-α)*VectorSim mit Min-Max Normalisierung
  - Flexible Kombination: Text-only, Vector-only, oder beide
- ✅ AQL BM25(doc) Funktion: **Implementiert** (Task abgeschlossen)
  - Parser-Erweiterung und Query-Engine-Integration umgesetzt
  - Expression Evaluator wertet BM25(doc) im SORT-Kontext aus

## Detaillierter Abgleich

- Doku-Verweise (offen):
  - docs/development/todo.md
    - „Hybrid-Search: Fulltext (BM25) + Vector Fusion; Reranking“ – offen
    - „BM25/TF-IDF Scoring“ – offen
    - „Scoring (BM25/TF-IDF) und Filterkombinationen (AND/OR/NOT)“ – offen
  - AQL-Doku/Seiten: Beispiele mit `BM25(doc)` sind nun implementiert und getestet

- Code (Kernausschnitte):
  - include/index/secondary_index.h / src/index/secondary_index.cpp
    - createFulltextIndex, scanFulltext, tokenize – implementiert
    - scanFulltext: liefert PKs (Schnittmenge), keine Score-Berechnung, kein Ranking
  - Kein Vorkommen/Stub für „BM25“, „TFIDF“, „RRF“, „fusion“, „rerank“ in include/** oder src/**

## Bewertung & Relevanz

- Relevanz hoch, wenn Text-Relevanzsortierung oder Hybrid-Suche (Text+Vektor) benötigt wird (ArangoSearch‑ähnlicher Use Case)
- Wenn Textsuche nur als grober Filter genutzt wird und Vektor dominiert, kann BM25/Hybrid in den Backlog; die aktuelle Fulltext-AND-Suche reicht dann nur für einfache Filter

## Vorschlag: Minimaler Umsetzungsplan

### ✅ 1) BM25 v1 (minimal-invasiv) – **ABGESCHLOSSEN** (94af141)
- ✅ Indexpflege: zusätzlich pro (token, doc) die Termfrequenz (TF) speichern; pro Dokument DocLength/AvgDL tracken
- ✅ Query: scanFulltextWithScores liefert Kandidaten mit BM25-Score; Top‑k sortiert zurückgegeben
- ✅ API: POST /search/fulltext mit `{"results": [{"pk": "...", "score": 3.14}, ...]}` Response
- ✅ AQL: `SORT BM25(doc) DESC` in Parser/Executor integriert
- Effort: ~2d (Implementation + Tests)

### ✅ 2) Hybrid-Fusion v1 – **ABGESCHLOSSEN** (e55508a)
- ✅ RRF (Reciprocal Rank Fusion): score = Σ 1/(k_rrf + rank), k_rrf=60 default
- ✅ Weighted: α*normalize(BM25) + (1-α)*normalize(VectorSim), Min-Max Normalisierung
- ✅ API: POST /search/fusion mit text_query+text_column und/oder vector_query
- ✅ Flexible Modi: Text-only, Vector-only, oder beide kombiniert
- ✅ Parameter: fusion_mode (rrf|weighted), weight_text, k_rrf, k (top-k)
- Effort: ~1.5d (Implementation + Tests)


### ✅ 3) AQL Integration – **ABGESCHLOSSEN** (Task 3)
- BM25(doc) Funktion für SORT
- Parser-Erweiterung in aql_parser.cpp
- Query-Engine: Score-Propagation und Evaluator-Hook
- Tests: 4/4 PASS

### 🔲 4) Analyzer/Quality (später) – **BACKLOG** (Task 4)
- Stemming/N‑Grams (Snowball Porter für DE/EN), Phrase-/Prefix-Suche, Highlighting
- Effort: ~1-2d

## Akzeptanzkriterien (v1)
- ✅ Fulltext-Suche liefert `items` mit `{ pk, score }` (BM25); sortiert nach Score DESC
- ✅ Hybrid-Endpunkt liefert fusionierte Top‑k mit RRF oder Weighted Fusion
- ✅ AQL: `SORT BM25(doc) DESC` für Fulltext-Queries
- 🔲 Benchmarks auf Demo-Datensatz: BM25-Sortierung validiert, Hybrid NDCG@k Evaluation

## Aufwandsschätzung
- ✅ BM25 v1: 2 Tage (Indexpflege + Query + Tests) – ABGESCHLOSSEN
- ✅ Hybrid v1 (RRF/Weighted): 1.5 Tage – ABGESCHLOSSEN
- ✅ AQL-Erweiterungen: 1–2 Tage (abgeschlossen)
- 🔲 Analyzer/Stemming: 1–2 Tage (Backlog)

