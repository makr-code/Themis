````markdown
# Content‑Blob ZSTD Compression und HKDF‑Caching — Detaillierte Dokumentation

Datum: 16. November 2025

Dieses Dokument beschreibt zwei verwandte, aber eigenständige Funktionsbereiche, die im Repository implementiert sind: die Content‑Blob Vor‑Kompression mit ZSTD und das Thread‑local HKDF LRU‑Cache für Key‑Derivationen. Es fasst Verhalten, Konfiguration, zentrale Codepfade, Tests, offene Punkte und DoD zusammen.

---

**1) Content‑Blob ZSTD Compression**

Kurzstatus
- Implementierungsstatus: Implementiert (Code vorhanden, Feature‑Flag / Config ausgewertet)
- Ziel: Große textbasierte Blobs (z.B. PDF, DOCX, TXT) vor persistenter Ablage mit ZSTD (konfigurierbar, default level 19) komprimieren; bereits komprimierte MIME‑Typen überspringen.

Motivation
- Spart Speicherplatz und Netzwerkkosten bei Upload/Backup.
- ZSTD Level 19 bietet hohe Ratio für textlastige Formate; CPU‑Kosten sind auf Upload‑Pfad akzeptabel.

Verhalten / Ablauf (End‑to‑End)
- Aufrufpunkt: `ContentManager::importContent(const json& spec, const std::optional<std::string>& blob)`
  - Wenn ein `blob` übergeben wird, lädt `importContent()` zunächst `config:content` aus RocksDB (falls vorhanden).
  - Konfigurationswerte gelesen: `compress_blobs` (bool), `compression_level` (int, default 19) und `skip_compressed_mimes` (array of strings, default includes `image/`, `video/`, `application/zip`, `application/gzip`).
  - `should_compress(mime, size)` entscheidet basierend auf `compress_blobs`, MIME‑Prefix‑Excusions und Mindestgröße (>4 KB standardmäßig)
  - Bei Entscheidung zu komprimieren: `utils::zstd_compress` aufrufen; falls Kompression erfolgreich → gespeicherte Bytes = komprimiert, `ContentMeta.compressed = true`, `compression_type = "zstd"`.
  - Blob wird in RocksDB unter Key `content_blob:<content_id>` gespeichert (binary value). Meta wird unter `content:<id>` aktualisiert.
- Download: `ContentManager::getContentBlob(content_id)` überprüft `ContentMeta` und, wenn `compressed` && `compression_type == "zstd"`, wird `utils::zstd_decompress` verwendet — Rückgabe der dekomprimierten Bytes (Fallback: bei Build ohne ZSTD oder Decompress‑Fehler raw bytes zurückgeben).

Zentrale Code‑Fundstellen
- API / Metadata
  - `include/content/content_manager.h` — `ContentMeta` Struktur (Felder `compressed`, `compression_type`) und Signaturen `importContent()` / `getContentBlob()`.
- Implementation
  - `src/content/content_manager.cpp` — Implementierung von `importContent()` (kompression decision, speichern) und `getContentBlob()` (dekompression/return).
- ZSTD Wrapper
  - `include/utils/zstd_codec.h` (Deklaration) — `zstd_compress`, `zstd_decompress`.
  - `src/utils/zstd_codec.cpp` — Wrapper um ZSTD APIs (`ZSTD_compress`, `ZSTD_decompress`, `ZSTD_getFrameContentSize`), bedingt durch `THEMIS_HAS_ZSTD`.

Konfiguration (Beispiel)
- Store in RocksDB config key `config:content` as JSON or via runtime config store. Beispiel:
{
  "compress_blobs": true,
  "compression_level": 19,
  "skip_compressed_mimes": ["image/","video/","application/zip","application/gzip"]
}

Hinweise zu MIME‑Matching
- Default‑liste enthält Präfixe wie `image/` → `mime.rfind("image/", 0) == 0` wird als Skip interpretiert. Exakte Matches (z. B. `application/zip`) ebenfalls möglich.

Build/Feature Flags
- Der ZSTD‑Wrapper ist bedingt kompiliert: `THEMIS_HAS_ZSTD` (CMake/vcpkg). Wenn nicht verfügbar, wird die Logik in `ContentManager` nicht komprimieren (Fallback: raw bytes werden gespeichert).

Tests / Verifikation
- Tests erwähnt in Repo: `tests/test_content_manager_*.cpp` (beispielhafte Tests in wiki/out); konkret prüfen:
  - Roundtrip: upload blob → stored compressed bytes under `content_blob:<id>` + meta `compressed=true` → `getContentBlob()` liefert original bytes
  - MIME skip: upload an image → ensure not compressed
  - Failover: if `zstd_compress` returns empty → store raw bytes and `compressed=false`
- Empfehlung: Ergänze Unit Test `tests/test_content_zstd_roundtrip.cpp` mit parametrisierten MIME‑Typen.

Metriken / Observability
- Empfohlen: Metriken (Prometheus) hinzufügen
  - `content.blobs.total_uploaded`, `content.blobs.compressed_total`, `content.blobs.skipped_mime_total`, `content.blobs.compression_time_ms`, `content.blobs.decompression_time_ms`.

Edge Cases
- Sehr große Blobs: prüfen Memory‑Footprint beim Komprimieren (streaming API evtl. notwendig)
- Already compressed files (e.g. zip inside pdf) — MIME heuristics only; content sniffing optional
- Backwards compatibility: ensure existing `content_blob` entries without meta flags are handled gracefully.

DoD (Definition Of Done)
- Upload API speichert komprimierte blobs wenn konfiguriert.
- Download API liefert dekomprimiert dieselben Bytes.
- Unit tests für roundtrip, skip‑mime, failure‑fallback implementiert.
- Konfigurationsdokumentation (siehe Beispiel) in `docs/` vorhanden.
- Prometheus Metriken und logs für compression events.

Nächste Schritte / Empfehlungen
- Ergänze parametrisierten Unit/Test‑Matrix für verschiedene MIME‑Typen und Größen.
- Erwäge streaming‑Compression für sehr große Blobs (verringert peak memory).
- Füge Metriken & thresholds für monitoring hinzu.

---

**2) HKDF‑Caching für Encryption (Thread‑local LRU)**

Kurzstatus
- Implementierungsstatus: Implementiert
- Ziel: Wiederverwendung abgeleiteter Schlüsselmaterialien (HKDF) in Hot‑Paths, mit thread‑local LRU Cache, TTL/Capacity konfigurierbar.

Motivation
- HKDF Ableitungen (z. B. DEK→field‑key) sind CPU‑aufwendig; viele Entities/Requests wiederverwenden dieselben base IKM/salt/info. Ein Cache reduziert wiederholte Ableitungen und verbessert Latenz.

Verhalten / Laufzeit
- API (Header): `include/utils/hkdf_cache.h`
  - `static HKDFCache& threadLocal();` — gibt thread‑lokalen Cache zurück.
  - `std::vector<uint8_t> derive_cached(const std::vector<uint8_t>& ikm, const std::vector<uint8_t>& salt, const std::string& info, size_t output_length);`
  - `void clear(); void setCapacity(size_t cap);`
- Implementation: `src/utils/hkdf_cache.cpp`
  - Intern: LRU List + unordered_map<Key, bytes>, protected by a mutex (defensive), mit default capacity 1024.
  - `Impl::make_key` serialisiert ikm|salt|info|outlen in Key string for map lookup.
  - On cache miss: calls `HKDFHelper::derive(...)` to perform HKDF and stores result; on hit, moves key to front of LRU.
  - `threadLocal()` returns `thread_local HKDFCache instance` → avoids cross-thread contention (but mutex retained for defensive safety).

Code‑Fundstellen
- Deklaration: `include/utils/hkdf_cache.h`
- Implementierung: `src/utils/hkdf_cache.cpp`
- HKDF primitive helper referred: `src/utils/hkdf_helper.h` / `hkdf_helper.cpp` (Helper implementiert eigentliche HKDF via OpenSSL/Evp or custom) — 
  (Anmerkung: HKDFHelper wurde in docs referenziert; suche/prüfe `src/utils/hkdf_helper.*` falls benötigt.)

Konfiguration / Runtime
- Cache Capacity kann per `setCapacity(size_t)` verändert werden; default ≈ 1024 entries.
- Keine TTL in der aktuellen Impl (impl comment erwähnte TTL/TTL‑Support in docs — derzeit LRU only). Wenn TTL gewünscht, erweitern: store timestamp per entry + periodic purge.

Tests / Verifikation
- Tests empfehlenswert:
  - Single thread: mehrfacher derive_cached Aufruf mit identischen inputs → verify derive_cached returns same bytes and underlying HKDFHelper called once (requires mocking or instrumentation)
  - Multi thread: Each thread has own `threadLocal()` → confirm low contention und correctness
  - Eviction test: set small capacity and derive > capacity distinct keys, confirm oldest evicted

DoD (Definition Of Done)
- HKDFCache threadLocal() returns usable cache und `derive_cached` returns correct derived bytes on hit/miss.
- Unit tests: hit/miss/eviction behavior.
- Optional: add TTL support und metrics (cache_hit_count, cache_miss_count, cache_size).

Nächste Schritte / Empfehlungen
- Ergänze TTL‑Support falls Keys should expire (security policy)
- Instrumentiere metrics (hit/miss) und optional histogram für derive latency
- Nutze HKDFCache in Batch‑Encryption path (`FieldEncryption::encryptEntityBatch`) to ensure one base key fetch + many HKDF derives are efficient.

---

Anhang: Beispiel‑Konfiguration und Test‑Befehle

Konfig‑Beispiel (RocksDB key `config:content`):
```json
{
  "compress_blobs": true,
  "compression_level": 19,
  "skip_compressed_mimes": ["image/","video/","application/zip","application/gzip"]
}
```

Schneller Test (lokal):
- Build tests (Windows PowerShell task provided in workspace):

```powershell
# from workspace root (PowerShell)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64; cmake --build build --config Debug --target themis_tests
.
# oder mit der vorhandenen VS Code task: Run task "Build tests (Debug)"
```

Empfohlene Unit‑Tests (zu implementieren/erweitern):
- `tests/test_content_zstd_roundtrip.cpp` — parameterized by mime/size
- `tests/test_hkdf_cache_eviction.cpp` — capacity & eviction



````
