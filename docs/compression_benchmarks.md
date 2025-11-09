# Kompressionsvalidierung und Benchmarks

## Erweiterte Blob-Kompressionsmetriken

Neue Metriken (Prometheus):

```
themis_compressed_blobs_total
themis_uncompressed_blobs_total
themis_compression_original_bytes_total
themis_compression_compressed_bytes_total
themis_compression_ratio_average
themis_compression_level_original_bytes_total{level="<n>"}
themis_compression_mime_groups_total{group="text|image|video|application|audio|other"}
themis_compression_time_microseconds_bucket / _sum / _count
themis_decompression_time_microseconds_bucket / _sum / _count
```

Diese erlauben:
- Globale Ratio-Trends
- Level-Verteilung (welche Levels verarbeiten wie viel Daten)
- Latenz-Histogramme für (De)Kompression
- MIME-Gruppen-Lastprofil

## Per-MIME Compression Policy

Konfiguration (`/content/config`):

```json
{
  "compress_blobs": true,
  "compression_level": 19,
  "skip_compressed_mimes": ["image/", "video/", "application/zip"],
  "compression_levels_map": {
    "text/": 9,
    "text/plain": 9,
    "application/json": 3
  }
}
```

Auflösung:
1. Exakter MIME-Treffer (`text/plain`).
2. Prefix mit Slash (`text/`).
3. Fallback auf globalen `compression_level`.

Ungültige Level (<1 oder >22) werden ignoriert bzw. bei PUT validiert (400).

## ZSTD Level Microbenchmarks

Neue Benchmark-Datei: `benchmarks/bench_blob_zstd.cpp` testet Levels 3, 9, 19 auf 16KB und 128KB Text. Counter `ratio` = (original/compressed).

Prometheus/Bench-Nutzung siehe unten; für echte Zahlen bitte Benchmark ausführen.

**Datum:** 27. Oktober 2025  
**System:** Windows 11, MSVC 19.44, 20 CPU cores @ 3.7 GHz

## Validierung

Die Kompression wurde zur Laufzeit verifiziert:
- **none:** `default=none, bottommost=none`
- **lz4:** `default=lz4, bottommost=lz4`
- **zstd:** `default=zstd, bottommost=zstd`

RocksDB nutzt die in vcpkg.json aktivierten Features (`lz4`, `zstd`) korrekt.

## Benchmark-Ergebnisse

### Sequential Write (1000 keys per iteration)

| Compression | Blob Size | Time/Iter | Throughput (MB/s) | Items/s |
|-------------|-----------|-----------|-------------------|---------|
| none        | 512B      | 23.6 ms   | 22.7 MB/s         | 46.4k   |
| **lz4**     | 512B      | 22.0 ms   | 24.1 MB/s         | 49.3k   |
| **zstd**    | 512B      | 21.9 ms   | 25.6 MB/s         | 52.5k   |
| none        | 4KB       | 29.7 ms   | 147.7 MB/s        | 37.8k   |
| **lz4**     | 4KB       | 31.2 ms   | 141.0 MB/s        | 36.1k   |
| **zstd**    | 4KB       | 31.6 ms   | 148.6 MB/s        | 38.1k   |
| none        | 16KB      | 49.8 ms   | 348.8 MB/s        | 22.3k   |
| **lz4**     | 16KB      | 54.1 ms   | 289.5 MB/s        | 18.5k   |
| **zstd**    | 16KB      | 53.4 ms   | 294.1 MB/s        | 18.8k   |

### Random Read (warm cache, 1000 pre-populated keys)

| Compression | Blob Size | Latency (µs) | Items/s   |
|-------------|-----------|--------------|-----------|
| none        | 4KB       | 2.32         | 434k      |
| **lz4**     | 4KB       | 2.63         | 383k      |
| **zstd**    | 4KB       | 2.61         | 412k      |

## Interpretation

### Write Performance
- **Kleine Blobs (512B):**  
  ZSTD und LZ4 **schneller** als `none` (~7-13% Verbesserung). Die Kompression reduziert I/O und Write Amplification stärker als die CPU-Kosten wiegen.
  
- **Mittlere Blobs (4KB):**  
  Alle drei Varianten ähnlich (~147 MB/s). ZSTD minimal schneller bei hoher Kompressibilität.

- **Große Blobs (16KB):**  
  `none` **deutlich schneller** (+20% gegenüber LZ4/ZSTD). CPU-Kosten für Kompression überwiegen I/O-Einsparungen bei großen Payloads.

### Read Performance
- LZ4 und ZSTD fügen ~14% Latenz hinzu (Dekompressions-Overhead).
- Bei Cache-Hits (reiner memcpy) ist `none` am schnellsten.
- Im realen Betrieb mit Disk-I/O kann Kompression durch geringere Datenmengen schneller sein.

## Empfehlungen

1. **Für hohen Write-Throughput mit kleineren Entities (< 1KB):**  
   → **ZSTD** (default) oder **LZ4** (bottommost) nutzen

2. **Für große BLOBs (> 8KB) oder read-heavy Workloads:**  
   → **LZ4** für Reads mit weniger Latenz; oder **none** für maximalen Read-Durchsatz

3. **Hybrid-Konfiguration (empfohlen):**
   ```json
   "compression": {
     "default": "lz4",
     "bottommost": "zstd"
   }
   ```
   Frische Daten (L0) mit LZ4 schnell komprimiert; ältere Levels (bottommost) mit ZSTD platzsparend.

## Write Amplification

Ohne direkte Messung lässt sich aus den Benchmarks ableiten:
- Kompression reduziert SSTable-Größe → weniger Compaction-Aufwand
- Bei kompressiblen JSON-Daten (Faktor ~3-5x) führt Kompression zu **niedrigerer Write Amplification**

Für genaue Werte: RocksDB-Property `rocksdb.total-sst-files-size` vor/nach Schreibvorgängen prüfen.

## Nächste Schritte

- [ ] Write Amplification mit RocksDB `GetProperty("rocksdb.total-sst-files-size")` messen
- [ ] Disk-I/O Benchmarks (cold cache) mit verschiedenen Kompressionen
- [ ] Speicherplatzvergleich nach 10k/100k Entities
