# Content Filesystem API

Datum: 19. Nov 2025
Status: Implementiert (Core), Server-Endpoints optional

## Ziel
Einfaches Binär-Content-Storage über RocksDB mit konsistenten Metadaten, optionaler SHA‑256 Integrität und Range‑Reads.

## Schlüsselkonzept
- Datenlayout:
  - `content:<pk>:meta` → CBOR‑codiertes JSON `{ pk, mime, size, sha256_hex }`
  - `content:<pk>:blob` → Binärdaten
- Transaktionen: Einzel‑Writes (Meta dann Blob); für Multi‑Objekt‑Ingest können WriteBatch/Txn genutzt werden.

## C++ API
```cpp
class ContentFS {
public:
  ContentFS(RocksDBWrapper& db);
  struct Status { bool ok; std::string message; };

  Status put(const std::string& pk,
             const std::vector<uint8_t>& data,
             const std::string& mime,
             const std::optional<std::string>& sha256_expected_hex = std::nullopt);

  std::pair<Status, std::vector<uint8_t>> get(const std::string& pk) const;
  std::pair<Status, std::vector<uint8_t>> getRange(const std::string& pk, uint64_t offset, uint64_t length) const;
  std::pair<Status, ContentMeta> head(const std::string& pk) const;
  Status remove(const std::string& pk);

  static std::string sha256Hex(const std::vector<uint8_t>& data);
};
```

### Beispiel
```cpp
RocksDBWrapper db(cfg); db.open();
ContentFS fs(db);

std::vector<uint8_t> bytes = load_file("image.png");
auto hex = ContentFS::sha256Hex(bytes);
auto st = fs.put("img_123", bytes, "image/png", hex);
auto [hst, meta] = fs.head("img_123");
auto [gst, full] = fs.get("img_123");
auto [rst, part] = fs.getRange("img_123", 0, 1024);
```

## Tests
- `tests/test_content_fs.cpp`: Roundtrip, Range‑Reads, Checksum‑Mismatch, Delete → 4/4 PASS (MSVC Debug)

## Performance & Grenzen
- Kleine bis mittlere Blobs (KB–MB) performant direkt in RocksDB/BlobDB.
- Für sehr große Objekte (>100MB) optionales Sharding/Streaming als Erweiterung (Future Work).

## Server‑Endpoints (optional)
- `POST /content` – Upload (Body), Header: `Content-Type`, optional `X-Checksum-SHA256`.
- `GET /content/{pk}` – Download; unterstützt `Range:`.
- `HEAD /content/{pk}` – Metadaten.
- `DELETE /content/{pk}` – Löschen.

Hinweis: Endpoints sind aktuell nicht verdrahtet; Implementierung kann über `themis_server` Handler folgen.
