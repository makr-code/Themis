# Suche & Relevanz – Gap-Analyse (Stand: 2025-11-02)

**Status Update (02.11.2025):** BM25 v1 und HTTP-API implementiert (Commit 94af141)

Ziel: Abgleich Dokumentation (Kapitel „Suche & Relevanz") mit dem aktuellen Quellcode. Fokus auf BM25/TF‑IDF, Hybrid (RRF / gewichtete Fusion) und Fulltext-Funktionalität.

## Zusammenfassung

- ✅ Fulltext mit BM25 Scoring: **Implementiert** (v1)
  - Inverted Index: vorhanden (SecondaryIndexManager::createFulltextIndex)
  - Tokenisierung: vorhanden (Whitespace + lowercase; keine Analyzer/Stemming)
  - TF/IDF Storage: TF pro (token, doc), DocLength pro doc – automatische Pflege bei put/delete
  - BM25 Ranking: scanFulltextWithScores liefert {pk, score} sortiert nach Relevanz (k1=1.2, b=0.75)
  - HTTP API: POST /search/fulltext mit Score-Antwort
  - Backward-kompatibel: scanFulltext (ohne Scores) weiterhin verfügbar
- ⏳ AQL BM25(doc) Funktion: **In Arbeit** (Task 3)
  - Parser-Erweiterung und Query-Engine-Integration geplant
- 🔲 Hybrid-Search (Vector + Text Fusion, RRF/Reranking): **Geplant** (Task 2)
  - Keine Score-Fusion, kein Reranking über Text- und Vektor-Ergebnisse
  - Implementation als POST /search/hybrid geplant

## Detaillierter Abgleich

- Doku-Verweise (offen):
  - docs/development/todo.md
    - „Hybrid-Search: Fulltext (BM25) + Vector Fusion; Reranking“ – offen
    - „BM25/TF-IDF Scoring“ – offen
    - „Scoring (BM25/TF-IDF) und Filterkombinationen (AND/OR/NOT)“ – offen
  - AQL-Doku/Seiten (generiert): Beispiele mit `BM25(doc)` (nur Beispiel, keine Implementierung)

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
- ⏳ AQL: `SORT BM25(doc) DESC` in Parser/Executor abbildbar (Task 3)
- Effort: ~2d (Implementation + Tests)

### 🔲 2) Hybrid-Fusion v1 – **IN ARBEIT** (Task 2)
- Normalisierung: Min‑Max pro Liste (Text/Vektor) oder robustere RRF (Σ 1/(k + rank))
- Fusion: score = α*BM25 + (1−α)*SIM oder RRF; Parameter in HTTP konfigurierbar
- API: POST /search/hybrid mit text_query, vector_query, fusion_mode (rrf|weighted), weight_text
- Effort: ~1-2d

### 🔲 3) Analyzer/Quality (später) – **BACKLOG** (Task 4)
- Stemming/N‑Grams (Snowball Porter für DE/EN), Phrase-/Prefix-Suche, Highlighting
- Effort: ~1-2d

## Akzeptanzkriterien (v1)
- Fulltext-Suche liefert `items` mit `{ pk, score }` (BM25); `SORT BY score DESC`
- Hybrid-Endpunkt/Operator liefert fusionierte Top‑k mit konsistenter Score-Skala
- Benchmarks auf Demo-Datensatz: BM25-Sortierung erkennbar, Hybrid verbessert NDCG@k ggü. Text‑only/Vector‑only

## Aufwandsschätzung
- BM25 v1: 2–4 Tage (Indexpflege + Query + Tests)
- Hybrid v1 (RRF/gewichtete Summe): 1–2 Tage (ohne Parser-Erweiterungen)
- Optional AQL-Erweiterungen: +1–2 Tage
