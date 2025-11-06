# Search & Relevance – Future Work

**Status:** v1 Complete (BM25 HTTP + Hybrid Fusion) – v2 Planning

## Implemented Features (v1)

### ✅ BM25 Fulltext Search (Commit 94af141)
- **API:** POST /search/fulltext
- **Scoring:** Okapi BM25 (k1=1.2, b=0.75)
- **Index:** TF/DocLength automatic maintenance
- **Response:** {pk, score} sorted by relevance
- **Tests:** 10/10 passed

### ✅ Hybrid Text+Vector Fusion (Commit e55508a)
- **API:** POST /search/fusion
- **Modes:** RRF (rank-based) and Weighted (score-based)
- **Flexibility:** Text-only, Vector-only, or combined
- **Normalization:** Min-Max for weighted, reciprocal rank for RRF
- **Tests:** No regressions in fulltext suite

### ✅ Stemming & Analyzer Extensions (v1.2)
- **Implementation:** Porter-Subset (EN), simplified suffix removal (DE)
- **Configuration:** Per-index via `POST /index/create` with:
  ```json
  {
    "type": "fulltext",
    "config": {
      "stemming_enabled": true,
      "language": "en"  // en | de | none
    }
  }
  ```
- **Index Maintenance:** Consistent tokenization in Put/Delete/Rebuild
- **Query-Time:** Automatically uses index config for query tokens
- **Storage:** Config persisted in `ftidxmeta:table:column` as JSON
- **Backward Compatible:** Default `{stemming_enabled: false, language: "none"}`
- **Tests:** 16/16 stemming tests passed + 10/10 fulltext regression tests
- **HTTP API:** `/index/create` with `type: "fulltext"` and optional `config`
- **OpenAPI:** Documented in openapi.yaml with examples
- **Stopwords:** Pro-Index konfigurierbar (Default-Listen EN/DE, Custom-Liste)

## Future Work (v2+)

### 🔲 AQL Integration: BM25(doc) Function

**Goal:** Enable BM25 scoring in AQL queries with SORT support

**Requirements:**
1. **Parser Extension** (aql_parser.cpp)
   - Register `BM25(doc_var)` as built-in function
   - Return type: numeric (double)

2. **Query Engine Integration** (query_engine.cpp)
   - Extend `ExecutionContext` with score map: `map<string, double> bm25_scores`
   - When FULLTEXT filter present: call `scanFulltextWithScores`, populate score map
   - Function evaluation: `BM25(doc)` returns `bm25_scores[doc.pk]` or 0.0

3. **SORT Integration**
   - `SORT BM25(doc) DESC` uses score field from context
   - Alternative: Implicit `_score` field for FULLTEXT results

**Example Query:**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "machine learning")
  SORT BM25(doc) DESC
  LIMIT 10
  RETURN {title: doc.title, score: BM25(doc)}
```

**Effort Estimate:** 1-2 days
- Parser extension: 2-4 hours
- ExecutionContext refactoring: 4-6 hours
- FULLTEXT integration: 4-6 hours
- Testing: 2-4 hours

**Complexity:** Medium
- Requires threading score context through query execution pipeline
- FULLTEXT operator currently not implemented in AQL (only HTTP)
- May conflict with existing filter evaluation order

**Priority:** Medium
- HTTP API is sufficient for most use cases
- AQL integration provides unified query language
- Useful for complex multi-stage queries (FILTER → SORT → LIMIT)

---

### 🔲 Advanced Analyzer Extensions

**Goal:** Extend stemming with additional linguistic features

**Potential Enhancements:**
1. ~~Stopword Filtering~~
  - Implemented in v1.2 (Default EN/DE + Custom per Index)

2. **Umlaut Normalization (German)**
   - Normalize "ä→a", "ö→o", "ü→u", "ß→ss"
   - Improves matching for search queries without special chars
   - Example: "läuft" → "lauft" (stems to "lauf")

3. **Compound Word Splitting (German)**
   - Split "Fußballweltmeisterschaft" → "fußball welt meisterschaft"
   - Critical for German precision/recall
   - Requires dictionary or ML-based approach

4. **Lemmatization (vs. Stemming)**
   - More accurate morphological analysis
   - "running" → "run", "better" → "good"
   - Requires POS tagging and lexicon

**Effort Estimate:** 2-5 days (depending on scope)
- Stopwords: 4-6 hours
- Umlaut normalization: 2-3 hours
- Compound splitting: 1-2 days (complex)
- Lemmatization: 2-3 days (requires NLP library)

**Complexity:** Medium-High
- Stopwords: Low
- Normalization: Low
- Compound splitting: High (ambiguity resolution)
- Lemmatization: High (dependency on NLP toolkit)

**Priority:** Medium
- Stopwords: High value/effort ratio
- Umlaut normalization: High for German content
- Compound splitting: Nice-to-have (complex)
- Lemmatization: Overkill for most use cases (stemming sufficient)

**Alternative Analyzers (Future):**
- N-Grams (for partial matching, typo tolerance)
- Phonetic matching (Soundex, Metaphone for fuzzy search)
- Synonym expansion
- Stop-word removal

---

### 🔲 Phrase Search

**Goal:** Support exact phrase matching (quoted queries)

**Example:**
```json
{
  "query": "\"machine learning\"",
  "match": "exact phrase only, not 'machine' and 'learning' separately"
}
```

**Requirements:**
- Store token positions in index (extend TF keys with position arrays)
- Phrase query parser: detect quoted strings
- Proximity verification: ensure tokens appear consecutively

**Effort:** 2-3 days

---

### 🔲 Query Highlighting

**Goal:** Return matched terms/snippets in response

**Example Response:**
```json
{
  "pk": "doc123",
  "score": 8.5,
  "highlights": {
    "content": "...with <em>machine learning</em> algorithms..."
  }
}
```

**Requirements:**
- Extract matched tokens from query
- Locate occurrences in document text
- Generate snippets with highlighting markup

**Effort:** 1-2 days

---

### 🔲 Learned Fusion (ML-based Ranking)

**Goal:** Replace hand-tuned fusion with learned weights

**Approach:**
- Collect query logs with relevance judgments
- Train LambdaMART/LightGBM ranker
- Features: BM25 score, Vector similarity, metadata signals
- Online serving: predict fusion weights per query

**Effort:** 1-2 weeks (requires ML infrastructure)

---

### 🔲 Multi-Stage Retrieval Pipeline

**Goal:** Efficient retrieval → reranking architecture

**Stages:**
1. **Retrieval** (fast, high recall): Fusion search with k=1000
2. **Reranking** (slow, high precision): Cross-encoder on top-100
3. **Diversification** (optional): MMR for result diversity

**Effort:** 2-3 days (without Cross-Encoder integration)

---

## Implementation Priority

**High Priority (v2):**
1. ✅ BM25 HTTP API (DONE)
2. ✅ Hybrid Fusion (DONE)
3. 🔲 Stemming (DE/EN) – **Next**
4. 🔲 AQL Integration – **After Stemming**

**Medium Priority (v3):**
5. 🔲 Phrase Search
6. 🔲 Query Highlighting
7. 🔲 Advanced Analyzers (N-Grams, Synonyms)

**Low Priority (v4+):**
8. 🔲 Learned Fusion
9. 🔲 Multi-Stage Reranking
10. 🔲 Query Expansion

## Testing Strategy

**Unit Tests:**
- Stemmer: token → stem mappings for DE/EN
- AQL Parser: BM25(doc) function parsing
- Query Engine: Score context propagation

**Integration Tests:**
- End-to-end AQL queries with FULLTEXT + SORT BM25
- Stemming: Query "running" matches docs with "run"
- Phrase search: Quoted vs. unquoted queries

**Performance Tests:**
- BM25 latency: 100k docs, 5-token queries (target: <50ms)
- Fusion overhead: Text+Vector vs. separate (target: <2× slowdown)
- Stemming impact: Index size increase (expect: +10-20%)

## Documentation TODOs

- [ ] AQL Syntax Guide: FULLTEXT operator, BM25(doc) function
- [ ] Index Configuration: Stemming options, language codes
- [ ] Performance Tuning: efSearch, k_rrf, weight_text recommendations
- [ ] Migration Guide: v1 → v2 (stemming index rebuild)

## References

- Snowball Stemmer: https://snowballstem.org/
- Okapi BM25: Robertson & Zaragoza (2009)
- RRF: Cormack, Clarke, Büttcher. SIGIR 2009
- LambdaMART: Burges (2010)

Nächste sinnvolle Schritte
Umlaut-/ß-Normalisierung (z. B. „läuft“ -> „lauft“) für DE verbessern.
Phrase Queries und Highlighting.
AQL-Integration: FULLTEXT-Operator + BM25 im Sort (geplant für V2).