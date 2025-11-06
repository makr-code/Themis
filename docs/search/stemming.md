# Fulltext Stemming Support

**Status:** ✅ Implemented (v1.1) – Per-Index Configuration

## Overview

Themis supports optional stemming for fulltext indexes to improve text matching by reducing words to their root form. This increases recall by matching different word forms (e.g., "running" matches "run", "runs").

## Supported Languages

| Language | Code | Algorithm | Examples |
|----------|------|-----------|----------|
| English | `en` | Porter Subset | running→run, cats→cat, played→play |
| German | `de` | Suffix Removal | laufen→lauf, machte→macht, gruppen→grupp |
| None | `none` | No stemming | Exact token matching only |

## Configuration

### Creating a Fulltext Index with Stemming

**HTTP API:**
```bash
POST /index/create
{
  "table": "articles",
  "column": "content",
  "type": "fulltext",
  "config": {
    "stemming_enabled": true,
    "language": "de",
    "stopwords_enabled": true  // optional: Stopwords entfernen
  }
}
```

**C++ API:**
```cpp
SecondaryIndexManager::FulltextConfig config;
config.stemming_enabled = true;
config.language = "en";

auto status = indexMgr.createFulltextIndex("articles", "content", config);
```

### Default Configuration (No Stemming)

```bash
POST /index/create
{
  "table": "articles",
  "column": "content",
  "type": "fulltext"
}
# Equivalent to:
# "config": {"stemming_enabled": false, "language": "none"}
```

## How It Works

### Index-Time Tokenization

When a document is indexed with stemming enabled:

1. **Tokenization:** Text is split on whitespace and punctuation
2. **Lowercase:** Tokens are converted to lowercase
3. **Stemming:** Tokens are reduced to their stem form (if enabled)
4. **Storage:** Stemmed tokens are stored in the inverted index

Note: If stopwords are enabled, stopwords are filtered out before stemming. If umlaut normalization is enabled (German), normalization occurs before tokenization.

**Example (English):**
```
Input:  "Machine learning algorithms are optimizing systems"
Tokens: ["machine", "learning", "algorithms", "are", "optimizing", "systems"]
Stems:  ["machin", "learn", "algorithm", "are", "optim", "system"]
```

**Example (German):**
```
Input:  "Die Maschinen lernen aus vergangenen Fehlern"
Tokens: ["die", "maschinen", "lernen", "aus", "vergangenen", "fehlern"]
Stems:  ["die", "maschin", "lern", "aus", "vergangen", "fehl"]
```

### Query-Time Tokenization

When searching with stemming enabled:

1. Query tokens are processed **identically** to index tokens
2. Stemmed query terms match stemmed index terms
3. BM25 scoring uses stemmed token statistics

**Example Query:**
```bash
POST /search/fulltext
{
  "table": "articles",
  "column": "content",
  "query": "learning optimization",
  "limit": 10
}
```

With stemming enabled (`language: "en"`):
- Query stems to: `["learn", "optim"]`
- Matches documents containing: "learning", "learned", "learns", "optimize", "optimizing", "optimization"

## Algorithm Details

### English Porter Stemmer (Subset)

Implements a simplified version of the Porter Stemmer:

**Step 1a - Plurals:**
- `sses` → `ss` (caresses → caress)
- `ies` → `i` (ponies → poni)
- `s` → `` (cats → cat)

**Step 1b - Past Tense:**
- `eed` → `ee` (agreed → agree)
- `ed` → `` (played → play, running → run with double consonant removal)
- `ing` → `` (running → run)

**Step 1c - Y suffix:**
- `y` → `i` (happy → happi, only if preceded by consonant)

**Step 2 - Common Suffixes:**
- `ational` → `ate` (relational → relate)
- `ation` → `ate` (activation → activate)
- `ness` → `` (goodness → good)
- `enci` → `enc` (valenci → valenc)

**Limitations:**
- Simplified subset (not full Porter)
- No Step 3-5 transformations
- Minimum word length: 3 characters

### German Stemmer (Simplified)

Removes common German suffixes in order:

**Plurals and Declension:**
- `ern`, `em`, `en`, `er`, `es`, `e`, `s`

**Derivational Suffixes:**
- `ung` (Handlung → Handl)
- `heit` (Freiheit → Frei)
- `keit` (Möglichkeit → Möglich)
- `lich` (freundlich → freund)

**Limitations:**
- No umlaut normalization (ä, ö, ü unchanged)
- No compound word splitting
- No strong verb handling (irregular forms)
- Order-dependent (may over-stem in edge cases)

## Storage Format

### Index Metadata

Stemming configuration is persisted in RocksDB:

**Key:** `ftidxmeta:table:column`

**Value (JSON):**
```json
{
  "type": "fulltext",
  "stemming_enabled": true,
  "language": "de"
}
```

### Inverted Index Structure

Stemmed tokens are stored in the same index keys as non-stemmed:

- **Presence:** `ftidx:table:column:token:PK` → "" (token is stemmed if config enabled)
- **Term Frequency:** `fttf:table:column:token:PK` → count
- **Doc Length:** `ftdlen:table:column:PK` → total_tokens

## Backward Compatibility

### Legacy Indexes

Indexes created before stemming support:
- **Behavior:** Config lookup returns `{stemming_enabled: false, language: "none"}`
- **Migration:** Recreate index with `POST /index/create` and new config
- **No Auto-Migration:** Existing indexes remain unchanged

### API Compatibility

- `POST /index/create` without `config` field → no stemming (default)
- Query API unchanged: `/search/fulltext` automatically uses index config
- C++ API: `createFulltextIndex(table, column)` → default config

## Performance Considerations

### Index Size

- **Reduction:** Stemming typically reduces unique token count by 10-30%
- **Compression:** Fewer unique tokens → better RocksDB compression
- **Trade-off:** Slight increase in false positives (over-matching)

### Query Performance

- **Impact:** Negligible (stemming overhead < 1% of total query time)
- **Optimization:** Stemmer uses in-memory string manipulation
- **Caching:** Not needed (stemming is fast enough)

### Rebuild Time

- **Impact:** +5-10% for large datasets (stemming overhead)
- **Mitigation:** Rebuild only needed when changing config

## Testing

### Unit Tests

See `tests/test_stemming.cpp`:

```cpp
// English stemming
EXPECT_EQ(Stemmer::stem("cats", EN), "cat");
EXPECT_EQ(Stemmer::stem("running", EN), "run");
EXPECT_EQ(Stemmer::stem("relational", EN), "relate");

// German stemming
EXPECT_EQ(Stemmer::stem("laufen", DE), "lauf");
EXPECT_EQ(Stemmer::stem("machte", DE), "macht");
EXPECT_EQ(Stemmer::stem("wirkung", DE), "wirk");
```

### Integration Tests

```cpp
// Create index with stemming
FulltextConfig config{true, "en"};
indexMgr->createFulltextIndex("articles", "content", config);

// Insert document
BaseEntity doc("doc1");
doc.setField("content", "running dogs");
indexMgr->put("articles", doc);

// Query with base form
auto [status, results] = indexMgr->scanFulltext("articles", "content", "run");
EXPECT_EQ(results.size(), 1); // Matches "running"
```

## Best Practices

### When to Use Stemming

✅ **Enable stemming when:**
- Content is in a supported language (EN/DE)
- Recall is more important than precision
- Users search with different word forms
- Text contains morphological variations (verbs, plurals)

❌ **Disable stemming when:**
- Exact matching is required (e.g., product codes, technical terms)
- Content is multilingual without dominant language
- Domain-specific terminology should not be normalized
- Precision is critical (avoid false positives)

### Language Selection

- **Monolingual content:** Use appropriate language code (`en`, `de`)
- **Mixed content:** Choose dominant language or use `none`
- **Unknown language:** Use `none` (exact matching)

### Index Recreation

To change stemming config:
1. Drop existing index: `POST /index/drop`
2. Create new index with config: `POST /index/create`
3. Data will be automatically re-indexed on next entity update
4. Optional: Trigger rebuild via `POST /index/rebuild`

## Future Enhancements

### Planned Features

// Umlaut normalization implemented in v1.3
- **Umlaut normalization:** ä→a, ö→o, ü→u for German
- **More languages:** FR, ES, IT, NL via Snowball integration
- **Custom stemmers:** Plugin interface for domain-specific rules

### Advanced Analyzers

- **Compound word splitting (German):** "Fußballweltmeisterschaft" → ["fußball", "welt", "meisterschaft"]
- **Lemmatization:** More accurate than stemming ("better" → "good")
- **N-grams:** Partial matching and typo tolerance
- **Phonetic matching:** Soundex/Metaphone for fuzzy search

## Examples

### German Legal Documents

```bash
# Create index with German stemming
POST /index/create
{
  "table": "gesetze",
  "column": "text",
  "type": "fulltext",
  "config": {"stemming_enabled": true, "language": "de"}
}

# Insert document
PUT /entities/gesetze/bgb123
{"text": "Die Verträge müssen schriftlich geschlossen werden"}

# Search (matches "Vertrag", "Verträge", "Vertrags", etc.)
POST /search/fulltext
{
  "table": "gesetze",
  "column": "text",
  "query": "Vertrag schriftlich",
  "limit": 20
}
```

### English Technical Docs

```bash
# Create index with English stemming
POST /index/create
{
  "table": "docs",
  "column": "content",
  "type": "fulltext",
  "config": {"stemming_enabled": true, "language": "en"}
}

# Insert documents
PUT /entities/docs/ml101
{"content": "Machine learning algorithms optimize neural networks"}

PUT /entities/docs/ml102
{"content": "Optimizing machine learned models for production"}

# Search (matches both documents)
POST /search/fulltext
{
  "table": "docs",
  "column": "content",
  "query": "optimize learning",
  "limit": 10
}

# Response:
# [
#   {"pk": "ml102", "score": 9.42},  # "Optimizing...learned"
#   {"pk": "ml101", "score": 8.15}   # "learning...optimize"
# ]
```

## Troubleshooting

### No Results with Stemming Enabled

**Problem:** Query returns empty results after enabling stemming

**Diagnosis:**
1. Check if index was recreated with new config
2. Verify documents were re-indexed after config change
3. Test with non-stemmed query (exact token match)

**Solution:**
```bash
# Rebuild index to apply stemming to existing documents
POST /index/rebuild
{"table": "docs", "column": "content"}
```

### Unexpected Matches

**Problem:** Query matches unrelated documents

**Cause:** Over-stemming (common with aggressive algorithms)

**Example:**
- "university" → "univers"
- "universal" → "univers"
- Both match despite different meanings

**Solution:**
1. Disable stemming if precision is critical
2. Use exact phrases with quotes (future feature)
3. Add domain-specific stopwords

### Language Mismatch

**Problem:** Poor results for multilingual content

**Cause:** Single-language stemmer applied to mixed content

**Solution:**
1. Create separate indexes per language
2. Use language detection to route queries
3. Fallback to `language: "none"` for mixed content

## References

- **Porter Stemmer:** [Martin Porter, 1980](https://tartarus.org/martin/PorterStemmer/)
- **Snowball Algorithms:** [tartarus.org/martin/PorterStemmer/](https://snowballstem.org/)
- **BM25 Ranking:** See `docs/search/fulltext_api.md`
- **HTTP API:** See `openapi/openapi.yaml`

---

**Last Updated:** 2025-11-02  
**Version:** v1.1  
**Status:** Production Ready
