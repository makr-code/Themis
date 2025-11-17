```markdown
# vector_index.cpp

Path: `src/index/vector_index.cpp`

**Purpose:** Verwaltung des ANN Vektorindex (VectorIndexManager). Unterstützt HNSW (nur wenn mit `THEMIS_HNSW_ENABLED` kompiliert), optionale Quantisierung und Persistenz/Auto‑Save.

**Kernkonzepte:**
- **VectorIndexManager:** zentrale Klasse für Index‑Lifecycle (init, add/update/remove, search, save/load).
- **Metriken:** COSINE (normalisiert) und L2 werden unterstützt.
- **Optional:** HNSW (abrufbar über Compile‑Flag), Quantisierung (embedding_q + embedding_scale) für Speicher/Performance‑Tradeoffs.
- **Persistenz:** speichert Index‑Artefakte unter einem AutoSave‑Pfad in Dateien wie `meta.txt`, `labels.txt`, `index.bin`.

**Wichtige API‑Funktionen (Übersicht):**
- `init(...)` — initialisiert/erstellt einen Index (Name/Dimension/Metrik/Optionen).
- `addEntity(...)` — fügt ein Entity mit Primärschlüssel und optionaler Embedding ein.
- `updateEntity(...)` — aktualisiert Felder oder Embedding eines existierenden Entities.
- `removeByPk(const std::string& pk)` — entfernt ein Objekt nach PK.
- `searchKnn(query_embedding, k, whitelist_ptr)` — führt eine k‑NN Suche aus und liefert (Status, Ergebnisse).
- `saveIndex()` / `loadIndex()` — persistiert bzw. lädt Index‑Daten auf/aus Disk.
- `rebuildFromStorage()` — baut Index aus persistenten Labels/Metadaten neu auf.

**Persistenz & Format:**
- Standard‑Speicherpfad: konfigurierbar via `setAutoSavePath(...)`.
- Erwartete Dateien: `meta.txt` (Index‑Metadaten JSON), `labels.txt` (pk/label mapping), `index.bin` (binäres HNSW/Index blob).
- Wichtig: Beim Laden wird Konsistenz geprüft; bei Fehlern kann ein Rebuild erforderlich sein.

**Performance / Tuning:**
- `efSearch` / `efConstruction` (wenn HNSW aktiv) beeinflussen Recall vs Throughput.
- Quantisierung (`embedding_q`) reduziert Speicher, kann aber Recall verschlechtern — Benchmarks notwendig.
- Rebuilding aus Storage ist teuer (I/O + CPU); für große Indices Wartungs‑Fenster empfehlen.

**Beispiel (Pseudocode):**
```cpp
// Index initialisieren
VectorIndexManager idx;
idx.init("chunks", dim, VectorIndexManager::Metric::COSINE);
// Entity anlegen
BaseEntity e = BaseEntity::fromFields("chunks:123", {{"embedding", embedding}});
idx.addEntity(e);
// Suche
auto [st, res] = idx.searchKnn(query, 10, nullptr);
for (auto &r : res) { printf("pk=%s score=%f\n", r.pk.c_str(), r.distance); }
```

**Empfohlene Ergänzungen / TODOs:**
- Unit‑/Integration‑Tests für Recall/Precision mit/ohne Quantisierung.
- Dokumentation der genauen Signaturen (Header `include/index/` verlinken).
- Beispiel‑Benchmarks (Ingest‑Durchsatz, Query‑Latency, Speicherbedarf).

```
