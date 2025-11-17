# Indexe – Überblick und Verwendung

Dieser Leitfaden beschreibt die in THEMIS verfügbaren Indexe (Sekundär-, Range-, Geo-, TTL-, Fulltext-, Graph- und Vektorindizes), ihre Key-Schemata und die korrekte Verwendung im Code.

## Key-Schemata (Präfixe)

- Equality (Single): `idx:<table>:<column>:<value>:<PK>`
- Equality (Composite): `idx:<table>:<col1+col2>:<val1>:<val2>:<PK>`
- Range (lexikografisch): `ridx:<table>:<column>:<value>:<PK>`
- Sparse: `sidx:<table>:<column>:<value>:<PK>`
- Geo (Geohash/Morton): `gidx:<table>:<column>:<geohash>:<PK>`
- TTL (Expiry): `ttlidx:<table>:<column>:<timestamp>:<PK>`
- Fulltext (invertiert): `ftidx:<table>:<column>:<token>:<PK>`
- Graph (Adjazenz, logisch):
  - Out: `graph:out:<from_pk>:<edge_id> -> <to_pk>`
  - In:  `graph:in:<to_pk>:<edge_id>   -> <from_pk>`
- Vector (per Entity gespeichert): `objectName:<PK>` mit Feld `embedding`

Hinweise
- Composite-Indizes verwenden das gleiche Präfix wie Single-Column (`idx:`); die Spaltennamen werden im `column`-Teil durch `+` getrennt.
- Geo-Indizes erwarten Felder `<column>_lat` und `<column>_lon` als Strings (z. B. `"52.5"`, `"13.4"`).
- TTL-Indizes speichern Expire-Timestamps in Sekunden; die tatsächliche Löschung erfolgt über einen Cleanup-Lauf.
- Fulltext-Tokenizer: simples Whitespace/Punktuation-Splitting, Lowercasing, AND-Logik bei Suche.
 - Range-Scans sind lexikografisch (String-Encoding!). Für numerische Ordnung ggf. Zero-Padding oder Canonical-Encoding verwenden.
 - VectorIndex nutzt HNSW (falls mit THEMIS_HNSW_ENABLED gebaut) oder Fallback (Brute-Force) mit in-memory Cache.

## API-Snippets (C++)

Vorbereitung:

```cpp
#include "index/secondary_index.h"
#include "storage/base_entity.h"
using themis::SecondaryIndexManager;
using themis::BaseEntity;
```

### Equality-Index (Single-Column)

```cpp
SecondaryIndexManager idx(db);
idx.createIndex("users", "email", /*unique=*/false);

BaseEntity e("user42");
e.setField("email", "u42@example.com");
idx.put("users", e);

auto [st, keys] = idx.scanKeysEqual("users", "email", "u42@example.com");
```

- Unique-Constraint: `createIndex(table, column, /*unique=*/true)` verhindert doppelte Values.

### Composite-Index

```cpp
idx.createCompositeIndex("orders", {"customer_id", "status"}, /*unique=*/false);

BaseEntity o("order1");
o.setField("customer_id", "cust1");
o.setField("status", "pending");
idx.put("orders", o);

auto [st, keys] = idx.scanKeysEqualComposite("orders", {"customer_id","status"}, {"cust1","pending"});
```

Wichtig: Composite benutzt `idx:` mit `column = col1+col2` und `value = val1:val2` (intern percent-encodiert, falls nötig).

### Range-Index

```cpp
idx.createRangeIndex("users", "age");

auto [st, keys] = idx.scanKeysRange(
  "users", "age",
  /*lower*/ std::make_optional(std::string("18")),
  /*upper*/ std::make_optional(std::string("65")),
  /*includeLower*/ true, /*includeUpper*/ false,
  /*limit*/ 1000, /*reversed*/ false);
```

Range-Index ist unabhängig vom Equality-Index auf derselben Spalte.

#### Cursor-Anker (Pagination)

```cpp
auto [st, page1] = idx.scanKeysRangeAnchored(
  "users", "age",
  /*lower*/ std::nullopt, /*upper*/ std::nullopt,
  /*inclL*/ true, /*inclU*/ true,
  /*limit*/ 50, /*reversed*/ false,
  /*anchor*/ std::nullopt
);

// Nächste Seite (weiter ab letztem (value, pk))
auto last = page1.back(); // PK der letzten Zeile
// den zuletzt gesehenen Wert (age) aus Entity lesen
auto [stE, ents] = idx.scanEntitiesEqual("users", "pk", last);
std::string lastAge = ents.empty()?"":ents[0].getString("age");
auto [st2, page2] = idx.scanKeysRangeAnchored(
  "users", "age",
  std::nullopt, std::nullopt,
  true, true,
  50, false,
  std::make_optional(std::make_pair(lastAge, last))
);
```

### Sparse-Index

```cpp
idx.createSparseIndex("users", "nickname", /*unique=*/false);
```

Leere/fehlende Felder werden nicht indexiert – spart Speicher.

### Geo-Index

```cpp
idx.createGeoIndex("places", "coords");

BaseEntity p("p1");
p.setField("coords_lat", "52.52");
p.setField("coords_lon", "13.40");
idx.put("places", p);

auto [st1, inBox] = idx.scanGeoBox("places", "coords", 52.0, 53.0, 13.0, 14.0);
auto [st2, inRadius] = idx.scanGeoRadius("places", "coords", 52.52, 13.40, 5.0 /*km*/);
```

### TTL-Index

```cpp
idx.createTTLIndex("sessions", "last_seen", /*ttl_seconds=*/3600);

// Periodisch aufrufen (z. B. CRON/Timer)
auto [st, removed] = idx.cleanupExpiredEntities("sessions", "last_seen");
```

- Beim Put wird ein Ablauf-Timestamp berechnet und als `ttlidx:`-Eintrag abgelegt.
- `cleanupExpiredEntities` löscht abgelaufene Entities und zugehörige Indizes atomar.

### Fulltext-Index

```cpp
idx.createFulltextIndex("articles", "body");

BaseEntity a("a1");
a.setField("body", "Fast search with inverted index.");
idx.put("articles", a);

auto [st, hits] = idx.scanFulltext("articles", "body", "fast inverted");
```

Tokens werden whitespace-/punktuationsbasiert extrahiert; Suche nutzt AND-Logik über alle Tokens.

## Graph-Index (Property Graph)

Header: `index/graph_index.h`

Funktionen:

- `addEdge(edgeEntity)` / `deleteEdge(edgeId)` – atomar via WriteBatch/MVCC
- `outNeighbors(pk)` / `inNeighbors(pk)` – Nachbarschaftsabfragen
- Traversierungen: `bfs`, `dijkstra`, `aStar`
- Zeitliche Varianten: `bfsAtTime`, `dijkstraAtTime`, `getEdgesInTimeRange`

Edge-Entity-Felder: `id`, `_from`, `_to`, optional `_weight`, `valid_from`, `valid_to`

Beispiel:

```cpp
GraphIndexManager gidx(db);

BaseEntity e("edge-1");
e.setField("_from", "chunk-1");
e.setField("_to", "chunk-2");
e.setField("_weight", "1.0");
gidx.addEdge(e);

auto [st, outs] = gidx.outNeighbors("chunk-1");
```

Statistiken: `getTopologyNodeCount()`, `getTopologyEdgeCount()`

## Vektorindex (KNN/ANN)

Header: `index/vector_index.h`

Konfiguration und Aufbau:

```cpp
VectorIndexManager vix(db);
vix.init("chunks", /*dim=*/768, VectorIndexManager::Metric::COSINE, /*M*/16, /*efC*/200, /*ef*/64);

// Entity mit Embedding hinzufügen
BaseEntity c("chunk-1");
c.setVector("embedding", std::vector<float>(768, 0.1f));
vix.addEntity(c);

// Suche
std::vector<float> q(768, 0.05f);
auto [st, res] = vix.searchKnn(q, 10);
```

Hinweise:

- HNSW optional (compile flag `THEMIS_HNSW_ENABLED`), sonst Brute-Force Fallback
- Persistenz: `saveIndex/loadIndex` bzw. `setAutoSavePath` und `shutdown()`
- Laufzeit-Tuning: `setEfSearch()` (Tradeoff Genauigkeit/Latenz)

## Fehler- und Kantenfälle

- Leere oder fehlende Felder: werden von Sparse und TTL korrekt behandelt (Sparse: skip; TTL: kein Eintrag).
- Typkonflikte: Werte werden als Strings behandelt; sortierte Range-Scans sind lexikografisch (ggf. Vorverarbeitung/Zero-Padding nutzen).
- Geo: Ungültige Zahlenwerte werden beim Rebuild/Put übersprungen.
- Unique: Verstöße liefern Status mit `ok=false`.
 - Graph: Fehlende `_from`/`_to`-Felder führen zu `Status::Error`.
 - Vektor: Dimension muss konsistent zur Initialisierung sein; falsche Dimension ⇒ `Status::Error`.
