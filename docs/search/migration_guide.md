# Themis Search Migration Guide

**Version:** 1.0  
**Datum:** 7. November 2025  
**Zielgruppe:** Database Administrators, DevOps Engineers

---

## Übersicht

Dieser Guide beschreibt die Migration von Fulltext- und Vector-Indizes bei Konfigurations- oder Schema-Änderungen in Themis.

---

## Wann ist Migration nötig?

### Fulltext Index Migration

**Migration erforderlich bei:**
- ✅ Stemming aktivieren/deaktivieren
- ✅ Sprache ändern (en ↔ de)
- ✅ Stopword-Liste modifizieren
- ✅ Umlaut-Normalisierung aktivieren/deaktivieren

**Keine Migration nötig bei:**
- ❌ BM25-Parameter ändern (k1, b) - sind Query-time Parameter
- ❌ Limit-Parameter ändern - ist Query-time Parameter

### Vector Index Migration

**Migration erforderlich bei:**
- ✅ Dimensions ändern (768 → 1536)
- ✅ M-Parameter ändern (Build-time)
- ✅ Metric ändern (cosine ↔ euclidean ↔ dot)

**Keine Migration nötig bei:**
- ❌ efSearch ändern - ist Query-time Parameter

---

## Migration Strategies

### Strategy 1: Zero-Downtime (Dual Index)

**Vorteile:**
- ✅ Keine Service-Unterbrechung
- ✅ Rollback möglich
- ✅ A/B-Testing möglich

**Nachteile:**
- ❌ 2x Storage während Migration
- ❌ 2x Write-Load während Sync

**Workflow:**

```bash
# 1. Neuen Index mit v2-Konfiguration erstellen
POST /index/create
{
  "table": "articles",
  "column": "content",
  "type": "fulltext",
  "name": "content_v2",  # Neuer Name
  "config": {
    "stemming_enabled": true,  # Neue Config
    "language": "de"
  }
}

# 2. Daten in neuen Index kopieren (Batch)
# Option A: Via API (langsam, aber sicher)
GET /entities/articles?limit=1000
# Für jeden Batch:
POST /index/add_batch
{
  "index": "content_v2",
  "documents": [...]
}

# Option B: Interne Rebuild-API (schneller)
POST /index/rebuild
{
  "table": "articles",
  "column": "content",
  "source_index": "content_v1",
  "target_index": "content_v2"
}

# 3. Health Check auf neuem Index
POST /search/fulltext
{
  "table": "articles",
  "column": "content",
  "index": "content_v2",
  "query": "test query"
}

# 4. Traffic auf neuen Index umleiten (Application-Level)
# Update App-Config oder Feature-Flag:
FULLTEXT_INDEX_VERSION=v2

# 5. Alten Index beobachten (1-7 Tage)
# Metriken: Query Count, Error Rate

# 6. Alten Index löschen
DELETE /index/drop
{
  "table": "articles",
  "column": "content",
  "name": "content_v1"
}
```

**Timeline:**
- Vorbereitung: 1-2 Stunden
- Index Rebuild: 10min - 2 Stunden (abhängig von Datenmenge)
- Monitoring: 1-7 Tage
- Cleanup: 30 Minuten

---

### Strategy 2: Maintenance Window (In-Place)

**Vorteile:**
- ✅ Einfacher Workflow
- ✅ Kein Dual-Storage nötig

**Nachteile:**
- ❌ Service-Downtime
- ❌ Kein Rollback ohne Backup

**Workflow:**

```bash
# 1. Announcement: Maintenance Window
# Notify users: Search unavailable 02:00-04:00 UTC

# 2. Stop write traffic (optional)
POST /config {"read_only_mode": true}

# 3. Alten Index löschen
DELETE /index/drop
{
  "table": "articles",
  "column": "content"
}

# 4. Neuen Index mit neuer Config erstellen
POST /index/create
{
  "table": "articles",
  "column": "content",
  "type": "fulltext",
  "config": {
    "stemming_enabled": true,
    "language": "de"
  }
}

# 5. Index befüllen (automatisch bei Entity-Operations)
# oder via Bulk-Rebuild:
POST /index/rebuild {"table": "articles", "column": "content"}

# 6. Smoke Test
POST /search/fulltext {"query": "test", "limit": 10}

# 7. Resume write traffic
POST /config {"read_only_mode": false}
```

**Timeline:**
- Downtime: 10 Minuten - 2 Stunden

---

### Strategy 3: Incremental (Rolling Update)

**Für sehr große Datasets (>10M Dokumente)**

**Workflow:**

```bash
# 1. Neuen Index erstellen (wie Strategy 1)

# 2. Inkrementelles Befüllen mit Batches
for offset in $(seq 0 10000 10000000); do
  # Fetch batch
  GET /entities/articles?offset=$offset&limit=10000
  
  # Index batch in v2
  POST /index/add_batch {"index": "content_v2", "documents": [...]}
  
  # Sleep um Load zu reduzieren
  sleep 5
done

# 3. Delta Sync (neue Dokumente seit Start)
GET /entities/articles?created_after=$MIGRATION_START_TIME
POST /index/add_batch {"index": "content_v2", "documents": [...]}

# 4. Cutover (wie Strategy 1)
```

**Timeline:**
- Migration: 1-7 Tage (je nach Dataset-Größe)

---

## Rollback Procedures

### Rollback bei Dual-Index (Strategy 1)

```bash
# 1. Traffic auf alten Index zurück umleiten
FULLTEXT_INDEX_VERSION=v1

# 2. Neuen Index löschen (optional)
DELETE /index/drop {"name": "content_v2"}
```

**Rollback-Zeit:** < 5 Minuten

### Rollback bei In-Place (Strategy 2)

**Vorbedingung:** Backup des alten Index

```bash
# 1. Index aus Backup wiederherstellen
POST /index/restore
{
  "table": "articles",
  "column": "content",
  "backup_id": "2025-11-07_01-00-00"
}

# 2. Smoke Test
POST /search/fulltext {"query": "test"}
```

**Rollback-Zeit:** 30 Minuten - 2 Stunden

---

## Backward Compatibility

### API Compatibility Matrix

| Version | FULLTEXT() Syntax | BM25() Function | Stemming | Phrase Search |
|---------|-------------------|-----------------|----------|---------------|
| v1.0 | ✅ | ❌ | ❌ | ❌ |
| v1.1 | ✅ | ✅ | ✅ | ❌ |
| v1.2 | ✅ | ✅ | ✅ | ✅ (substring) |
| v1.3 | ✅ | ✅ | ✅ | ✅ (substring) |

**Breaking Changes:** Keine

**Deprecated Features:** Keine

---

## Testing Checklist

### Pre-Migration

- [ ] Backup des aktuellen Index erstellt
- [ ] Neue Index-Konfiguration validiert (JSON schema)
- [ ] Test-Queries vorbereitet (Representative sample)
- [ ] Rollback-Prozedur dokumentiert
- [ ] Stakeholder informiert (bei Zero-downtime nicht nötig)

### During Migration

- [ ] Index-Build-Progress monitored
- [ ] Memory/CPU-Usage überwacht
- [ ] Error Logs geprüft
- [ ] Sample Queries auf neuem Index getestet

### Post-Migration

- [ ] Relevanz-Vergleich: Alt vs. Neu (Top-10 Results)
- [ ] Performance-Metriken: Latenz p50/p99
- [ ] Error-Rate überwacht (24h)
- [ ] User Feedback gesammelt (falls User-facing)

---

## Migration Examples

### Example 1: Stemming aktivieren (EN)

**Aktuell:** Kein Stemming  
**Ziel:** EN Stemming aktiviert

```bash
# Dual-Index Migration
POST /index/create
{
  "table": "articles",
  "column": "content",
  "name": "content_stemmed",
  "type": "fulltext",
  "config": {
    "stemming_enabled": true,
    "language": "en"
  }
}

# Rebuild
POST /index/rebuild
{
  "table": "articles",
  "column": "content",
  "target_index": "content_stemmed"
}

# Relevanz-Test
# Query: "running"
# Alt: Nur exakte "running" Matches
# Neu: "running", "run", "runs", "ran" Matches

# Cutover nach Validierung
FULLTEXT_INDEX_NAME=content_stemmed
```

### Example 2: DE Umlaut-Normalisierung

**Aktuell:** Keine Normalisierung  
**Ziel:** ä→a, ö→o, ü→u, ß→ss

```bash
POST /index/create
{
  "table": "documents",
  "column": "text",
  "name": "text_normalized",
  "type": "fulltext",
  "config": {
    "language": "de",
    "normalize_umlauts": true
  }
}

# Test-Query: "lauft"
# Alt: Nur exakte "lauft" Matches
# Neu: "lauft" + "läuft" Matches
```

### Example 3: Vector Dimension Change

**Aktuell:** 768-dim (BERT)  
**Ziel:** 1536-dim (OpenAI ada-002)

```bash
# 1. Neuer Index
POST /vector/create
{
  "table": "embeddings",
  "column": "vector_1536",
  "dimension": 1536,
  "metric": "cosine"
}

# 2. Re-Embedding Pipeline
# - Fetch all documents
# - Generate new embeddings (1536-dim)
# - Insert into new column

# 3. Cutover
VECTOR_COLUMN=vector_1536
```

---

## Performance Impact

### During Migration

| Metric | Impact | Mitigation |
|--------|--------|------------|
| Write Latency | +20-50% | Batch writes, rate limiting |
| Read Latency | Minimal (dual-index) | Route reads to v1 during migration |
| Storage | +100% (temporary) | Monitor disk space |
| CPU | +30-60% | Schedule off-peak hours |
| Memory | +20-40% | Ensure sufficient RAM |

### After Migration

**Stemming Impact:**
- Index Size: +10-20% (mehr Tokens)
- Query Latency: +5-10% (mehr Kandidaten)
- Recall: +15-30% (bessere Matches)

**Umlaut Normalization:**
- Index Size: Minimal (+2-5%)
- Query Latency: Minimal
- Recall: +5-10% (DE Queries)

---

## FAQ

**Q: Kann ich Stemming nachträglich aktivieren ohne Rebuild?**  
A: Nein. Stemming ist eine Index-time Operation. Rebuild erforderlich.

**Q: Was passiert mit bestehenden Queries während Migration?**  
A: Bei Dual-Index: Keine Auswirkung. Bei In-Place: Downtime während Rebuild.

**Q: Wie lange dauert ein Rebuild?**  
A: Abhängig von Datenmenge. Faustregeln:
- 10k docs: ~10 Sekunden
- 100k docs: ~2 Minuten
- 1M docs: ~20 Minuten
- 10M docs: ~3 Stunden

**Q: Muss ich alte Daten neu embedden bei Vector-Dim-Change?**  
A: Ja. Vektoren müssen mit neuem Modell neu generiert werden.

**Q: Kann ich v1 und v2 parallel A/B-testen?**  
A: Ja, mit Dual-Index Strategy. Route 50% Traffic auf v1, 50% auf v2.

**Q: Was ist die empfohlene Migration-Strategy?**  
A: Für Production: **Dual-Index (Strategy 1)** - Zero Downtime & Rollback-fähig.

---

## Monitoring & Alerts

### Key Metrics während Migration

```yaml
migration_metrics:
  - index_build_progress_percent
  - index_build_duration_seconds
  - index_size_bytes_old
  - index_size_bytes_new
  - migration_errors_total
  - dual_index_write_latency_ms
```

### Alerts

```yaml
alerts:
  - name: MigrationStalled
    condition: index_build_progress_percent unchanged for 30min
    action: Check logs, restart rebuild if needed
    
  - name: MigrationErrors
    condition: migration_errors_total > 100
    action: Pause migration, investigate root cause
    
  - name: DiskSpaceLow
    condition: disk_usage_percent > 85
    action: Free space or abort migration
```

---

## Best Practices

1. **Test in Staging first**: Validate migration process with production-like data
2. **Monitor closely**: Set up dashboards for migration-specific metrics
3. **Communicate early**: Notify stakeholders 48h before migration
4. **Have a rollback plan**: Always maintain ability to revert
5. **Validate relevance**: Compare Top-10 results before/after
6. **Schedule off-peak**: Minimize impact on users
7. **Document everything**: Record decisions, issues, and resolutions

---

## Support & Troubleshooting

**Common Issues:**

| Problem | Cause | Solution |
|---------|-------|----------|
| Rebuild fails with OOM | Insufficient RAM | Increase memory or use incremental migration |
| Relevance degraded | Wrong stemming language | Check language config, rebuild if needed |
| Queries slower after migration | Higher candidate count | Adjust limit parameter, check indexes |
| Disk space full | Dual index taking 2x space | Clean up old data, expand storage |

**Support Channels:**
- Internal Docs: `docs/search/`
- Monitoring: `/metrics` endpoint
- Logs: `themis_server.log`

---

## References

- Performance Tuning Guide: `docs/search/performance_tuning.md`
- Fulltext API: `docs/search/fulltext_api.md`
- AQL Syntax: `docs/aql_syntax.md`
