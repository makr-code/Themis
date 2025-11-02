# Suche & Relevanz – Gap-Analyse (Stand: 2025-11-02)

Ziel: Abgleich Dokumentation (Kapitel „Suche & Relevanz“) mit dem aktuellen Quellcode. Fokus auf BM25/TF‑IDF, Hybrid (RRF / gewichtete Fusion) und Fulltext-Funktionalität.

## Zusammenfassung

- Fulltext (einfach): Implementiert
  - Inverted Index: vorhanden (SecondaryIndexManager::createFulltextIndex)
  - Tokenisierung: vorhanden (Whitespace + lowercase; keine Analyzer)
  - Suche: vorhanden (scanFulltext) – AND-Logik über Tokens, keine Scores, keine Sortierung
- BM25/TF‑IDF Scoring: Nicht implementiert
  - Keine Berechnung/Verwendung von TF/IDF, keine Ranking-Sortierung nach Relevanz
  - AQL-Beispiele („BM25(doc)“) sind Dokumentation/Plan, kein ausführbarer Pfad
- Hybrid-Search (Vector + Text Fusion, RRF/Reranking): Nicht implementiert
  - Keine Score-Fusion, kein Reranking über Text- und Vektor-Ergebnisse

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

1) BM25 v1 (minimal-invasiv)
- Indexpflege: zusätzlich pro (token, doc) die Termfrequenz (TF) speichern; pro Dokument DocLength/AvgDL tracken
- Query: scanFulltext(token) liefert Kandidaten; anschließend BM25-Score je PK berechnen und Top‑k sortiert zurückgeben
- API/AQL: Ergebnis um `score` ergänzen; `SORT BM25(doc) DESC` optional in Parser/Executor abbilden (oder als implizites „score“ Feld)

2) Hybrid-Fusion v1
- Normalisierung: Min‑Max pro Liste (Text/Vektor) oder robustere RRF (Σ 1/(k + rank))
- Fusion: score = α*BM25 + (1−α)*SIM oder RRF; Parameter in HTTP/AQL konfigurierbar

3) Analyzer/Quality (später)
- Stemming/N‑Grams, Phrase-/Prefix-Suche, Highlighting

## Akzeptanzkriterien (v1)
- Fulltext-Suche liefert `items` mit `{ pk, score }` (BM25); `SORT BY score DESC`
- Hybrid-Endpunkt/Operator liefert fusionierte Top‑k mit konsistenter Score-Skala
- Benchmarks auf Demo-Datensatz: BM25-Sortierung erkennbar, Hybrid verbessert NDCG@k ggü. Text‑only/Vector‑only

## Aufwandsschätzung
- BM25 v1: 2–4 Tage (Indexpflege + Query + Tests)
- Hybrid v1 (RRF/gewichtete Summe): 1–2 Tage (ohne Parser-Erweiterungen)
- Optional AQL-Erweiterungen: +1–2 Tage
