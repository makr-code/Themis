# Themis Search Performance Tuning Guide

**Version:** 1.0  
**Datum:** 7. November 2025  
**Zielgruppe:** DevOps, Database Administrators, Performance Engineers

---

## Übersicht

Dieser Guide beschreibt Best Practices und Tuning-Parameter für optimale Performance bei Fulltext-, Vector- und Hybrid-Suchen in Themis.

---

## 1. Fulltext Search (BM25)

### BM25 Parameter Tuning

**Standard-Parameter:**
```json
{
  "k1": 1.2,
  "b": 0.75
}
```

**Parameter-Bedeutung:**

- **k1 (Term Saturation)**: Kontrolliert, wie stark wiederholte Terme gewichtet werden
  - **Niedriger (0.5-1.0)**: Reduziert Gewicht wiederholter Terme → besser für kurze Dokumente
  - **Standard (1.2)**: Balanced für die meisten Anwendungsfälle
  - **Höher (1.5-2.0)**: Erhöht Gewicht wiederholter Terme → besser für lange Dokumente
  
- **b (Length Normalization)**: Kontrolliert Dokumentlängen-Normalisierung
  - **0.0**: Keine Normalisierung → lange Dokumente bevorzugt
  - **0.75** (Standard): Balanced normalization
  - **1.0**: Volle Normalisierung → kurze Dokumente bevorzugt

**Anwendungsfälle:**

| Use Case | k1 | b | Begründung |
|----------|----|----|------------|
| Kurze Tweets/Messages | 1.0 | 0.5 | Weniger Längen-Bias, moderate Term-Saturation |
| Standard Artikel | 1.2 | 0.75 | Default, balanced für gemischte Längen |
| Lange Dokumente (Bücher) | 1.5 | 0.9 | Höhere Saturation, starke Längen-Normalisierung |
| FAQ/Q&A | 0.8 | 0.6 | Kurze Queries, kurze Antworten |

### Limit-Parameter Optimization

**Query Limit:**
```json
POST /search/fulltext
{
  "query": "machine learning",
  "limit": 100  // Kandidaten-Limit
}
```

**Empfehlungen:**
- **Small Datasets (<10k docs)**: limit=1000 (default) ist ausreichend
- **Medium Datasets (10k-100k)**: limit=500 für bessere Performance
- **Large Datasets (>100k)**: limit=200-300, kombiniert mit strukturellen Filtern

**Trade-off:**
- Niedrigerer Limit = schneller, aber möglicherweise schlechtere Top-K Qualität
- Höherer Limit = langsamer, aber bessere Recall-Garantie

### Index Configuration

**Stemming aktivieren für bessere Recall:**
```json
{
  "stemming_enabled": true,
  "language": "en"  // oder "de"
}
```

**Wann Stemming nutzen:**
- ✅ User-generierte Queries (verschiedene Wortformen)
- ✅ Lange Dokumente mit variierender Sprache
- ❌ Exakte Suchen (z.B. Code, IDs, Produktnamen)
- ❌ Mehrsprachige Korpora ohne Sprachfilter

**Stopwords:**
```json
{
  "stopwords_enabled": true,
  "stopwords": ["z.b.", "bzw."]  // Custom für Domain
}
```

**Impact:**
- Index Size: -10-15% durch Stopword-Removal
- Query Speed: +5-10% durch weniger Kandidaten
- Recall: Minimal impact bei häufigen Terms

---

## 2. Vector Search (HNSW)

### efSearch Parameter

**Definition:** efSearch kontrolliert die Suchtiefe im HNSW-Graph

**Standard:** 50

**Tuning Guide:**

| efSearch | Recall@10 | Latency | Use Case |
|----------|-----------|---------|----------|
| 20 | ~85% | 1-2ms | Real-time recommendations (speed critical) |
| 50 | ~95% | 3-5ms | **Default**, balanced precision/speed |
| 100 | ~98% | 8-12ms | High-precision search |
| 200 | ~99.5% | 20-30ms | Offline batch processing |

**Empfehlung:**
```python
# Development/Testing
efSearch = 50

# Production (latency-critical)
efSearch = 30-40  # Adjust based on acceptable recall drop

# Production (quality-critical)
efSearch = 80-120

# Offline analytics
efSearch = 150-200
```

**Trade-off Analyse:**
- **2x efSearch** ≈ **+1.5-2% recall**, **+2x latency**
- Diminishing returns ab efSearch > 150

### M Parameter (Index Construction)

**Definition:** M kontrolliert die Anzahl Verbindungen pro Node im HNSW-Graph

**Standard:** 16

**Impact:**

| M | Index Size | Build Time | Query Latency | Recall |
|---|-----------|-----------|--------------|--------|
| 8 | 1x | 1x | +20% | -2% |
| 16 | 1.5x | 1.5x | Baseline | Baseline |
| 32 | 2.2x | 2.5x | -15% | +1% |
| 64 | 3.5x | 4x | -25% | +1.5% |

**Empfehlung:**
- **Small datasets (<100k vectors)**: M=16 (default)
- **Large datasets (>1M vectors)**: M=32 für bessere Connectivity
- **Ultra-large (>10M vectors)**: M=48-64 + Quantization

**Rebuild nicht nötig:** M ist ein Build-time Parameter, efSearch ist runtime.

---

## 3. Hybrid Search (Text + Vector Fusion)

### RRF (Reciprocal Rank Fusion)

**k_rrf Parameter:**
```json
POST /search/hybrid
{
  "fusion_method": "rrf",
  "k_rrf": 60
}
```

**Tuning:**

| k_rrf | Effekt | Use Case |
|-------|--------|----------|
| 20 | Starke Bevorzugung von Top-Ranks | Text und Vector hochkorreliert |
| 60 | **Default**, balanced fusion | Standard-Anwendungsfälle |
| 100 | Smoothere Fusion, weniger Rank-Bias | Text und Vector schwach korreliert |

**Formel:**
```
RRF_score = Σ 1/(k + rank_i)
```

**Empfehlung:**
- Start with k=60
- If text & vector give similar results → Lower k (40-50)
- If text & vector diverge → Higher k (80-100)

### Weighted Fusion

**weight_text Parameter:**
```json
POST /search/hybrid
{
  "fusion_method": "weighted",
  "weight_text": 0.7,
  "weight_vector": 0.3
}
```

**Tuning by Use Case:**

| Use Case | weight_text | weight_vector | Begründung |
|----------|-------------|---------------|------------|
| Keyword-focused search | 0.8 | 0.2 | User knows exact terms |
| Semantic search | 0.3 | 0.7 | Conceptual similarity important |
| Balanced hybrid | 0.5 | 0.5 | Default, equal importance |
| Q&A systems | 0.4 | 0.6 | Meaning > exact terms |
| Code search | 0.7 | 0.3 | Syntax matters |

**A/B Testing empfohlen:**
```bash
# Test verschiedene Weights
for w in 0.3 0.5 0.7; do
  POST /search/hybrid \
    -d '{"weight_text": '$w', "weight_vector": '$(echo "1-$w" | bc)'}'
done
```

---

## 4. Query Optimization

### LIMIT früh setzen

**Schlecht:**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "AI")
  SORT BM25(doc) DESC
  LIMIT 10
  RETURN doc
```

**Gut:**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "AI", 100)  // Kandidaten begrenzen
  SORT BM25(doc) DESC
  LIMIT 10
  RETURN doc
```

### Strukturelle Filter kombinieren

**Optimal:**
```aql
FOR doc IN articles
  FILTER doc.year >= 2023  // Index-Scan zuerst
  FILTER FULLTEXT(doc.content, "AI")
  LIMIT 10
  RETURN doc
```

**Warum:** Strukturelle Filter (year) reduzieren Kandidatenmenge für FULLTEXT

---

## 5. Index Maintenance

### Rebuild-Strategie

**Wann Rebuild nötig:**
- Große Datenmengen gelöscht (>20% des Index)
- Stemming/Stopword-Konfiguration geändert
- Vector Index fragmentiert (nach vielen Deletes)

**Rebuild Workflow:**
```bash
# 1. Neuen Index mit v2-Name erstellen
POST /index/create {"table": "docs", "column": "text", "type": "fulltext", "name": "text_v2"}

# 2. Traffic auf v2 umleiten (Zero-downtime)
# 3. Alten Index v1 löschen
DELETE /index/drop {"table": "docs", "column": "text", "name": "text_v1"}
```

**Automatic Rebuild Trigger (Future):**
- Delete-Ratio > 30% → Auto-rebuild
- Index fragmentation metric > threshold

---

## 6. Performance Benchmarks

### Fulltext Search

**Dataset:** 100k articles, avg 500 words/doc

| Query Length | Limit | Latency (p50) | Latency (p99) |
|--------------|-------|---------------|---------------|
| 1 token | 1000 | 8ms | 15ms |
| 3 tokens | 1000 | 12ms | 25ms |
| 5 tokens | 1000 | 18ms | 35ms |
| 3 tokens | 100 | 5ms | 10ms |

### Vector Search

**Dataset:** 1M vectors, 768 dimensions

| efSearch | Recall@10 | Latency (p50) | Latency (p99) |
|----------|-----------|---------------|---------------|
| 50 | 95.2% | 4ms | 8ms |
| 100 | 98.1% | 9ms | 18ms |
| 200 | 99.4% | 22ms | 45ms |

### Hybrid Search

**Fusion Overhead:**
- RRF: +2-3ms vs. separate queries
- Weighted: +1-2ms vs. separate queries

**Target:** <2× slowdown compared to single-modality search ✅ ACHIEVED

---

## 7. Monitoring

### Key Metrics

**Fulltext:**
```
fulltext_query_duration_ms
fulltext_candidate_count
fulltext_index_size_bytes
```

**Vector:**
```
vector_query_duration_ms
vector_index_dimension
vector_index_ef_search
```

**Hybrid:**
```
hybrid_fusion_duration_ms
hybrid_text_weight
hybrid_vector_weight
```

### Alerting Thresholds

```yaml
alerts:
  - name: HighFulltextLatency
    condition: fulltext_query_duration_ms.p99 > 100ms
    action: Check index fragmentation, consider rebuild
    
  - name: LowVectorRecall
    condition: vector_recall_at_10 < 0.90
    action: Increase efSearch or M parameter
    
  - name: HybridFusionSlow
    condition: hybrid_fusion_duration_ms.p99 > 50ms
    action: Reduce candidate counts (limit parameter)
```

---

## 8. FAQ

**Q: Wie oft sollte ich Indizes rebuilden?**  
A: Bei stabilen Daten: Nie. Bei vielen Deletes (>20%): Alle 3-6 Monate oder automatisch per Trigger.

**Q: Ist Stemming immer besser?**  
A: Nein. Bei exakten Suchen (Code, IDs) verschlechtert Stemming die Precision. A/B-Test empfohlen.

**Q: Wie wähle ich zwischen RRF und Weighted Fusion?**  
A: RRF ist robuster ohne Hyperparameter-Tuning. Weighted erlaubt mehr Kontrolle, erfordert aber Domain-Wissen.

**Q: Was ist der Memory-Impact von höherem M?**  
A: M=32 benötigt ca. 2x RAM vs. M=16. Für >1M Vektoren: Quantization (SQ8) empfohlen.

**Q: Kann ich efSearch zur Laufzeit ändern?**  
A: Ja, efSearch ist ein Query-Parameter. M ist Build-time only.

---

## 9. Checkliste für Production

- [ ] BM25 Parameter getestet (k1, b) für Use Case
- [ ] Stemming enabled/disabled based on Query-Typ
- [ ] LIMIT-Parameter optimiert (100-500 für große Datasets)
- [ ] efSearch auf 30-50 für latency-critical apps
- [ ] Hybrid weights per A/B-Test validiert
- [ ] Monitoring & Alerting aktiv
- [ ] Rebuild-Strategie dokumentiert
- [ ] Fallback bei Index-Ausfall definiert

---

## Referenzen

- BM25 Parameter Analysis: Robertson & Zaragoza (2009)
- HNSW efSearch Tuning: Malkov & Yashunin (2018)
- RRF k Parameter: Cormack, Clarke, Büttcher (2009)
