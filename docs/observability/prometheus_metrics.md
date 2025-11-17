# Prometheus Metrics Reference

**Status:** ✅ Vollständig implementiert (29.10.2025)  
**Endpoint:** `GET /metrics`  
**Format:** Prometheus Text Format

---

## Übersicht

ThemisDB exportiert Metriken im Prometheus-Text-Format über den `/metrics`-Endpoint. Alle Histogramme verwenden **kumulative Buckets** gemäß Prometheus-Spezifikation.

**Metrik-Kategorien:**
- Server-Metriken (Requests, Errors, QPS, Uptime)
- Latenz-Histogramme (HTTP, Query)
- RocksDB-Metriken (Cache, Keys, Compaction)
- Index-Metriken (Rebuild, Cursor, Range Scans)
- Vector-Index-Metriken (Größe, Suchlatenz, Batch-Operationen)

---

## Server-Metriken

### vccdb_requests_total (Counter)
**Beschreibung:** Gesamtzahl der HTTP-Requests  
**Labels:** 
- `method` - HTTP-Methode (GET, POST, PUT, DELETE)
- `route` - Request-Route (z.B. `/entities/{key}`, `/query`, `/vector/search`)

**Beispiel:**
```
vccdb_requests_total{method="GET",route="/entities/{key}"} 1234
vccdb_requests_total{method="POST",route="/query"} 567
```

**PromQL-Beispiele:**
```promql
# Requests pro Sekunde (letzte 5 Minuten)
rate(vccdb_requests_total[5m])

# Requests nach Route
sum by (route) (rate(vccdb_requests_total[5m]))
```

---

### vccdb_errors_total (Counter)
**Beschreibung:** Gesamtzahl der Fehler (HTTP 4xx/5xx)  
**Labels:**
- `status_code` - HTTP-Status-Code (400, 404, 500, etc.)
- `route` - Request-Route

**Beispiel:**
```
vccdb_errors_total{status_code="404",route="/entities/{key}"} 12
vccdb_errors_total{status_code="500",route="/query"} 3
```

**PromQL-Beispiele:**
```promql
# Fehlerrate (letzte 5 Minuten)
rate(vccdb_errors_total[5m])

# Error Rate Ratio
rate(vccdb_errors_total[5m]) / rate(vccdb_requests_total[5m])
```

---

### vccdb_qps (Gauge)
**Beschreibung:** Queries per Second (aktuelle Rate)  
**Berechnung:** Exponentieller Mittelwert über rollende Zeitfenster

**Beispiel:**
```
vccdb_qps 123.45
```

**PromQL-Beispiele:**
```promql
# QPS-Durchschnitt (letzte Stunde)
avg_over_time(vccdb_qps[1h])
```

---

### process_uptime_seconds (Gauge)
**Beschreibung:** Server-Laufzeit in Sekunden seit Start

**Beispiel:**
```
process_uptime_seconds 86400
```

**PromQL-Beispiele:**
```promql
# Uptime in Tagen
process_uptime_seconds / 86400
```

---

## Latenz-Histogramme

### Bucket-Definitionen

**Latenz-Buckets (Mikrosekunden):**
- 100µs, 500µs, 1ms, 5ms, 10ms, 50ms, 100ms, 500ms, 1s, 5s, +Inf

**Page-Fetch-Buckets (Millisekunden):**
- 1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s, +Inf

**Wichtig:** Alle Buckets sind **kumulativ** (le="X" = alle Werte ≤ X)

---

### vccdb_latency_bucket_microseconds (Histogram)
**Beschreibung:** HTTP-Request-Latenz (Mikrosekunden)  
**Labels:**
- `le` - Less-or-equal Bucket-Grenze (100, 500, 1000, ...)

**Beispiel:**
```
vccdb_latency_bucket_microseconds{le="100"} 45
vccdb_latency_bucket_microseconds{le="500"} 123
vccdb_latency_bucket_microseconds{le="1000"} 234
vccdb_latency_bucket_microseconds{le="5000"} 450
vccdb_latency_bucket_microseconds{le="+Inf"} 500
vccdb_latency_sum_microseconds 1234567
vccdb_latency_count 500
```

**PromQL-Beispiele:**
```promql
# P95 Latenz (Mikrosekunden)
histogram_quantile(0.95, rate(vccdb_latency_bucket_microseconds[5m]))

# P99 Latenz (Mikrosekunden)
histogram_quantile(0.99, rate(vccdb_latency_bucket_microseconds[5m]))

# Durchschnittliche Latenz
rate(vccdb_latency_sum_microseconds[5m]) / rate(vccdb_latency_count[5m])

# Requests unter 1ms
sum(rate(vccdb_latency_bucket_microseconds{le="1000"}[5m]))
```

---

### vccdb_page_fetch_time_ms_bucket (Histogram)
**Beschreibung:** Latenz für Cursor-Pagination-Fetches (Millisekunden)  
**Labels:**
- `le` - Less-or-equal Bucket-Grenze (1, 5, 10, 25, ...)

**Beispiel:**
```
vccdb_page_fetch_time_ms_bucket{le="1"} 89
vccdb_page_fetch_time_ms_bucket{le="5"} 156
vccdb_page_fetch_time_ms_bucket{le="10"} 234
vccdb_page_fetch_time_ms_bucket{le="+Inf"} 250
vccdb_page_fetch_time_ms_sum 2345.67
vccdb_page_fetch_time_ms_count 250
```

**PromQL-Beispiele:**
```promql
# P95 Page-Fetch-Latenz
histogram_quantile(0.95, rate(vccdb_page_fetch_time_ms_bucket[5m]))
```

---

## RocksDB-Metriken

### rocksdb_block_cache_usage_bytes (Gauge)
**Beschreibung:** Aktuell verwendeter Block-Cache (Bytes)

**Beispiel:**
```
rocksdb_block_cache_usage_bytes 1073741824
```

---

### rocksdb_block_cache_capacity_bytes (Gauge)
**Beschreibung:** Block-Cache-Kapazität (Bytes, konfiguriert)

**Beispiel:**
```
rocksdb_block_cache_capacity_bytes 2147483648
```

**PromQL-Beispiele:**
```promql
# Cache-Auslastung in %
100 * rocksdb_block_cache_usage_bytes / rocksdb_block_cache_capacity_bytes
```

---

### rocksdb_estimate_num_keys (Gauge)
**Beschreibung:** Geschätzte Anzahl Keys in der Datenbank  
**Hinweis:** Schätzwert, nicht exakt

**Beispiel:**
```
rocksdb_estimate_num_keys 1234567
```

---

### rocksdb_pending_compaction_bytes (Gauge)
**Beschreibung:** Bytes, die auf Compaction warten  
**Wichtig:** Hohe Werte (>10 GB) können Latenz erhöhen

**Beispiel:**
```
rocksdb_pending_compaction_bytes 1234567890
```

**PromQL-Beispiele:**
```promql
# Alert: Compaction-Backlog > 10 GB
rocksdb_pending_compaction_bytes > 10000000000
```

---

### rocksdb_memtable_size_bytes (Gauge)
**Beschreibung:** Aktuelle Memtable-Größe (Bytes)

**Beispiel:**
```
rocksdb_memtable_size_bytes 67108864
```

---

### rocksdb_files_per_level (Gauge)
**Beschreibung:** Anzahl SST-Dateien pro LSM-Level  
**Labels:**
- `level` - LSM-Tree-Level (0, 1, 2, ...)

**Beispiel:**
```
rocksdb_files_per_level{level="0"} 3
rocksdb_files_per_level{level="1"} 12
rocksdb_files_per_level{level="2"} 45
```

**PromQL-Beispiele:**
```promql
# Gesamtzahl SST-Dateien
sum(rocksdb_files_per_level)
```

---

## Index-Metriken

### vccdb_index_rebuild_total (Counter)
**Beschreibung:** Gesamtzahl Index-Rebuild-Operationen  
**Labels:**
- `table` - Tabellenname
- `column` - Spaltenname

**Beispiel:**
```
vccdb_index_rebuild_total{table="users",column="email"} 3
```

---

### vccdb_index_rebuild_duration_seconds (Counter)
**Beschreibung:** Gesamt-Zeit für Index-Rebuilds (Sekunden)  
**Labels:**
- `table` - Tabellenname
- `column` - Spaltenname

**Beispiel:**
```
vccdb_index_rebuild_duration_seconds{table="users",column="email"} 123.45
```

**PromQL-Beispiele:**
```promql
# Durchschnittliche Rebuild-Dauer
vccdb_index_rebuild_duration_seconds / vccdb_index_rebuild_total
```

---

### vccdb_index_rebuild_entities_processed (Counter)
**Beschreibung:** Anzahl verarbeiteter Entities bei Index-Rebuilds  
**Labels:**
- `table` - Tabellenname
- `column` - Spaltenname

**Beispiel:**
```
vccdb_index_rebuild_entities_processed{table="users",column="email"} 1000000
```

---

### themis_index_cursor_anchor_hits_total (Counter)
**Beschreibung:** Anzahl erfolgreicher Cursor-Anchor-Lookups (Pagination)

**Beispiel:**
```
themis_index_cursor_anchor_hits_total 567
```

---

### themis_index_range_scan_steps_total (Counter)
**Beschreibung:** Gesamtzahl Range-Scan-Schritte (Index-Traversierungen)

**Beispiel:**
```
themis_index_range_scan_steps_total 12345
```

**PromQL-Beispiele:**
```promql
# Range-Scan-Schritte pro Sekunde
rate(themis_index_range_scan_steps_total[5m])
```

---

## Vector-Index-Metriken

### vccdb_vector_index_size_bytes (Gauge)
**Beschreibung:** Geschätzte Größe des In-Memory-Vector-Index (Bytes)

**Beispiel:**
```
vccdb_vector_index_size_bytes 536870912
```

---

### vccdb_vector_search_duration_ms (Histogram)
**Beschreibung:** Vector-Search-Latenz (Millisekunden)

**Beispiel:**
```
vccdb_vector_search_duration_ms_bucket{le="1"} 45
vccdb_vector_search_duration_ms_bucket{le="5"} 123
vccdb_vector_search_duration_ms_bucket{le="10"} 234
vccdb_vector_search_duration_ms_bucket{le="+Inf"} 250
```

---

### vccdb_vector_batch_insert_duration_ms (Histogram)
**Beschreibung:** Batch-Insert-Latenz (Millisekunden)

---

### vccdb_vector_batch_insert_total (Counter)
**Beschreibung:** Gesamtzahl Batch-Insert-Operationen

---

### vccdb_vector_batch_insert_items_total (Counter)
**Beschreibung:** Gesamtzahl eingefügter Vector-Items

**PromQL-Beispiele:**
```promql
# Durchschnittliche Batch-Größe
rate(vccdb_vector_batch_insert_items_total[5m]) / rate(vccdb_vector_batch_insert_total[5m])
```

---

### vccdb_vector_delete_by_filter_total (Counter)
**Beschreibung:** Gesamtzahl Delete-by-Filter-Operationen

---

### vccdb_vector_delete_by_filter_items_total (Counter)
**Beschreibung:** Gesamtzahl gelöschter Vector-Items

---

## Grafana-Dashboard-Beispiele

### Dashboard 1: Server-Übersicht

**Panels:**
```promql
# QPS
vccdb_qps

# Error Rate
rate(vccdb_errors_total[5m]) / rate(vccdb_requests_total[5m])

# P95 Latenz
histogram_quantile(0.95, rate(vccdb_latency_bucket_microseconds[5m]))

# Uptime
process_uptime_seconds / 86400
```

---

### Dashboard 2: RocksDB Health

**Panels:**
```promql
# Cache Hit Rate (approximation)
100 * rocksdb_block_cache_usage_bytes / rocksdb_block_cache_capacity_bytes

# Compaction Backlog
rocksdb_pending_compaction_bytes

# Keys Estimate
rocksdb_estimate_num_keys

# SST Files per Level
sum by (level) (rocksdb_files_per_level)
```

---

### Dashboard 3: Vector Operations

**Panels:**
```promql
# Vector Index Size
vccdb_vector_index_size_bytes

# Search Latency P95
histogram_quantile(0.95, rate(vccdb_vector_search_duration_ms_bucket[5m]))

# Batch Insert Rate
rate(vccdb_vector_batch_insert_items_total[5m])
```

---

## Alerts (Beispiele)

### High Error Rate
```yaml
- alert: HighErrorRate
  expr: rate(vccdb_errors_total[5m]) / rate(vccdb_requests_total[5m]) > 0.05
  for: 5m
  annotations:
    summary: "Error rate above 5%"
```

### High Latency P99
```yaml
- alert: HighLatencyP99
  expr: histogram_quantile(0.99, rate(vccdb_latency_bucket_microseconds[5m])) > 100000
  for: 5m
  annotations:
    summary: "P99 latency above 100ms"
```

### Compaction Backlog
```yaml
- alert: CompactionBacklog
  expr: rocksdb_pending_compaction_bytes > 10000000000
  for: 10m
  annotations:
    summary: "Compaction backlog > 10 GB"
```

### Low Cache Hit Rate
```yaml
- alert: LowCacheHitRate
  expr: 100 * rocksdb_block_cache_usage_bytes / rocksdb_block_cache_capacity_bytes < 20
  for: 15m
  annotations:
    summary: "Block cache usage < 20%"
```

---

## Best Practices

### Scrape-Konfiguration

```yaml
scrape_configs:
  - job_name: 'themis'
    scrape_interval: 15s
    scrape_timeout: 10s
    static_configs:
      - targets: ['localhost:8765']
```

### Retention

- **Empfohlen:** 15 Tage für hochfrequente Metriken
- **Long-term:** Downsampling auf 1h-Auflösung nach 7 Tagen

### Cardinality

- Niedrig: ~100 Zeitreihen (ohne Labels)
- Hoch: Bei vielen Tables/Columns können Index-Metriken Tausende Zeitreihen erzeugen

---

## Validierung

### Metriken testen

```bash
# Alle Metriken abrufen
curl http://localhost:8765/metrics

# Spezifische Metrik filtern
curl http://localhost:8765/metrics | grep vccdb_qps

# Histogram-Buckets prüfen (müssen kumulativ sein)
curl http://localhost:8765/metrics | grep latency_bucket
```

### Kumulative Buckets validieren

```python
# Beispiel-Validator (Python)
import re

metrics = """
vccdb_latency_bucket_microseconds{le="100"} 45
vccdb_latency_bucket_microseconds{le="500"} 123
vccdb_latency_bucket_microseconds{le="1000"} 234
"""

# Buckets müssen monoton steigend sein
buckets = []
for line in metrics.strip().split('\n'):
    match = re.search(r'le="([^"]+)"\}\s+(\d+)', line)
    if match:
        bucket_value = int(match.group(2))
        buckets.append(bucket_value)

# Validierung
assert buckets == sorted(buckets), "Buckets sind nicht kumulativ!"
print("✅ Buckets sind korrekt kumulativ")
```

---

## Siehe auch

- [deployment.md](../deployment.md) - Monitoring-Konfiguration
- [operations_runbook.md](operations_runbook.md) - Alert-Response
- [Prometheus Documentation](https://prometheus.io/docs/)
- [Grafana Dashboards](https://grafana.com/docs/grafana/latest/dashboards/)

---

**Letzte Aktualisierung:** 17. November 2025  
**Status:** Produktionsreif  
**Tests:** 4/4 PASS (test_metrics_api.cpp)
