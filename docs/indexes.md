# Sekundärindizes – Überblick und Verwendung

Dieser Leitfaden beschreibt die in THEMIS verfügbaren Sekundärindizes, ihre Key-Schemata und die korrekte Verwendung in Code.

- Single-Column (Equality): `idx:table:column:value:PK`
- Composite (Mehrspaltig): `idx:table:col1+col2:val1:val2:PK`
- Range (Sortiert/Range-Scan): `ridx:table:column:value:PK`
- Sparse (NULL/leer überspringen): `sidx:table:column:value:PK`
- Geo (Geohash/Morton): `gidx:table:column:geohash:PK`
- TTL (Ablaufzeit): `ttlidx:table:column:timestamp:PK`
- Fulltext (Invertierter Index): `ftidx:table:column:token:PK`

Hinweise
- Composite-Indizes verwenden das gleiche Präfix wie Single-Column (`idx:`); die Spaltennamen werden im `column`-Teil durch `+` getrennt.
- Geo-Indizes erwarten Felder `<column>_lat` und `<column>_lon` als Strings (z. B. `"52.5"`, `"13.4"`).
- TTL-Indizes speichern Expire-Timestamps in Sekunden; die tatsächliche Löschung erfolgt über einen Cleanup-Lauf.
- Fulltext-Tokenizer: simples Whitespace/Punktuation-Splitting, Lowercasing, AND-Logik bei Suche.

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

## Fehler- und Kantenfälle

- Leere oder fehlende Felder: werden von Sparse und TTL korrekt behandelt (Sparse: skip; TTL: kein Eintrag).
- Typkonflikte: Werte werden als Strings behandelt; sortierte Range-Scans sind lexikografisch (ggf. Vorverarbeitung/Zero-Padding nutzen).
- Geo: Ungültige Zahlenwerte werden beim Rebuild/Put übersprungen.
- Unique: Verstöße liefern Status mit `ok=false`.
