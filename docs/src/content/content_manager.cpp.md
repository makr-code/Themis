```markdown
# content_manager.cpp

Path: `src/content/content_manager.cpp`

**Purpose:** Implementiert `ContentManager` für Content‑Ingest, Chunking, optionale ZSTD‑Kompression, Metadatenverwaltung und Integration mit Vector/Graph Indizes.

**Kerntypen:**
- `ContentMeta` — Metadaten eines Content‑Objekts (mime_type, size_bytes, compressed, chunk_count, embedding_dim, timestamps, hashes).
- `ChunkMeta` — Metadaten eines Chunks (seq_num, chunk_type, embedding, content_id).
- `ContentManager` — bietet Import/Get/Delete/Search APIs.

**Wichtige API‑Funktionen (Übersicht):**
- `importContent(spec, blob)` — Ingest eines Content‑Objekts.
- `getContentMeta(content_id)` — lädt `ContentMeta`.
- `getContentBlob(content_id)` — liefert entpackten Blob (falls komprimiert, Dekompression intern).
- `getContentChunks(content_id)` — liefert Chunk‑Liste (`ChunkMeta`).
- `getChunk(chunk_id)` — lädt einzelnes Chunk‑Meta.
- `searchContent(query_text, k, filters)` — vektorbasierte Suche via TextProcessor→Embedding.
- `searchWithExpansion(query_text, k, expansion_hops, filters)` — erweitert per Graph BFS/Dijkstra.
- `deleteContent(content_id)` — löscht Blob, Chunks, Metadaten und Index‑Einträge.
- `getStats()` — einfache Statistiken (counts, approximate storage size).

**Speicher‑Keys / Konventionen:**
- `content:<id>` → ContentMeta JSON
- `content_blob:<id>` → Blob (roh oder ZSTD)
- `chunk:<id>` → ChunkMeta JSON
- `content_chunks:<id>` → JSON Liste der chunk ids
- `content_hash:<sha256>` → lookup für Duplikaterkennung

**Import‑Ablauf (`importContent`):**
1. `spec` JSON parsen (metadaten, chunks, edges optional).
2. Generiere IDs falls notwendig (UUID fallback).
3. Entscheide Kompression über Heuristik + `config:content` (z.B. skip small mime types, size threshold ~4KB).
4. Wenn komprimiert → ZSTD (nur wenn `THEMIS_HAS_ZSTD` verfügbar), sonst roher Blob.
5. Speichere Blob unter `content_blob:<id>` und Meta unter `content:<id>`.
6. Für jeden Chunk: speichere `chunk:<id>`, optional Embedding → `vector_index_->addEntity(...)` in `chunks:` namespace.
7. Optional: Kanten in `graph_index_` anlegen.

**Kompressions‑Heuristik (aktuell):**
- MIME‑Whitelist / Blacklist: gewisse textliche MIMEs komprimieren, bereits komprimierte MIMEs (z. B. `image/*`, `video/*`) werden ausgelassen.
- Größenheuristik: Standardfall komprimieren wenn > ~4096 Bytes (siehe Quellcode: `return size > 4096;`).
- Build‑Zeit: Wenn ZSTD nicht vorhanden ist (kein `THEMIS_HAS_ZSTD`) → Fallback auf rohen Blob.

**Deduplication:**
- SHA256 des Inhalts wird berechnet und in `content_hash:<hex>` abgelegt; ein Treffer kann Import überspringen oder referenzieren.

**Suche & Expansion:**
- `searchContent` nutzt einen `TextProcessor` (falls registriert) zur Erstellung einer Query‑Embedding und ruft `vector_index_->searchKnn` auf.
- `searchWithExpansion` kombiniert Vector‑Scores mit Graph‑Expansion (BFS) und optionalen Dijkstra‑Kosten; Parameter `alpha/beta/gamma` steuern Gewichtung und Hop‑Penalty.

**Fehlerbehandlung / Fallbacks:**
- Alle `storage_->put`/`get` Rückgaben werden geprüft; fehlgeschlagene Stores geben `Status::Error` zurück.
- Dekompressionsfehler führen zu Rückgabe des rohen Bytestrings (Fallback).

**Beispiel `spec` (Import JSON):**
```json
{
	"id": "", // optional
	"mime_type": "text/plain",
	"title": "Beispiel",
	"chunks": [ { "seq_num": 0, "id": "", "embedding": [0.1, 0.2, ...] } ],
	"edges": [ { "from": "obj:1", "to": "obj:2", "weight": 1.0 } ]
}
```

**Pseudocode‑Beispiel (Ingest → Suche):**
```cpp
ContentManager cm(storage, vector_index, graph_index);
json spec = ...; std::string blob = readFile("file.txt");
auto st = cm.importContent(spec, blob);
auto meta = cm.getContentMeta(st.id);
auto results = cm.searchContent("Suchtext", 10, json::object());
```

**Wichtige Ergänzungen / TODOs:**
- E2E Tests: ingest → getBlob → searchContent → deleteContent (inkl. Kompressionsvarianten).
- Concurrency: dokumentieren, welche atomic/transactional Guarantees Storage bietet und ob externes Locking empfohlen wird (race beim Setzen von `embedding_dim`).
- Explizite Beispiele für `config:content` Settings.

```
