# Basismodell der Datenbank

**Version:** 1.1  
**Status:** Implementiert  
**Letzte Aktualisierung:** 28. Oktober 2025

---

## Überblick

Das **Basismodell** von THEMIS definiert die fundamentale Speichereinheit für alle Datenmodelle (relational, document, graph, vector). Jede logische Entität wird als **BaseEntity** gespeichert – ein einzelnes binäres Blob mit effizienter Serialisierung und schnellem Feldzugriff.

### Kernkonzepte

1. **BaseEntity**: Kanonische Speichereinheit (ein Blob pro Entität)
2. **Primary Key**: Eindeutige Identifikation innerhalb eines Namespace (table/collection/graph)
3. **Key Schema**: Hierarchisches Schlüssel-System für Multi-Modell-Zugriff
4. **TTL/Retention**: Automatische Ablauf-Mechanik über TTL-Indizes
5. **Path Constraints**: Geplante Constraints für Graph-Traversals (siehe [Path Constraints](path_constraints.md))

---

## 1. BaseEntity – Die Speichereinheit

### 1.1 Architektur

`BaseEntity` ist die zentrale Klasse für alle persistierten Daten:

```cpp
class BaseEntity {
public:
    using Blob = std::vector<uint8_t>;
    using FieldMap = std::map<std::string, Value>;
    
    enum class Format { BINARY, JSON };
    
    // Primärschlüssel
    const std::string& getPrimaryKey() const;
    void setPrimaryKey(std::string_view pk);
    
    // Feld-Zugriff (lazy parsing)
    std::optional<Value> getField(std::string_view field_name) const;
    void setField(std::string_view field_name, const Value& value);
    
    // Serialisierung
    Blob serialize() const;
    std::string toJson() const;
    
    // Factory-Methoden
    static BaseEntity fromJson(std::string_view pk, std::string_view json_str);
    static BaseEntity fromFields(std::string_view pk, const FieldMap& fields);
    static BaseEntity deserialize(std::string_view pk, const Blob& blob);
    
private:
    std::string primary_key_;
    Blob blob_;
    Format format_ = Format::BINARY;
    mutable std::shared_ptr<FieldMap> field_cache_;
};
```

### 1.2 Value-Typsystem

`Value` ist ein `std::variant` mit folgenden Typen:

| Typ | C++ Typ | Verwendung |
|-----|---------|------------|
| **null** | `std::monostate` | Fehlende/leere Werte |
| **bool** | `bool` | Wahrheitswerte |
| **int** | `int64_t` | Ganzzahlen |
| **double** | `double` | Gleitkommazahlen |
| **string** | `std::string` | Texte |
| **vector** | `std::vector<float>` | Embeddings (optimiert für ANN-Indizes) |
| **binary** | `std::vector<uint8_t>` | Binärdaten |

**Hinweis:** Nested Objects/Arrays sind aktuell nicht unterstützt (geplant für zukünftige Version).

---

## 2. Key Schema – Namespacing & Hierarchie

### 2.1 Multi-Modell-Schlüssel

THEMIS nutzt ein hierarchisches Schlüssel-System mit **Präfixen**, um alle Datenmodelle in einer RocksDB zu vereinen:

```cpp
class KeySchema {
public:
    enum class KeyType {
        RELATIONAL,        // Tabellenzeilen
        DOCUMENT,          // Dokumente
        GRAPH_NODE,        // Graph-Knoten
        GRAPH_EDGE,        // Graph-Kanten
        VECTOR,            // Vektor-Objekte
        SECONDARY_INDEX,   // Sekundärindizes
        GRAPH_OUTDEX,      // Outgoing Edges
        GRAPH_INDEX        // Incoming Edges
    };
    
    // Key-Konstruktion
    static std::string makeRelationalKey(std::string_view table, std::string_view pk);
    static std::string makeGraphNodeKey(std::string_view pk);
    static std::string makeSecondaryIndexKey(
        std::string_view table,
        std::string_view column,
        std::string_view value,
        std::string_view pk
    );
};
```

### 2.2 Schlüssel-Formate

| Datenmodell | Schlüsselformat | Beispiel |
|-------------|------------------|----------|
| **Relational** | `entity:table:pk` | `entity:users:alice` |
| **Document** | `entity:collection:pk` | `entity:orders:order_123` |
| **Graph Node** | `entity:node:pk` | `entity:node:user_456` |
| **Graph Edge** | `entity:edge:pk` | `entity:edge:follows_789` |
| **Vector** | `entity:vectors:pk` | `entity:vectors:doc_abc` |
| **Secondary Index** | `idx:table:column:value:pk` | `idx:users:age:30:alice` |
| **Range Index** | `ridx:table:column:value:pk` | `ridx:products:price:99.99:prod_1` |
| **Spatial Index** | `sidx:table:geohash:pk` | `sidx:locations:u33dc1:berlin` |
| **TTL Index** | `ttlidx:table:column:timestamp:pk` | `ttlidx:sessions:created_at:1730000000:sess_1` |
| **Fulltext Index** | `ftidx:table:column:token:pk` | `ftidx:articles:body:search:art_42` |
| **Graph Outdex** | `graph:out:pk_start:pk_edge` | `graph:out:alice:follows_789` |
| **Graph Indeg** | `graph:in:pk_target:pk_edge` | `graph:in:bob:follows_789` |
| **Changefeed** | `changefeed:pk:seqno` | `changefeed:alice:0000000001` |
| **Time-Series** | `ts:metric:entity:timestamp` | `ts:cpu:server1:1730000000000` |

**Separator:** Alle Schlüssel verwenden `:` als Trennzeichen.

### 2.3 Primary Key Extraktion

```cpp
// Aus beliebigem Schlüssel PK extrahieren
std::string pk = KeySchema::extractPrimaryKey("idx:users:age:30:alice");
// Ergebnis: "alice"

KeyType type = KeySchema::parseKeyType("entity:users:alice");
// Ergebnis: KeyType::RELATIONAL
```

**Siehe auch:** [RocksDB Storage Layout](storage/rocksdb_layout.md) für Details zu Key Prefixes und Column Families.

---

## 3. TTL & Retention – Automatisches Ablaufen

### 3.1 TTL-Index-Konzept

THEMIS unterstützt **Time-To-Live (TTL)** für Entitäten über spezielle TTL-Indizes:

```cpp
// TTL-Index erstellen (Sessions laufen nach 3600 Sekunden ab)
idx.createTTLIndex("sessions", "created_at", /*ttl_seconds=*/3600);

// Entity einfügen (TTL wird automatisch berechnet)
BaseEntity session("sess_123");
session.setField("user", "alice");
session.setField("created_at", "2025-10-27T10:00:00Z");
idx.put("sessions", session);
// → TTL-Eintrag: ttlidx:sessions:created_at:1730034000:sess_123

// Periodisch Cleanup aufrufen (z. B. CRON/Timer alle 60s)
auto [status, deletedCount] = idx.cleanupExpiredEntities("sessions", "created_at");
```

### 3.2 Mechanik

**Beim Put:**
1. Aktueller Timestamp: `now = std::chrono::system_clock::now()`
2. Expire-Timestamp: `expire = now + ttl_seconds`
3. TTL-Index-Key: `ttlidx:table:column:{expire}:pk`
4. Atomare WriteBatch: Entity + TTL-Index-Eintrag

**Beim Cleanup:**
1. Scan TTL-Index: `ttlidx:table:column:0` bis `ttlidx:table:column:{current_time}`
2. Für jeden PK: Lösche Entity + alle Indizes (atomar per WriteBatch)
3. Rückgabe: Anzahl gelöschter Entities

### 3.3 Retention für Time-Series

Zusätzlich gibt es eine **Retention Policy** für Time-Series-Daten (TSStore):

```cpp
RetentionPolicy policy;
policy.per_metric["cpu"] = std::chrono::hours(24);      // CPU-Daten 24h
policy.per_metric["logs"] = std::chrono::hours(72);     // Logs 72h

RetentionManager mgr(&tsstore, policy);
size_t deleted = mgr.apply();  // Löscht alte Datenpunkte pro Metrik
```

**Hinweis:** TTL-Indizes sind für Entities (Dokumente, Sessions, Cache), Retention ist für Time-Series (Metriken, Logs).

**Siehe auch:**
- [Index Stats & Maintenance](index_stats_maintenance.md) für TTL-Index-Details
- [Time-Series](time_series.md) für Retention Policies

---

## 4. Verwendungsbeispiele

### 4.1 Entity erstellen & persistieren

```cpp
// Von Field Map erstellen
BaseEntity::FieldMap fields;
fields["name"] = std::string("Alice");
fields["age"] = int64_t(30);
fields["active"] = true;

BaseEntity user = BaseEntity::fromFields("alice", fields);

// Von JSON erstellen
std::string json = R"({"name":"Bob","age":25})";
BaseEntity user2 = BaseEntity::fromJson("bob", json);

// Serialisieren & speichern
std::string key = KeySchema::makeRelationalKey("users", user.getPrimaryKey());
auto blob = user.serialize();
db.put(key, blob);
```

### 4.2 Entity laden & Felder lesen

```cpp
// Von RocksDB laden
auto blob = db.get("entity:users:alice");
BaseEntity user = BaseEntity::deserialize("alice", *blob);

// Felder lesen (typsicher)
auto name = user.getFieldAsString("name");    // std::optional<std::string>
auto age = user.getFieldAsInt("age");         // std::optional<int64_t>
auto active = user.getFieldAsBool("active");  // std::optional<bool>

if (name && age) {
    std::cout << *name << " ist " << *age << " Jahre alt\n";
}

// Alle Felder extrahieren (für Index-Updates)
auto attrs = user.extractAllFields();
for (const auto& [field, value] : attrs) {
    // Index-Einträge erstellen
}
```

### 4.3 Vector-Embeddings

```cpp
// Entity mit Embedding erstellen
std::vector<float> embedding = {0.1f, 0.2f, 0.3f, 0.4f};
BaseEntity doc("doc_123");
doc.setField("title", std::string("Artikel über KI"));
doc.setField("embedding", embedding);

// Embedding für ANN-Index extrahieren
auto vec = doc.extractVector("embedding");
if (vec) {
    hnsw_index.add(*vec, doc.getPrimaryKey());
}
```

### 4.4 Index-Updates (Secondary Index)

```cpp
// Entity + Secondary Index atomar updaten
auto batch = db.createWriteBatch();

// Entity speichern
std::string entityKey = KeySchema::makeRelationalKey("users", "alice");
batch->put(entityKey, user.serialize());

// Secondary Index für age=30
std::string idxKey = KeySchema::makeSecondaryIndexKey("users", "age", "30", "alice");
batch->put(idxKey, "alice");  // PK als Value

batch->commit();  // Atomar!
```

---

## 5. Performance & Implementierung

### 5.1 Lazy Parsing (Field Cache)

- **Field Cache** wird beim ersten Feldzugriff befüllt
- Nachfolgende Zugriffe nutzen Cache (keine erneute Deserialisierung)
- Cache wird invalidiert bei Blob-Modifikation (`setField()`)

```cpp
// Erstes getField() → Parse Blob → Cache füllen
auto name = entity.getField("name");  // Parse!

// Zweites getField() → Cache verwenden
auto age = entity.getField("age");    // Cache Hit!

// setField() → Cache invalidieren
entity.setField("status", std::string("online"));  // Cache invalidiert
```

### 5.2 simdjson Integration (JSON Format)

- **JSON Format** nutzt simdjson on-demand API
- Feld-Extraktion **ohne vollständigen Parse** (O(1))
- Parsing-Throughput: **Multi-GB/s** auf modernen CPUs

```cpp
// JSON-Entity laden (lazy parsing)
BaseEntity entity = BaseEntity::fromJson("alice", json_string);

// Einzelne Felder extrahieren (simdjson on-demand)
auto name = entity.extractField("name");  // Kein vollständiger Parse!
```

### 5.3 Binary Format Effizienz

| Eigenschaft | JSON | Binary |
|-------------|------|--------|
| **Größe** | 100% | ~60% (kompakter) |
| **Parse-Overhead** | Mittel (simdjson) | Minimal (direkter Zugriff) |
| **Vector-Encoding** | Base64/String | Native Float-Array |
| **Lesbarkeit** | Hoch | Niedrig |

**Empfehlung:** Binary für Produktiv-Storage, JSON für Debugging/Export.

### 5.4 Binary Format Struktur

```
<Object>
  <num_fields: uint32>           // Anzahl Felder
  <field_1>
    <name_length: uint32>        // Feldname-Länge
    <name: bytes>                // Feldname (UTF-8)
    <type_tag: uint8>            // Typ (siehe Type Tags)
    <value: varies by type>      // Wert (typ-abhängig)
  <field_2>
    ...
```

**Type Tags:**
```cpp
NULL_VALUE    = 0x00
BOOL_FALSE    = 0x01
BOOL_TRUE     = 0x02
INT64         = 0x11
DOUBLE        = 0x21
STRING        = 0x30
BINARY        = 0x40
VECTOR_FLOAT  = 0x70  // Optimiert für Embeddings (ohne extra Encoding)
```

---

## 6. Best Practices

### 6.1 Storage-Format-Wahl

- **Binary Format**: Immer für RocksDB-Storage (Standard, kompakt, schnell)
- **JSON Format**: Nur für Debugging, Export, HTTP-Responses

```cpp
// Produktiv: Binary
auto blob = entity.serialize();          // Binary (Standard)
db.put(key, blob);

// Debugging: JSON
std::string json = entity.toJson();      // Für Logs
THEMIS_DEBUG("Entity: {}", json);
```

### 6.2 Feld-Extraktion für Index-Updates

```cpp
// Schnell: Einzelne Felder extrahieren (ohne Full Parse)
auto name = entity.extractField("name");
auto age = entity.extractField("age");

// Langsamer: Alle Felder (Full Parse)
auto attrs = entity.extractAllFields();
```

**Regel:** `extractField()` für gezielte Updates, `extractAllFields()` nur wenn alle Felder benötigt.

### 6.3 Batch-Updates (Atomarität)

```cpp
// ✅ RICHTIG: Atomare Updates via WriteBatch
auto batch = db.createWriteBatch();
batch->put(entity_key, entity.serialize());
batch->put(idx_key1, idx_value1);
batch->put(idx_key2, idx_value2);
batch->commit();  // Alles oder nichts!

// ❌ FALSCH: Einzelne Puts (nicht atomar)
db.put(entity_key, entity.serialize());
db.put(idx_key1, idx_value1);  // Fehler hier → Inkonsistenz!
```

### 6.4 Vector-Embeddings optimieren

```cpp
// ✅ RICHTIG: Native float-Vektoren (kein Encoding)
std::vector<float> embedding = model.encode(text);
entity.setField("embedding", embedding);

// ❌ FALSCH: String-Encoding (langsam, groß)
std::string encoded = serializeFloats(embedding);
entity.setField("embedding", encoded);
```

### 6.5 Thread Safety

- **Ein `BaseEntity` pro Thread** (keine Shared Ownership)
- simdjson-Parser ist thread-local (automatisch)
- Bei Shared Access: Locks verwenden

```cpp
// ✅ RICHTIG: Separate Instanzen
std::thread t1([&]() {
    BaseEntity e1 = BaseEntity::deserialize("alice", blob);
});
std::thread t2([&]() {
    BaseEntity e2 = BaseEntity::deserialize("bob", blob);
});

// ❌ FALSCH: Shared Instanz ohne Lock
BaseEntity shared("user");
std::thread t1([&]() { shared.getField("name"); });  // Race Condition!
std::thread t2([&]() { shared.setField("age", 30); });
```

---

## 7. Zukünftige Erweiterungen

- [ ] **Kompression**: LZ4/Snappy für große Blobs
- [ ] **Schema-Validation**: Optionale JSON-Schema-Prüfung
- [ ] **Nested Objects**: Support für verschachtelte Maps/Arrays
- [ ] **Custom Types**: Erweiterbare Type Tags (z. B. Geo-Points, UUIDs)
- [ ] **Memory-Mapped Access**: Für sehr große Entities (>1MB)
- [ ] **Path Constraints**: Graph-Traversal-Constraints (siehe [Path Constraints](path_constraints.md))

---

## Referenzen

- **Implementation:** `include/storage/base_entity.h`
- **Key Schema:** `include/storage/key_schema.h`
- **simdjson:** https://github.com/simdjson/simdjson
- **RocksDB Storage:** [RocksDB Layout](storage/rocksdb_layout.md)
- **TTL-Indizes:** [Index Stats & Maintenance](index_stats_maintenance.md)
- **Retention:** [Time-Series](time_series.md)
- **Path Constraints:** [Graph Traversal Constraints](path_constraints.md)
