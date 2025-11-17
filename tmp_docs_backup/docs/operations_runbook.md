# Operations Runbook

Dieses Runbook fasst die wichtigsten Betriebsaufgaben und Notfallmaßnahmen für THEMIS zusammen. Es richtet sich an On-Call/Operations und beschreibt Checks, Standardprozeduren und Playbooks.

## 1) Quick Checks (60 Sekunden)

- Health: GET /health
- Version: GET /version (falls aktiviert) oder Server-Logs
- Metrics: GET /metrics (Prometheus Textformat)
- Stats: GET /stats (System-/Storage-Kurzüberblick, falls aktiviert)
- Config (laufend): GET /config

Beispiele (Windows PowerShell):

```powershell
Invoke-RestMethod http://localhost:8765/health
Invoke-RestMethod http://localhost:8765/metrics | Out-String | Select-Object -First 50
Invoke-RestMethod http://localhost:8765/config | ConvertTo-Json -Depth 5
```

Linux/macOS:

```bash
curl -fsS http://localhost:8765/health
curl -fsS http://localhost:8765/metrics | head -50
curl -fsS http://localhost:8765/config | jq .
```

## 2) Start/Stop/Restart

### Systemd (Linux)

```bash
sudo systemctl status vccdb
sudo systemctl restart vccdb
sudo journalctl -u vccdb -f
```

### Docker / Docker Compose

```bash
docker ps | grep vccdb
docker logs -f vccdb
# Restart
docker restart vccdb

# Compose Stack
docker-compose ps
docker-compose logs -f vccdb
docker-compose restart vccdb
```

### Windows (PowerShell, Standalone Binary)

```powershell
# Prozess prüfen
Get-Process themis_server -ErrorAction SilentlyContinue
# Starten (aus Build/Release-Verzeichnis)
Start-Process -FilePath ".\build\Release\themis_server.exe" -NoNewWindow
# Stoppen
Get-Process themis_server -ErrorAction SilentlyContinue | Stop-Process -Force
```

## 3) Monitoring & Alert Response

Wichtige Metriken (siehe auch docs/deployment.md):

- Server: vccdb_requests_total, vccdb_errors_total, vccdb_qps, process_uptime_seconds
- Latenz: vccdb_latency_bucket_microseconds{le="..."}, vccdb_latency_sum_microseconds, vccdb_latency_count
- RocksDB: rocksdb_block_cache_usage_bytes, rocksdb_block_cache_capacity_bytes, rocksdb_pending_compaction_bytes, rocksdb_estimate_num_keys
- Index: themis_index_rebuild_count, themis_index_rebuild_duration_ms_total, themis_index_rebuild_entities_processed_total, themis_index_cursor_anchor_hits_total, themis_index_range_scan_steps_total

Typische Alarme und Sofortmaßnahmen:

- Hohe Fehlerquote (errors_total):
  - Logs auf Exceptions prüfen (Server-Logs, /metrics für spikes)
  - Falls Konfig-Änderung ursächlich: /config zurückdrehen oder Neustart mit bekannter funktionierender config.json
- Hohe p95/p99 Latenz:
  - Compaction-Backlog prüfen (rocksdb_pending_compaction_bytes)
  - Block Cache Nutzung vs. Kapazität prüfen; ggf. Cache vergrößern (Neustart erforderlich)
  - Index-Hitrate/Range-Schritte prüfen (range_scan_steps_total); Query-Filter/Indexierung optimieren
- Compaction-Backlog hoch:
  - Wartungsfenster: `flush`/`compactRange` per Admin-Tool, IO-Budget freihalten, kurzzeitig Ingestion drosseln
- /metrics nicht erreichbar:
  - Health prüfen; Reverse Proxy/Firewall prüfen; ggf. direkt auf die Instanz zugreifen

## 4) Konfiguration im Betrieb (Hot-Reload)

Folgende Werte sind zur Laufzeit änderbar (siehe docs/deployment.md → Runtime Configuration):

- logging.level (trace/debug/info/warn/error)
- logging.format (text/json)
- request_timeout_ms (1000–300000)
- features (z. B. cdc, semantic_cache)

Beispiel (Timeout auf 60s):

```bash
curl -X POST http://localhost:8765/config \
  -H "Content-Type: application/json" \
  -d '{"request_timeout_ms": 60000}'
```

Tracing an/aus (siehe docs/tracing.md):

```json
{
  "tracing": { "enabled": true, "service_name": "themis", "otlp_endpoint": "http://localhost:4318" }
}
```

## 5) Backup & Restore

Siehe Details in docs/deployment.md → Backup & Recovery.

### Snapshot (Checkpoint)

```bash
# Snapshot erstellen
curl -X POST http://localhost:8765/admin/snapshot \
  -H "Content-Type: application/json" \
  -d '{"path":"/backups/themis-snap-$(date +%Y%m%d-%H%M%S)"}'

# Restore (Server stoppen, Daten zurückspielen, verifizieren)
./themis_server --restore /backups/themis-snap-YYYYMMDD-HHMMSS --target /var/lib/vccdb/data
```

### WAL-Archivierung

- WAL-Archiv aktivieren (config.json), regelmäßige Rotation/Retention einplanen

## 6) Index- und Datenpflege (Wartung)

Dokumentation und API-Beispiele: `docs/indexes.md`, `docs/index_stats_maintenance.md`.

- Rebuild einzelner Indizes (bei Inkonsistenzen, Schema-Änderungen)
- Reindex ganzer Tabellen (geplant für Wartungsfenster)
- TTL-Cleanup regelmäßig ausführen (Cron/Timer in Admin-Service)

Hinweis: Die Rebuild-/Reindex-APIs existieren in der Serverbibliothek (C++). In Produktion idealerweise über ein Admin-Tool oder Maintenance-Job nutzen. Beachte die Metriken `themis_index_rebuild_*` für Dauer/Progress.

## 7) CDC Betrieb (Change Data Capture)

Siehe `docs/change_data_capture.md`.

- Aktivieren über Feature Flag in config.json
- Pull-Pattern: `GET /changefeed?from_seq=<checkpoint>&limit=<N>`
- Long-Poll: `long_poll_ms` zur Latenzreduktion
- Retention/Cleanup: Admin-Endpunkt (vorherige Sequenzen entfernen) gemäß Doku
- Reverse Proxy für SSE-Stream `/changefeed/stream` korrekt konfigurieren (Keep-Alive, keine Pufferung)

## 8) Skalierung & Performance

- Threads: `server.worker_threads` (Neustart erforderlich)
- VectorIndex (ANN): `efSearch` zur Laufzeit erhöhen für Genauigkeit, trade-off Latenz
- RocksDB:
  - `block_cache_size_mb`, `memtable_size_mb` (Neustart erforderlich)
  - Direct IO/Compaction-Optionen nur nach Tests ändern
- Compose/K8s: Ressourcenlimits und Replikate anpassen; Load Balancer/Sticky Sessions bei SSE beachten

## 9) Troubleshooting Playbooks (Kurzfassung)

- API 5xx Spike
  - Health OK? /health
  - Logs auf Exceptions prüfen
  - Letzte Config-Änderungen rückgängig machen (POST /config) oder Neustart
  - Bei Indexfehlern: betroffenen Index im Wartungsfenster rebuilden

- Abfragen sehr langsam
  - p95/p99 prüfen, Range-Schritte hoch? → Index-Selektion/Prädikate prüfen
  - Compaction-Backlog hoch? → IO-Feuerwehr (Flush/Compaction), Ingestion drosseln
  - Vector-Suche: `efSearch` erhöhen, ggf. ANN-Persistenz laden (`saveIndex`/`loadIndex`)

- CDC Events fehlen
  - Feature aktiviert? Config prüfen
  - /changefeed liefert latest_sequence, checkpoint-Logik auf Consumer-Seite prüfen
  - Reverse Proxy-Einstellungen für SSE prüfen

- Graph-Anomalien (Kanten fehlen)
  - Topologie neu aufbauen (`GraphIndexManager::rebuildTopology()` im Maintenance-Tool)

## 10) SLO/SLI (Empfehlung)

- Verfügbarkeit (monthly): ≥ 99.9%
- p95 Latenz (Query): ≤ 200 ms (je Index/Workload variabel)
- Fehlerquote: < 0.1% Requests
- Compaction Backlog: < 5 GB über 15 Minuten

## 11) Checklisten

### Release/Deployment Checklist

- [ ] Config validiert (`--validate`)
- [ ] Health/Metrics erreichbar
- [ ] Prometheus/Grafana up-to-date
- [ ] CDC Feature-Flags korrekt
- [ ] Backup-Policy aktiv

### Incident Checklist

- [ ] Health/Errors/Latenz geprüft
- [ ] Logs nach top Errors gefiltert
- [ ] Konfig-Drift ausgeschlossen (/config)
- [ ] Compaction/Storage-Lage geprüft
- [ ] Eskalation/Kommunikation dokumentiert

---

Weitere Details: 
- Deployment & Betrieb: `docs/deployment.md`
- Tracing: `docs/tracing.md`
- CDC: `docs/change_data_capture.md`
- Indexe & Wartung: `docs/indexes.md`, `docs/index_stats_maintenance.md`
