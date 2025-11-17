# Transaction Management in THEMIS

**Version:** 1.1  
**Datum:** 2. November 2025

## Überblick

THEMIS bietet ACID-konforme Transaktionen über alle Index-Typen hinweg (Relational, Graph, Vector). Transaktionen basieren auf MVCC mit Snapshot-Isolation und Konflikterkennung; Updates über Sekundär-, Graph- und Vektorindizes erfolgen atomar innerhalb der Transaktion.

### Kernfeatures

- **Atomicity**: Alle Operationen innerhalb einer Transaktion werden atomar ausgeführt (all-or-nothing)
- **MVCC & Isolation**: ReadCommitted (default) und Snapshot-Isolation mit konsistentem Sichtfenster
- **Konflikterkennung**: Write-Write-Konflikte werden beim Commit/Put erkannt und führen zu Fehlern/Abbrüchen
- **Session-Management**: Transaktionen sind über eindeutige Transaction-IDs identifizierbar
- **Statistics Tracking**: Umfassende Metriken (begun, committed, aborted, durations, success rate)
- **Auto-Rollback**: RAII-Pattern für automatisches Rollback bei Exception/Destruktion
- **Multi-Index Support**: Konsistente Updates über Secondary, Graph und Vector-Indizes

---

## Architektur

### TransactionManager

Zentrale Komponente für Session-basiertes Transaction-Management:

```cpp
class TransactionManager {
public:
    // Session-based API (empfohlen für HTTP/Multi-Client)
    TransactionId beginTransaction(IsolationLevel isolation = IsolationLevel::ReadCommitted);
    Status commitTransaction(TransactionId id);
    void rollbackTransaction(TransactionId id);
    std::shared_ptr<Transaction> getTransaction(TransactionId id);
    
    // Direct API (für Single-Threaded/Embedded)
    Transaction begin(IsolationLevel isolation = IsolationLevel::ReadCommitted);
    
    // Statistics
    Stats getStats() const;
    void cleanupOldTransactions(std::chrono::seconds max_age);
};
```

### Transaction Class

Stellt Operationen innerhalb einer Transaktion bereit:

```cpp
class Transaction {
public:
    // Metadata
    TransactionId getId() const;
    IsolationLevel getIsolationLevel() const;
    uint64_t getDurationMs() const;
    
    // Relational Operations
    Status putEntity(std::string_view table, const BaseEntity& entity);
    Status eraseEntity(std::string_view table, std::string_view pk);
    
    // Graph Operations
    Status addEdge(const BaseEntity& edgeEntity);
    Status deleteEdge(std::string_view edgeId);
    
    // Vector Operations
    Status addVector(const BaseEntity& entity, std::string_view vectorField = "embedding");
    Status updateVector(const BaseEntity& entity, std::string_view vectorField = "embedding");
    Status removeVector(std::string_view pk);
    
    // Finalization
    Status commit();
    void rollback();
};
```

### Isolation Levels

#### ReadCommitted (Default)
- Lesezugriffe sehen nur committed data
- Keine Dirty Reads
- Non-Repeatable Reads möglich
- Geeignet für: Standard OLTP-Workloads

#### Snapshot
- Point-in-time Konsistenz (Sichtfenster fixiert beim Begin)
- Repeatable Reads innerhalb der Transaktion
- Höhere Isolation, potenziell höherer Overhead
- Geeignet für: Analytische Queries, konsistente Reports

---

## HTTP REST API

### POST /transaction/begin

Startet eine neue Transaktion.

**Request:**
```json
{
  "isolation": "read_committed"  // optional, default: "read_committed"
}
```

**Response:**
```json
{
  "transaction_id": 42,
  "isolation": "read_committed",
  "status": "begun"
}
```

**cURL Beispiel:**
```bash
curl -X POST http://localhost:8080/transaction/begin \
  -H "Content-Type: application/json" \
  -d '{"isolation": "snapshot"}'
```

---

### POST /transaction/commit

Committed eine Transaktion.

**Request:**
```json
{
  "transaction_id": 42
}
```

**Response (Success):**
```json
{
  "transaction_id": 42,
  "status": "committed",
  "message": "Transaction committed successfully"
}
```

**Response (Error):**
```json
{
  "transaction_id": 42,
  "status": "error",
  "error": "Transaction not found or already completed"
}
```

**cURL Beispiel:**
```bash
curl -X POST http://localhost:8080/transaction/commit \
  -H "Content-Type: application/json" \
  -d '{"transaction_id": 42}'
```

---

### POST /transaction/rollback

Rollt eine Transaktion zurück.

**Request:**
```json
{
  "transaction_id": 42
}
```

**Response:**
```json
{
  "transaction_id": 42,
  "status": "rolled_back",
  "message": "Transaction rolled back successfully"
}
```

**cURL Beispiel:**
```bash
curl -X POST http://localhost:8080/transaction/rollback \
  -H "Content-Type: application/json" \
  -d '{"transaction_id": 42}'
```

---

### GET /transaction/stats

Liefert Statistiken über alle Transaktionen.

**Response:**
```json
{
  "total_begun": 1523,
  "total_committed": 1401,
  "total_aborted": 122,
  "active_count": 3,
  "avg_duration_ms": 45,
  "max_duration_ms": 523,
  "success_rate": 0.92
}
```

**cURL Beispiel:**
```bash
curl http://localhost:8080/transaction/stats
```

---

## Use Cases & Best Practices

### 1. Atomares Multi-Entity Update

**Problem:** Mehrere Entities mit verschiedenen Indizes müssen atomar gespeichert werden.

**Lösung:**
```bash
# Begin transaction
TXN_ID=$(curl -s -X POST http://localhost:8080/transaction/begin | jq -r '.transaction_id')

# Insert entities via transaction (hypothetisch - API-Erweiterung erforderlich)
# Aktuell: Direkte C++ API verwenden

# C++ Code:
auto txn_id = tx_manager->beginTransaction();
auto txn = tx_manager->getTransaction(txn_id);

BaseEntity user("user123");
user.setField("name", std::string("Alice"));
user.setField("city", std::string("Berlin"));
txn->putEntity("users", user);

BaseEntity order("order456");
order.setField("user_id", std::string("user123"));
order.setField("total", 99.99);
txn->putEntity("orders", order);

// Commit via HTTP
curl -X POST http://localhost:8080/transaction/commit \
  -d "{\"transaction_id\": $TXN_ID}"
```

---

### 2. Graph-Operationen mit Rollback

**Problem:** Mehrere Graph-Edges sollen atomar hinzugefügt werden, bei Fehler Rollback.

**C++ Beispiel:**
```cpp
auto txn_id = tx_manager->beginTransaction();
auto txn = tx_manager->getTransaction(txn_id);

try {
    // Add multiple edges
    BaseEntity edge1("edge1");
    edge1.setField("_from", std::string("user1"));
    edge1.setField("_to", std::string("user2"));
    edge1.setField("type", std::string("follows"));
    txn->addEdge(edge1);
    
    BaseEntity edge2("edge2");
    edge2.setField("_from", std::string("user1"));
    edge2.setField("_to", std::string("user3"));
    edge2.setField("type", std::string("follows"));
    txn->addEdge(edge2);
    
    // Validate business logic
    if (!validateFollowerLimit(txn)) {
        tx_manager->rollbackTransaction(txn_id);
        return Status::Error("Follower limit exceeded");
    }
    
    // Commit if all OK
    return tx_manager->commitTransaction(txn_id);
} catch (...) {
    tx_manager->rollbackTransaction(txn_id);
    throw;
}
```

---

### 3. Vector-Index mit Transaktionen

**Problem:** Vector-Embedding und Metadaten sollen atomar gespeichert werden.

**C++ Beispiel:**
```cpp
auto txn_id = tx_manager->beginTransaction();
auto txn = tx_manager->getTransaction(txn_id);

// Document with embedding
BaseEntity doc("doc123");
doc.setField("title", std::string("AI Research Paper"));
doc.setField("author", std::string("Dr. Smith"));
std::vector<float> embedding = {0.23f, 0.45f, 0.67f, /* ... 768 dims */};
doc.setField("embedding", embedding);

// Store entity (relational)
auto status = txn->putEntity("documents", doc);
if (!status.ok) {
    tx_manager->rollbackTransaction(txn_id);
    return status;
}

// Store vector (vector index)
status = txn->addVector(doc, "embedding");
if (!status.ok) {
    tx_manager->rollbackTransaction(txn_id);
    return status;
}

// Commit both atomically
return tx_manager->commitTransaction(txn_id);
```

---

### 4. Long-Running Transactions & Cleanup

**Problem:** Vergessene/abgestürzte Clients hinterlassen offene Transaktionen.

**Best Practice:**
```cpp
// Periodischer Cleanup (z.B. via Cronjob oder Background-Thread)
tx_manager->cleanupOldTransactions(std::chrono::hours(1));

// Metriken überwachen
auto stats = tx_manager->getStats();
if (stats.active_count > 100) {
    THEMIS_WARN("High number of active transactions: {}", stats.active_count);
}
```

**HTTP Monitoring:**
```bash
# Statistiken abrufen
curl http://localhost:8080/transaction/stats | jq '.active_count'

# Alert bei hoher Anzahl
if [ $(curl -s http://localhost:8080/transaction/stats | jq '.active_count') -gt 50 ]; then
  echo "WARNING: Too many active transactions"
fi
```

---

## Performance-Überlegungen

### MVCC Overhead

- Snapshot-Verwaltung und Konflikterkennung erzeugen geringen Overhead gegenüber einfachen Writes
- Benefit: Korrektheit unter Parallelität (kein Last-Write-Wins), konsistente Sicht

### Isolation Level Trade-offs

| Isolation Level | Read Overhead | Write Overhead | Use Case |
|----------------|---------------|----------------|----------|
| ReadCommitted  | Minimal       | Minimal        | OLTP, Standard-Queries |
| Snapshot       | +5-10%        | +5-10%         | Reports, Analytics |

### Index-spezifische Kosten

- **Secondary Index**: ~0.05ms pro Index-Entry (Put/Delete)
- **Graph Index**: ~0.1ms pro Edge (2x Index-Entries: out + in)
- **Vector Index**: ~0.5-2ms (abhängig von HNSW-Parametern, Dimension)

---

## Bekannte Einschränkungen

### 1. Vector Index In-Memory Cache

**Problem:**  
Der Vector-Index hält einen In-Memory Cache (HNSW-Struktur, `cache_` map). Bei Rollback werden RocksDB-Änderungen rückgängig gemacht, aber der Cache bleibt inkonsistent.

**Auswirkung:**
- Nach Rollback können Vector-Searches falsch-positive Ergebnisse liefern
- Cache enthält Vektoren, die in RocksDB nicht existieren

**Workaround:**
```cpp
// Nach Rollback: Vector-Index neu laden
vector_index->rebuildFromStorage();
```

**Zukünftige Verbesserung:**  
Callback-Mechanismus für commit/rollback, um Cache synchron zu halten.

---

### 2. Konflikte unter Parallelität

**Verhalten:**  
Write-Write-Konflikte werden erkannt und führen zu Fehlern beim Commit/Put. Clients sollten Retry-Logik mit Backoff implementieren, wenn eine Transaktion aufgrund eines Konflikts abgelehnt wird.

---

### 3. Transaction Timeout

**Limitation:**  
Keine automatischen Timeouts für aktive Transaktionen implementiert.

**Empfehlung:**
```cpp
// Client-seitig: Timeout überwachen
auto start = std::chrono::system_clock::now();
auto txn_id = tx_manager->beginTransaction();

// ... operations ...

auto duration = std::chrono::system_clock::now() - start;
if (duration > std::chrono::seconds(30)) {
    tx_manager->rollbackTransaction(txn_id);
    return Status::Error("Transaction timeout");
}
```

---

## Fehlerbehandlung

### Commit Failures

```cpp
auto status = tx_manager->commitTransaction(txn_id);
if (!status.ok) {
    // Mögliche Ursachen:
    // - Transaction nicht gefunden
    // - Bereits committed/rolled back
    // - RocksDB Write-Fehler (Disk-Full, Permissions)
    
    THEMIS_ERROR("Commit failed: {}", status.message);
    
    // Cleanup
    tx_manager->rollbackTransaction(txn_id);  // Safe auch bei bereits abgeschlossener Txn
}
```

### Network Failures (HTTP)

```bash
# Retry-Logic mit Exponential Backoff
for i in {1..3}; do
  if curl -X POST http://localhost:8080/transaction/commit \
       -d "{\"transaction_id\": $TXN_ID}" \
       --max-time 5; then
    break
  fi
  sleep $((2**i))
done
```

### Auto-Rollback bei Exception

```cpp
{
    auto txn = tx_manager->begin();  // Direct API
    
    txn.putEntity("users", user);
    
    // Exception thrown -> Destruktor ruft automatisch rollback()
    throw std::runtime_error("Business logic error");
    
}  // txn Destruktor: THEMIS_WARN + rollback()
```

---

## Metriken & Monitoring

### Prometheus-Integration

Transaction-Statistiken sind via `/metrics` Endpoint verfügbar:

```
# TYPE vccdb_transactions_begun_total counter
vccdb_transactions_begun_total 1523

# TYPE vccdb_transactions_committed_total counter
vccdb_transactions_committed_total 1401

# TYPE vccdb_transactions_aborted_total counter
vccdb_transactions_aborted_total 122

# TYPE vccdb_transactions_active gauge
vccdb_transactions_active 3

# TYPE vccdb_transaction_duration_ms histogram
vccdb_transaction_duration_ms_bucket{le="10"} 834
vccdb_transaction_duration_ms_bucket{le="50"} 1245
vccdb_transaction_duration_ms_bucket{le="100"} 1398
vccdb_transaction_duration_ms_bucket{le="+Inf"} 1523
vccdb_transaction_duration_ms_sum 68035
vccdb_transaction_duration_ms_count 1523
```

### Grafana Dashboard

Empfohlene Metriken:
- **Transaction Rate**: `rate(vccdb_transactions_begun_total[5m])`
- **Success Rate**: `vccdb_transactions_committed_total / vccdb_transactions_begun_total`
- **Active Transactions**: `vccdb_transactions_active`
- **Avg Duration**: `vccdb_transaction_duration_ms_sum / vccdb_transaction_duration_ms_count`

---

## Migrationshinweise

### Von Direct API zu Session-based API

**Vorher (Direct):**
```cpp
auto txn = tx_manager->begin();
txn.putEntity("users", user);
txn.commit();
```

**Nachher (Session-based, HTTP-kompatibel):**
```cpp
auto txn_id = tx_manager->beginTransaction();
auto txn = tx_manager->getTransaction(txn_id);
txn->putEntity("users", user);
tx_manager->commitTransaction(txn_id);
```

**Vorteil:** Transaction-ID kann über HTTP/Network übertragen werden.

---

## Weiterführende Dokumentation

- [Architecture Overview](architecture.md) - Systemarchitektur
- [MVCC Design](mvcc_design.md) - Hintergründe & Optionen
- [RocksDB Storage](storage/rocksdb_layout.md) - WAL/Snapshots/Compaction
- [Deployment Guide](deployment.md) - Production Setup
- [OpenAPI Specification](openapi.yaml) - Vollständige API-Referenz
- [Base Entity Documentation](base_entity.md) - Entity-Serialisierung

---

## Changelog

### Version 1.0 (28. Oktober 2025)
- Initial release
- Session-based Transaction Management
- Isolation Levels: ReadCommitted, Snapshot
- Multi-Index Support: Secondary, Graph, Vector
- HTTP REST API: begin, commit, rollback, stats
- Comprehensive Statistics & Monitoring
- 23 Unit-Tests (100% Pass-Rate)
