# Speicherhierarchie-Optimierung & RocksDB Tuning

Dieser Leitfaden beschreibt, wie du die Speicherhierarchie für THEMIS mit RocksDB effektiv konfigurierst.

## RocksDB Kompression

### Verfügbare Algorithmen

THEMIS unterstützt folgende Kompressionsalgorithmen (konfigurierbar via `compression_default` und `compression_bottommost`):

| Algorithmus | Kompressionsrate | CPU-Overhead | Write-Speed | Read-Speed | Empfehlung |
|-------------|-----------------|--------------|-------------|------------|------------|
| **None** | 1.0x (keine) | Minimal | ⚡ Sehr schnell | ⚡ Sehr schnell | Nur für sehr schnelle SSDs mit viel Speicher |
| **LZ4** | 2-3x | Niedrig | ⚡ Schnell | ⚡ Schnell | ✅ **Empfohlen für Level 0-5** |
| **ZSTD** | 3-5x | Mittel | Mittel | Schnell | ✅ **Empfohlen für Level 6+ (bottommost)** |
| **Snappy** | 2-2.5x | Niedrig | Schnell | Schnell | Alternative zu LZ4 |
| **Zlib** | 3-4x | Hoch | Langsam | Mittel | ⚠️ Nicht empfohlen |

### Benchmark-Ergebnisse

Test: 10.000 Entities à ~2KB (gemischte JSON-Daten mit Text)

```
Compression         DB Size (MB)    Ratio    Write (MB/s)    Read (MB/s)
------------------------------------------------------------------------------
none / none             ~45         1.0x        34.5           125.3
lz4 / zstd              ~19         2.4x        33.8           118.4
zstd / zstd             ~15         2.9x        32.3           112.7
snappy / zstd           ~19         2.3x        33.1           115.9
```

**Empfehlung:** `compression_default = "lz4"` + `compression_bottommost = "zstd"` für besten Trade-off zwischen Speicherplatz und Performance.

### Konfiguration

```cpp
RocksDBWrapper::Config config;
config.compression_default = "lz4";       // Für Level 0-5
config.compression_bottommost = "zstd";   // Für Level 6+ (selten gelesen)
```

Bei der DB-Erstellung wird die Kompression automatisch aktiviert. Prüfen mit:

```bash
# OPTIONS-Datei inspizieren
cat data/themis_server/OPTIONS-* | grep compression
```

## Ziele
- WAL (Write-Ahead-Log) auf schnelle NVMe
- SSTables verteilt auf mehrere NVMe-Pfade (Hot/Cold möglich)
- Großer Block-Cache im RAM, Index/Filter bevorzugt (High-Priority-Pool)
- Direkte I/O für Flush/Compaction optional, um OS-Cache zu umgehen
- Bloom-Filter und Partitioned-Filter für schnelle Point-Lookups

## Relevante Konfiguration (`RocksDBWrapper::Config`)

- Verzeichnisse
  - `db_path`: Hauptpfad für DB
  - `wal_dir`: separates WAL-Verzeichnis (z. B. NVMe1) — leer = Standard unter `db_path`
  - `db_paths`: Liste aus `{ path, target_size_bytes }` (z. B. NVMe2, NVMe3)

- Kompression
  - `compression_default`: Algorithmus für Level 0-5 (empfohlen: "lz4")
  - `compression_bottommost`: Algorithmus für Level 6+ (empfohlen: "zstd")

- Caches/Filter
  - `block_cache_size_mb`: Größe des Block-Caches in MB
  - `cache_index_and_filter_blocks` (true)
  - `pin_l0_filter_and_index_blocks_in_cache` (true)
  - `partition_filters` (true)
  - `high_pri_pool_ratio` (0.5): Anteil für Index/Filter im Cache
  - `bloom_bits_per_key` (10)

- Write Buffer / Compaction
  - `memtable_size_mb` (z. B. 256)
  - `max_write_buffer_number` (3)
  - `min_write_buffer_number_to_merge` (1)
  - `use_universal_compaction` (false/true)
  - `dynamic_level_bytes` (true)
  - `target_file_size_base_mb` (64)
  - `max_bytes_for_level_base_mb` (256)

- I/O
  - `use_direct_reads` (false)
  - `use_direct_io_for_flush_and_compaction` (false)

- WAL
  - `enable_wal` (true) — `write_options.sync`

- Kompression (best-effort)
  - Default: LZ4 (Levels), ZSTD (Bottommost)

## Beispielkonfiguration

```
themis::RocksDBWrapper::Config cfg;
cfg.db_path = "D:/data/vccdb";            // Hauptpfad
cfg.wal_dir = "E:/logs/vccdb_wal";       // WAL auf separater NVMe
cfg.db_paths = {
    {"D:/data/vccdb",       500ull * 1024 * 1024 * 1024}, // 500 GB
    {"F:/data/vccdb_hot",   500ull * 1024 * 1024 * 1024}  // 500 GB
};

cfg.memtable_size_mb = 512;
cfg.block_cache_size_mb = 4096; // 4 GB Cache
cfg.cache_index_and_filter_blocks = true;
cfg.pin_l0_filter_and_index_blocks_in_cache = true;
cfg.partition_filters = true;
cfg.high_pri_pool_ratio = 0.5;
cfg.bloom_bits_per_key = 10;

cfg.max_write_buffer_number = 4;
cfg.min_write_buffer_number_to_merge = 1;
cfg.use_universal_compaction = false;
cfg.dynamic_level_bytes = true;
cfg.target_file_size_base_mb = 128;
cfg.max_bytes_for_level_base_mb = 1024;

cfg.use_direct_reads = false;
cfg.use_direct_io_for_flush_and_compaction = true;
```

## Hinweise
- Direct I/O kann Performance verbessern, wenn der RocksDB-Block-Cache groß ist und OS-Cache thrashen würde. Testen!
- `db_paths` verteilt neue SSTables über Pfade entsprechend `target_size_bytes`.
- Für GPU-ANN (Faiss-GPU) ist VRAM-Management separat (Task 6/Weiteres); hier nicht enthalten.
- Prüfe `rocksdb.stats` (siehe `RocksDBWrapper::getStats`) nach Lasttests und passe Parameter an.

```
THEMIS_INFO("{}", db.getStats());
```

## Troubleshooting
- Langsame Point-Lookups: `bloom_bits_per_key` erhöhen, `cache_index_and_filter_blocks` aktivieren.
- Hohe Latenzen beim Flush/Compaction: `use_direct_io_for_flush_and_compaction` testen; mehr Background-Jobs (`max_background_jobs`).
- RAM zu knapp: `block_cache_size_mb` reduzieren, `memtable_size_mb` anpassen.
```