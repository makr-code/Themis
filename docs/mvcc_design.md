# MVCC Design für THEMIS

## ✅ IMPLEMENTIERUNGSSTATUS: PRODUKTIONSREIF

**Stand: 28. Oktober 2025**

MVCC ist vollständig implementiert und getestet mit **RocksDB TransactionDB** (Option 1).

### Test-Resultate
- **Transaction Tests**: 27/27 PASS (100%)
- **MVCC Tests**: 12/12 PASS (100%)
- **Performance**: Minimal Overhead gegenüber WriteBatch
  - SingleEntity: MVCC ~3.4k/s vs WriteBatch ~3.1k/s
  - Batch 100: WriteBatch ~27.8k/s
  - Rollback: MVCC ~35.3k/s
  - Snapshot Reads: ~44k/s

---

## Übersicht

MVCC (Multi-Version Concurrency Control) ermöglicht parallele Transaktionen ohne Locks durch Versionierung aller Daten.

## Implementierte Lösung: RocksDB TransactionDB

THEMIS nutzt RocksDB's native TransactionDB mit Pessimistic Locking:

- **Snapshot Isolation**: Automatisch via `set_snapshot=true`
- **Conflict Detection**: Write-Write Konflikte werden bei `put()` erkannt
- **Lock Timeouts**: Konfigurierbar (Standard: 1000ms)
- **ACID Garantien**: Vollständig durch RocksDB implementiert

### Architektur

```
┌─────────────────────────────────────────────┐
│         TransactionManager                   │
│  (High-Level Transaction API)                │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│      RocksDBWrapper::TransactionWrapper     │
│  • get(key) - snapshot reads                │
│  • put(key, value) - conflict detection     │
│  • del(key) - transactional deletes         │
│  • commit() - atomic persistence            │
│  • rollback() - automatic cleanup           │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│          RocksDB TransactionDB              │
│  • Pessimistic Locking                      │
│  • Snapshot Isolation                       │
│  • Write-Write Conflict Detection           │
└─────────────────────────────────────────────┘
```

### Indexe mit MVCC

Alle Index-Manager unterstützen MVCC-Transaktionen:

- **SecondaryIndexManager**: Equality, Range, Sparse, Geo, TTL, Fulltext
- **GraphIndexManager**: Kanten und Adjazenz-Indizes
- **VectorIndexManager**: HNSW + Cache-Updates

Alle Index-Operationen sind **atomar** mit der Haupttransaktion - Rollback entfernt alle Änderungen vollständig.

---

## Aktuelle Situation vs. MVCC

### Vor MVCC (SAGA Pattern)
- ✅ Eventual Consistency durch Compensating Actions
- ✅ Vector Cache Consistency
- ❌ Last-Write-Wins bei konkurrierenden Writes
- ❌ Keine Snapshot Isolation
- ❌ Write-Write Conflicts werden nicht erkannt

### Mit MVCC (Implementiert)
- ✅ Vollständige Snapshot Isolation
- ✅ Write-Write Conflict Detection
- ✅ Concurrent Reads blockieren nie
- ✅ Atomare Rollbacks (inkl. Indizes)
- ✅ SAGA Pattern für Vector Cache (hybride Lösung)
- ⚠️  Höherer Speicherverbrauch durch RocksDB Locks
- ⚠️  Kein Point-in-Time Recovery (RocksDB Limitation)
---

## Design-Optionen (Archiv)

Die ursprünglich evaluierten Optionen sind unten dokumentiert. **Option 1 (RocksDB TransactionDB)** wurde implementiert.

## Kernkomponenten für MVCC (Option 1 - Implementiert)

### 1. RocksDB TransactionDB Konfiguration

```cpp
// In RocksDBWrapper::open()
rocksdb::TransactionDBOptions txn_db_options;
txn_db_options.transaction_lock_timeout = 1000;      // 1s Lock Timeout
txn_db_options.default_lock_timeout = 1000;          // 1s für alle Locks

rocksdb::TransactionOptions txn_options;
txn_options.set_snapshot = true;                     // Automatisches Snapshot
txn_options.deadlock_detect = true;                  // Deadlock Prevention

rocksdb::TransactionDB* txn_db;
rocksdb::TransactionDB::Open(options, txn_db_options, db_path, &txn_db);
```

### 2. TransactionWrapper API

```cpp
class TransactionWrapper {
public:
    // Reads (mit Snapshot Isolation)
    std::optional<std::vector<uint8_t>> get(const std::string& key);
    
    // Writes (mit Conflict Detection)
    bool put(const std::string& key, const std::vector<uint8_t>& value);
    bool del(const std::string& key);
    
    // Commit/Rollback
    bool commit();     // false = Conflict detected
    void rollback();   // Immer erfolgreich
    
    // Snapshot Management
    const rocksdb::Snapshot* getSnapshot() const;
    bool isActive() const;
};
```

### 3. Conflict Detection Flow

```
Thread A                    Thread B
────────────────────────────────────────────
txn_a = begin()
  snapshot_a = get_snapshot()
                              txn_b = begin()
                                snapshot_b = get_snapshot()
                              
put("user:1", data_a)       put("user:1", data_b)
  ✅ Lock acquired            ⏳ Waiting for lock...
  
commit()                    
  ✅ Lock released
  ✅ Committed
                              ❌ Conflict detected!
                              ❌ Abort & Rollback
```

### 4. Index Integration

Alle Index-Operationen verwenden dieselbe MVCC-Transaktion:

```cpp
// In TransactionManager::Transaction::putEntity()
auto txn = mvcc_txn_;  // Shared MVCC transaction

// Primary data
txn->put(entityKey, entityData);

// Secondary indexes (atomisch mit primary data)
secIdx_.put(table, entity, *txn);  // MVCC variant

// Graph indexes (atomisch)
graphIdx_.addEdge(edge, *txn);     // MVCC variant

// Vector indexes (atomisch)
vecIdx_.addEntity(entity, *txn);   // MVCC variant

// Commit alles zusammen
txn->commit();  // Alles oder nichts
```

---

## Originales Design (Archiv - Nicht implementiert)

### Option 2/3: Custom Version Management

Die folgenden Designs wurden evaluiert aber nicht gewählt:
    
    pending_writes_[pk] = new_version;
    return Status::OK();
}
```

### 4. Commit-Protokoll

```cpp
Status Transaction::commit() {
    // 1. Atomare Version-Nummer holen
    uint64_t commit_version = global_version_counter_.fetch_add(1);
    
    // 2. Write-Write Conflicts prüfen
    for (auto& [pk, new_version] : pending_writes_) {
        auto latest = db_.getLatestVersion(pk);
        if (latest && latest->version_start > begin_version_) {
            rollback();
            return Status::Error("Serialization failure - retry transaction");
        }
    }
    
    // 3. Alte Versionen "abschließen"
    WriteBatch batch;
    for (auto& [pk, new_version] : pending_writes_) {
        auto old_version = db_.getLatestVersion(pk);
        if (old_version) {
            // Alte Version: version_end = commit_version
            old_version->version_end = commit_version;
            batch.put(makeVersionKey(pk, old_version->version_start), 
                     serialize(*old_version));
        }
        
        // Neue Version: version_start = commit_version
        new_version.version_start = commit_version;
        batch.put(makeVersionKey(pk, commit_version), 
                 serialize(new_version));
    }
    
    // 4. Atomarer Commit
    return batch.commit();
}
```

### 5. Garbage Collection

```cpp
class MVCCGarbageCollector {
    // Älteste aktive Transaktion finden
    uint64_t getOldestActiveTransaction() {
        std::lock_guard lock(txn_mutex_);
        uint64_t min_version = UINT64_MAX;
        for (auto& [txn_id, txn] : active_transactions_) {
            min_version = std::min(min_version, txn->begin_version);
        }
        return min_version;
    }
    
    // Alte Versionen löschen
    void collectGarbage() {
        uint64_t gc_horizon = getOldestActiveTransaction();
        
        // Alle Versionen mit version_end < gc_horizon können gelöscht werden
        for (auto& [pk, versions] : version_map_) {
            versions.erase(
                std::remove_if(versions.begin(), versions.end(),
                    [gc_horizon](const VersionedEntity& v) {
                        return v.version_end < gc_horizon;
                    }),
                versions.end()
            );
        }
    }
};
```

## RocksDB Integration

### Option 1: RocksDB TransactionDB (Empfohlen)

```cpp
#include <rocksdb/utilities/transaction_db.h>

class MVCCWrapper {
    rocksdb::TransactionDB* txn_db_;
    
    // RocksDB TransactionDB bietet:
    // - Built-in MVCC
    // - Optimistic/Pessimistic Concurrency Control
    // - Snapshot Isolation
    // - Conflict Detection
};
```

**Vorteile:**
- ✅ MVCC out-of-the-box
- ✅ Battle-tested Implementation
- ✅ Optimistic & Pessimistic Locking
- ✅ Snapshot Management

**Nachteile:**
- ❌ Breaking Change (RocksDB::DB → TransactionDB)
- ❌ Performance Overhead (~10-20%)

### Option 2: Manuelle MVCC-Implementierung

Eigene Versionsverwaltung über RocksDB Keys:

```cpp
// Key-Format: entity:{table}:{pk}:v{version}
// Beispiel:   entity:users:user_123:v0000000000000042

class ManualMVCC {
    // Version-Range Scan
    std::vector<VersionedEntity> getAllVersions(const std::string& pk) {
        std::string prefix = "entity:" + table + ":" + pk + ":v";
        std::vector<VersionedEntity> versions;
        db_.scanPrefix(prefix, [&](auto key, auto value) {
            versions.push_back(deserialize(value));
            return true;
        });
        return versions;
    }
};
```

**Vorteile:**
- ✅ Volle Kontrolle
- ✅ Keine Breaking Changes
- ✅ Optimierbar für spezifische Workloads

**Nachteile:**
- ❌ Komplex zu implementieren
- ❌ Mehr Fehlerquellen
- ❌ GC muss selbst implementiert werden

## Index-Anpassungen

### Secondary Index mit MVCC

```cpp
// Aktuell: idx:users:age:25 -> ["user_123", "user_456"]
// MVCC:    idx:users:age:25:v42 -> ["user_123:v42", "user_456:v39"]

class MVCCSecondaryIndex {
    Status addToIndex(const BaseEntity& entity, uint64_t version) {
        // Index-Entry muss auch versioniert sein
        std::string idx_key = makeIndexKey(field, value, version);
        std::string pk_with_version = entity.getPrimaryKey() + ":v" + 
                                      std::to_string(version);
        // ...
    }
};
```

### Vector Index mit MVCC

```cpp
class MVCCVectorIndex {
    struct VersionedVector {
        std::string pk;
        uint64_t version;
        std::vector<float> embedding;
    };
    
    // HNSW Index: Nur aktuelle Versionen
    std::unordered_map<uint64_t, std::vector<VersionedVector>> version_snapshots_;
    
    std::vector<SearchResult> search(
        const std::vector<float>& query,
        uint64_t snapshot_version,
        size_t k
    ) {
        // Filter: Nur Vektoren die in snapshot_version sichtbar sind
        auto visible_vectors = getVisibleVectors(snapshot_version);
        return hnsw_index_.search(query, k, visible_vectors);
    }
};
```

### Graph Index mit MVCC

```cpp
class MVCCGraphIndex {
    // Kanten versionieren
    struct VersionedEdge {
        std::string edge_id;
        std::string from_node;
        std::string to_node;
        uint64_t version_start;
        uint64_t version_end;
    };
    
    // Graph-Traversierung mit Snapshot
    std::vector<std::string> traverse(
        const std::string& start_node,
        uint64_t snapshot_version
    ) {
        // Nur Kanten verwenden, die in snapshot_version sichtbar sind
        auto visible_edges = getVisibleEdges(snapshot_version);
        return bfs(start_node, visible_edges);
    }
};
```

## Migration Plan

### Phase 1: Foundation (2-3 Wochen)
1. **RocksDB TransactionDB Migration**
   - `RocksDBWrapper` zu `TransactionDB` migrieren
   - Snapshot-Management implementieren
   - Bestehende Tests anpassen

2. **TransactionManager Refactoring**
   - `begin()` gibt `rocksdb::Transaction*` zurück
   - Snapshot beim `begin()` erstellen
   - Commit/Rollback über TransactionDB

### Phase 2: Index Integration (2-3 Wochen)
3. **Secondary Index MVCC**
   - Index-Einträge versionieren
   - Range-Queries mit Snapshot
   - Visibility-Filter

4. **Vector Index MVCC**
   - Version-aware HNSW
   - Snapshot-basierte Suche
   - In-Memory Cache pro Version

5. **Graph Index MVCC**
   - Versionierte Kanten
   - Snapshot-Traversierung
   - Temporal Graph Queries

### Phase 3: Advanced Features (1-2 Wochen)
6. **Garbage Collection**
   - Background Thread
   - Version-Pruning
   - Configurable Retention

7. **Performance Optimization**
   - Version-Caching
   - Optimistic Locking
   - Batch Conflict Detection

### Phase 4: Testing & Documentation (1 Woche)
8. **Test Suite**
   - Concurrent Transaction Tests
   - Conflict Detection Tests
   - Snapshot Isolation Tests

9. **Documentation**
   - MVCC Architecture Guide
   - Migration Guide
   - Performance Tuning Guide

## Benötigte Änderungen

### Dateien zu erstellen:
- `include/transaction/mvcc_manager.h` - MVCC Coordination
- `src/transaction/mvcc_manager.cpp`
- `include/storage/versioned_entity.h` - Version-aware Entity
- `src/storage/versioned_entity.cpp`
- `include/transaction/garbage_collector.h` - GC
- `src/transaction/garbage_collector.cpp`

### Dateien zu ändern:
- `include/storage/rocksdb_wrapper.h` - TransactionDB statt DB
- `src/storage/rocksdb_wrapper.cpp`
- `include/transaction/transaction_manager.h` - Snapshot Support
- `src/transaction/transaction_manager.cpp`
- `include/index/secondary_index.h` - Versionierte Indizes
- `src/index/secondary_index.cpp`
- `include/index/vector_index.h` - Snapshot-aware Search
- `src/index/vector_index.cpp`
- `include/index/graph_index.h` - Temporal Graphs
- `src/index/graph_index.cpp`

### Dependencies:
```json
// vcpkg.json
{
  "dependencies": [
    "rocksdb[core,lz4,zlib,zstd,transactions]"  // +transactions feature
  ]
}
```

## Performance-Überlegungen

### Speicher-Overhead
- **Pro Version**: ~100 Bytes Metadata + Entity-Größe
- **Beispiel**: 1M Entities, 10 Versionen = ~1GB zusätzlicher Speicher
- **Mitigation**: Aggressive GC, Configurable Retention

### Write Amplification
- **Aktuell**: 1 Write = 1 RocksDB Put
- **MVCC**: 1 Write = 2 Puts (alte Version update + neue Version insert)
- **Mitigation**: RocksDB Compaction optimieren

### Read Performance
- **Snapshot Reads**: +5-10% Overhead (Version-Check)
- **Range Queries**: +10-20% Overhead (Visibility-Filter)
- **Mitigation**: Version-Cache, Bloom Filters

## Alternativen

### 1. Hybrid: SAGA + Optimistic Locking
- SAGA Pattern behalten
- Nur Write-Write Conflict Detection hinzufügen
- Kein vollständiges MVCC
- **Aufwand**: 1 Woche, **Benefit**: 70% von MVCC

### 2. PostgreSQL-Style MVCC
- Tuple-Versionierung in-place
- Vacuum statt GC
- **Aufwand**: 4-6 Wochen, **Benefit**: Bessere Performance

### 3. RocksDB OptimisticTransactionDB
- Leichtgewichtiger als TransactionDB
- Nur Conflict Detection, kein Locking
- **Aufwand**: 2-3 Wochen, **Benefit**: 80% von MVCC

## Empfehlung

**Start: Hybrid-Ansatz (SAGA + Optimistic Locking)**

1. **Kurzfristig** (1-2 Wochen):
   - Write-Write Conflict Detection zu SAGA hinzufügen
   - Version-Counter in TransactionManager
   - Conflict-Check vor Commit
   - **Ergebnis**: 70% MVCC-Benefit, minimaler Aufwand

2. **Mittelfristig** (1-2 Monate):
   - Migration zu RocksDB OptimisticTransactionDB
   - Snapshot Isolation implementieren
   - Index-Versionierung
   - **Ergebnis**: Vollständiges MVCC

3. **Langfristig** (3-6 Monate):
   - Garbage Collection optimieren
   - Temporal Queries (Time-Travel)
   - Performance Tuning
   - **Ergebnis**: Production-ready MVCC

## Nächste Schritte

1. **Proof of Concept**: RocksDB TransactionDB Test (1 Tag)
2. **Benchmark**: SAGA vs. Optimistic vs. Full MVCC (2 Tage)
3. **Entscheidung**: Hybrid oder Full MVCC
4. **Implementation**: Nach gewähltem Ansatz

## Ressourcen

- [RocksDB Transactions Wiki](https://github.com/facebook/rocksdb/wiki/Transactions)
- [PostgreSQL MVCC Internals](https://www.postgresql.org/docs/current/mvcc.html)
- [Cockroach MVCC Design](https://www.cockroachlabs.com/docs/stable/architecture/transaction-layer.html)
- [Percolator Paper](https://research.google/pubs/pub36726/) - Google's MVCC System
