# HNSW Persistenz & Warmstart

Diese Seite beschreibt die Persistierung und Wiederherstellung von HNSW-Vektorindizes für schnellere Warmstarts und robustes Recovery.

## Motivation

- **Problem:** HNSW-Index wird bei jedem Neustart aus RocksDB neu aufgebaut (langsam bei >100k Vektoren)
- **Lösung:** Index auf Disk speichern (`saveIndex`) und beim Start laden (`loadIndex`)
- **Benefit:** Warmstart <10s statt mehrerer Minuten; kein Datenverlust

## Lifecycle

```
init(objectName, dim, metric, ..., savePath)
  ├─> Falls savePath/meta.txt existiert: loadIndex(savePath)
  └─> Sonst: leerer HNSW-Index

[Runtime: addEntity, searchKnn, ...]

setAutoSavePath(path, autoSave=true)
  └─> Aktiviert automatisches Speichern bei shutdown()

shutdown()
  └─> Falls autoSave: saveIndex(savePath)
```

## API

### 1) Init mit Save-Path

```cpp
VectorIndexManager vix(db);
vix.init("chunks", 768, VectorIndexManager::Metric::COSINE, 
         /*M*/16, /*efC*/200, /*ef*/64, 
         /*savePath=*/"./data/hnsw_chunks");
// Falls ./data/hnsw_chunks/meta.txt vorhanden → lädt Index automatisch
```

### 2) Automatisches Speichern aktivieren

```cpp
vix.setAutoSavePath("./data/hnsw_chunks", /*autoSave*/true);
// Bei shutdown() wird Index gespeichert
```

### 3) Manuelles Speichern

```cpp
auto status = vix.saveIndex("./data/hnsw_chunks");
if (!status.ok) {
    THEMIS_ERROR("Failed to save index: {}", status.message);
}
```

### 4) Manuelles Laden

```cpp
auto status = vix.loadIndex("./data/hnsw_chunks");
if (!status.ok) {
    THEMIS_WARN("Failed to load index, rebuilding from storage");
    vix.rebuildFromStorage();
}
```

## Persistenz-Format

Verzeichnisstruktur:

```
data/hnsw_chunks/
  ├─ index.bin      # HNSW Graph-Struktur (hnswlib-Format)
  ├─ meta.txt       # Metadaten (Dimension, Metric, M, efC)
  └─ mapping.txt    # PK-Mapping (Zeile i = PK für Label i)
```

### meta.txt

```
dim=768
metric=1
M=16
efConstruction=200
count=50000
```

### mapping.txt

```
chunk_doc123_0
chunk_doc123_1
chunk_doc456_0
...
```

## Workflow: Startup mit Warmstart

```cpp
// main_server.cpp
auto vix = std::make_shared<VectorIndexManager>(db);

// Setze Pfad vor init (optional)
vix->setAutoSavePath("./data/hnsw_index");

// Init lädt automatisch, falls vorhanden
vix->init("chunks", 768, VectorIndexManager::Metric::COSINE, 16, 200, 64);

// Nach Init prüfen
if (vix->getVectorCount() > 0) {
    THEMIS_INFO("Warmstart: {} vectors loaded", vix->getVectorCount());
} else {
    THEMIS_INFO("Cold start: rebuilding index from storage");
    vix->rebuildFromStorage();
}

// Laufzeit: addEntity/searchKnn

// Bei Shutdown
vix->shutdown(); // Speichert automatisch, falls autoSave aktiviert
```

## Performance

### Cold Start (rebuild from RocksDB)

| Vektoren | Dim | Rebuild-Zeit | Speichernutzung |
|----------|-----|--------------|-----------------|
| 10k | 768 | ~2s | ~50 MB |
| 100k | 768 | ~30s | ~500 MB |
| 1M | 768 | ~8min | ~5 GB |

### Warmstart (loadIndex)

| Vektoren | Dim | Load-Zeit | Disk-Größe |
|----------|-----|-----------|------------|
| 10k | 768 | <1s | ~20 MB |
| 100k | 768 | ~3s | ~200 MB |
| 1M | 768 | ~25s | ~2 GB |

**Empfehlung:** Ab >50k Vektoren immer Persistenz nutzen.

## Konfiguration

In `config/vector_index.json` (optional):

```json
{
  "save_path": "./data/hnsw_index",
  "auto_save": true,
  "save_interval_sec": 300
}
```

Falls `save_interval_sec` gesetzt: periodisches Auto-Save im Hintergrund (geplant).

## Fehlerbehandlung

### Korrupte Index-Datei

Falls `loadIndex` fehlschlägt:

```cpp
auto status = vix->loadIndex(path);
if (!status.ok) {
    THEMIS_WARN("Corrupted index, rebuilding: {}", status.message);
    std::filesystem::remove_all(path); // Optional: alte Dateien löschen
    vix->rebuildFromStorage();
    vix->saveIndex(path); // Neu speichern
}
```

### Disk-Space-Fehler

Bei `saveIndex`:

```cpp
auto status = vix->saveIndex(path);
if (!status.ok && status.message.find("No space") != std::string::npos) {
    THEMIS_ERROR("Disk full, cannot save index");
    // Alarm auslösen, alte Backups löschen, etc.
}
```

## Backup & Recovery

### Backup

```bash
# Index-Verzeichnis sichern
tar -czf hnsw_index_backup_$(date +%Y%m%d).tar.gz ./data/hnsw_index/

# Optional: mit RocksDB-Checkpoint kombinieren
```

### Recovery

```bash
# Index wiederherstellen
tar -xzf hnsw_index_backup_20251102.tar.gz -C ./data/

# Server starten → lädt Index automatisch
./themis_server
```

## Rebuild-Strategien

### 1) Vollständiger Rebuild

```cpp
vix->rebuildFromStorage(); // Scannt alle Vektoren aus RocksDB
vix->saveIndex(path);
```

### 2) Inkrementelles Update

Nach Bulk-Import:

```cpp
// ... Batch-Insert von 10k Vektoren via WriteBatch

// Index ist in-memory aktualisiert; speichere
vix->saveIndex(path);
```

### 3) Scheduled Rebuild

```cpp
// Cronjob oder Timer
std::thread([&vix, path]() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::hours(6));
        vix->rebuildFromStorage();
        vix->saveIndex(path);
    }
}).detach();
```

## Tests

Unit-Tests: `tests/test_vector_index_persistence.cpp` (geplant)

```cpp
TEST(VectorPersistence, SaveLoadCycle) {
    // 1) Init + Insert 1000 Vektoren
    // 2) saveIndex
    // 3) Neuer VectorIndexManager
    // 4) loadIndex
    // 5) Query → prüfe gleiche Top-k-IDs
}
```

## Runbook

### Problem: Index nicht geladen

**Symptom:** `getVectorCount() == 0` nach Init, obwohl RocksDB Daten enthält.

**Lösung:**

1. Prüfe `savePath`-Existenz: `ls -lh ./data/hnsw_index/`
2. Logs prüfen: `grep "VectorIndexManager::init" server.log`
3. Falls `meta.txt` fehlt/korrupt → manueller Rebuild:

```cpp
vix->rebuildFromStorage();
vix->saveIndex("./data/hnsw_index");
```

### Problem: Langsame Queries nach Load

**Symptom:** KNN-Suche dauert >500ms (sollte <50ms sein).

**Mögliche Ursachen:**

- efSearch zu niedrig → erhöhen:

```cpp
vix->setEfSearch(128); // statt 64
```

- Index nicht korrekt geladen → Rebuild erzwingen

## Metriken

Prometheus `/metrics`:

- `themis_vector_index_save_duration_seconds` — Histogram für saveIndex-Dauer
- `themis_vector_index_load_duration_seconds` — Histogram für loadIndex-Dauer
- `themis_vector_index_size_bytes` — Disk-Größe des Index
- `themis_vector_index_rebuild_total` — Counter für Rebuilds

(Implementierung geplant)

## Referenzen

- [Vector Operations](vector_ops.md)
- [Indexes](indexes.md#vector-index)
- [Performance & Benchmarks](performance_benchmarks.md#vector-suche-hnsw-tuning)
- [Operations Runbook](operations_runbook.md#vector-index-maintenance)
