# THEMIS Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         HTTP/REST API                            │
│                      (Boost.Beast - Port 8765)                   │
└───────────────────────┬─────────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        │               │               │
        ▼               ▼               ▼
┌──────────────┐ ┌────────────┐ ┌─────────────┐
│   Entity     │ │   Query    │ │   Index     │
│   Manager    │ │   Engine   │ │   Manager   │
└──────┬───────┘ └─────┬──────┘ └──────┬──────┘
       │               │                │
       │        ┌──────┴──────┐        │
       │        │             │        │
       ▼        ▼             ▼        ▼
┌──────────────────────────────────────────────────────────────┐
│                     Index Projections                         │
│  ┌────────────┐ ┌───────────┐ ┌──────────┐ ┌──────────────┐ │
│  │ Secondary  │ │   Graph   │ │  Vector  │ │   Spatial    │ │
│  │   Index    │ │   Index   │ │  Index   │ │    Index     │ │
│  │ (Equality, │ │ (Outdex/  │ │ (HNSW/   │ │ (Geo, R*Tree)│ │
│  │  Range,    │ │  Indeg)   │ │  Faiss)  │ │              │ │
│  │ Composite, │ │           │ │          │ │              │ │
│  │ Fulltext)  │ │           │ │          │ │              │ │
│  └────────────┘ └───────────┘ └──────────┘ └──────────────┘ │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────────┐
│                   Base Entity Layer                           │
│               (Canonical Storage Format)                      │
│                                                               │
│  Key Schema: table:primary_key                                │
│  Value: JSON blob (simdjson deserialization)                  │
│  Metadata: version, timestamp, blob_size                      │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────────┐
│                    RocksDB LSM-Tree                           │
│                                                               │
│  • Write Buffer: 256 MB memtable                              │
│  • Block Cache: 1 GB (LRU)                                    │
│  • Compression: LZ4 (L0-L5), ZSTD (L6 bottommost)             │
│  • Compaction: Level-based (7 levels)                         │
│  • Bloom Filters: 10 bits per key                             │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼
                  ┌─────────┐
                  │  Disk   │
                  │ Storage │
                  └─────────┘
```

## Data Flow

### Write Path (PUT /entities/table:pk)

```
Client Request
     │
     ├──> 1. HTTP Handler (http_server.cpp)
     │
     ├──> 2. Deserialize JSON blob
     │         └─> Extract indexed fields (_from, _to, columns)
     │
     ├──> 3. Base Entity Layer (base_entity.cpp)
     │         └─> Serialize to RocksDB format (key: table:pk)
     │
     ├──> 4. Index Updates (parallel with TBB)
     │         ├─> Secondary Indexes (equality, range, composite)
     │         ├─> Graph Indexes (outdex/indeg if _from/_to present)
     │         └─> Vector Indexes (if embedding present)
     │
     └──> 5. RocksDB Write
               ├─> Write to memtable (in-memory)
               ├─> Write to WAL (durability)
               └─> Response to client (async)
```

### Read Path (POST /query)

```
Client Query
     │
     ├──> 1. Query Parser (query_parser.cpp)
     │         └─> Parse predicates, range, order_by
     │
     ├──> 2. Query Optimizer (query_optimizer.cpp)
     │         ├─> Index selection (selectivity analysis)
     │         ├─> Predicate reordering (most selective first)
     │         └─> Execution plan generation
     │
     ├──> 3. Query Executor (query_engine.cpp)
     │         ├─> Parallel index scans (TBB task_group)
     │         │    └─> For each predicate: index.get(table, column, value)
     │         │
     │         ├─> Intersection of candidate sets (sorted merge)
     │         │    └─> Early termination on empty intermediate results
     │         │
     │         └─> Parallel entity loading (batch processing)
     │              ├─> Batch size: 50 entities
     │              ├─> Threshold: 100 entities (parallelization overhead)
     │              └─> TBB task_group for concurrent RocksDB gets
     │
     └──> 4. Result Serialization
               ├─> return: "keys" → JSON array of primary keys
               └─> return: "entities" → JSON array of blob contents
```

### Index Rebuild Flow (POST /index/rebuild)

```
Rebuild Request
     │
     ├──> 1. Drop existing index keys (range delete in RocksDB)
     │
     ├──> 2. Scan all entities in table (prefix scan: table:*)
     │
     ├──> 3. Parallel reindexing (batch processing)
     │         ├─> Batch size: 1000 entities
     │         ├─> For each batch:
     │         │    ├─> Deserialize entity
     │         │    ├─> Extract indexed field value
     │         │    └─> Write index entry (index:table:column:value -> pk)
     │         │
     │         └─> TBB parallel_for across batches
     │
     └──> 4. Update metrics
               ├─> rebuild_count++
               ├─> rebuild_duration_ms
               └─> rebuild_entities_processed
```

## Thread Model

### HTTP Server (Boost.Beast)

- **I/O Threads**: 8 threads (configurable)
- **Accept Loop**: Async accept on main thread
- **Request Handling**: Each connection handled by worker thread
- **Connection Pool**: Reused connections (keep-alive)

### Query Engine (Intel TBB)

- **Task Scheduling**: Work-stealing scheduler (automatic load balancing)
- **Index Scans**: `tbb::task_group` for parallel predicate evaluation
- **Entity Loading**: Batch-based parallelization (threshold: 100 entities)
  ```cpp
  std::vector<std::vector<BaseEntity>> batches;
  tbb::task_group tg;
  for (size_t batch_idx = 0; batch_idx < num_batches; ++batch_idx) {
      tg.run([&, batch_idx]() {
          // Load entities from RocksDB (batch_size = 50)
          // Deserialize JSON blobs
      });
  }
  tg.wait(); // Barrier
  // Merge results
  ```
- **Parallelization Benefit**: Up to 3.5x speedup on 8-core systems

### RocksDB Internal Threads

- **Flush Threads**: 2 (memtable → SST files)
- **Compaction Threads**: 4 (LSM-Tree level compaction)
- **WAL Sync**: Background thread (fsync batching)

## Memory Hierarchy

```
┌─────────────────────────────────────────────────────────────┐
│  L1: TBB Task Scheduler (per-thread allocation)             │
│      - Lock-free task queues                                 │
│      - Work-stealing deques                                  │
└─────────────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────────────┐
│  L2: RocksDB Memtable (256 MB)                               │
│      - SkipList structure (sorted by key)                    │
│      - Write-ahead Log (WAL) for durability                  │
└─────────────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────────────┐
│  L3: Block Cache (1 GB LRU)                                  │
│      - Decompressed SST blocks                               │
│      - Index/filter blocks (pinned)                          │
│      - Bloom filters (10 bits/key)                           │
└─────────────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────────────┐
│  L4: Operating System Page Cache                             │
│      - Memory-mapped SST files                               │
│      - Kernel read-ahead                                     │
└─────────────────────────────────────────────────────────────┘
                        │
┌─────────────────────────────────────────────────────────────┐
│  L5: Disk Storage (SSD/NVMe)                                 │
│      - SST files (2-64 MB per file)                          │
│      - 7 levels (L0-L6)                                      │
│      - LZ4 compression (L0-L5), ZSTD (L6)                    │
└─────────────────────────────────────────────────────────────┘
```

**Memory Budget (Typical Configuration):**
- Memtable: 256 MB
- Block Cache: 1024 MB
- TBB Scheduler: ~50 MB (8 threads)
- HTTP Buffers: ~32 MB (8 connections × 4 MB)
- **Total**: ~1.36 GB RAM

See [memory_tuning.md](memory_tuning.md) for tuning guidelines.

## Index Key Schemas

### Secondary Index (Equality)

```
Format: index:table:column:value -> primary_key
Example: index:users:city:Berlin -> alice,bob,charlie
```

### Range Index

```
Format: range:table:column:value -> primary_key
Example: range:products:price:00000999 -> p1,p2
Note: Values are zero-padded for lexicographic ordering
```

### Graph Index (Outdex)

```
Format: outdeg:from_vertex -> to_vertex1,to_vertex2,...
Example: outdeg:user:alice -> user:bob,user:charlie
```

### Graph Index (Indeg)

```
Format: indeg:to_vertex -> from_vertex1,from_vertex2,...
Example: indeg:user:bob -> user:alice,user:dave
```

### Composite Index

```
Format: composite:table:col1:col2:val1:val2 -> primary_key
Example: composite:orders:customer:status:alice:pending -> o1,o5
```

## Query Optimization

### Selectivity Estimation

```cpp
// Index statistics: sample-based cardinality estimation
struct IndexStats {
    uint64_t unique_values;    // Distinct values in index
    uint64_t total_entries;    // Total indexed entities
    uint64_t sample_size;      // Sample used for estimation
};

// Selectivity calculation
double selectivity = unique_values / (double)total_entries;
uint64_t estimated_results = total_entries * selectivity;
```

### Predicate Reordering

```
Input Query:
  predicates: [
    {column: "department", value: "Engineering"},  // 1000 results
    {column: "level", value: "Senior"}             // 50 results
  ]

After Optimization:
  execution_order: [
    {column: "level", value: "Senior"},            // Start with most selective
    {column: "department", value: "Engineering"}   // Intersect with smaller set
  ]

Benefit: 50 vs 1000 initial candidates (20x reduction)
```

### Execution Modes

1. **Index-Accelerated** (predicates with indexes):
   - Parallel index scans → intersection → entity loading
   - Typical latency: 0.1-2 ms (depending on result set size)

2. **Range-Aware** (range predicates + ORDER BY):
   - Direct range scan → sorted results (no intersection)
   - Typical latency: 0.5-5 ms (depends on range width)

3. **Full-Scan Fallback** (no indexes, allow_full_scan: true):
   - Sequential table scan → filter in memory
   - Typical latency: 10-500 ms (depends on table size)
   - **Warning**: Expensive for large tables (>10K entities)

## Compression Strategy

### Write Amplification vs Storage Savings

```
Level   | Compression | Write Amp | Use Case
--------|-------------|-----------|----------------------------------
L0-L5   | LZ4         | 1.05x     | Hot data (frequent compaction)
L6      | ZSTD        | 1.15x     | Cold data (infrequent compaction)
```

**Rationale:**
- LZ4: Fast compression (33.8 MB/s write throughput, 2.1x ratio)
- ZSTD: Better ratio (32.3 MB/s, 2.8x ratio) but slower → only for bottommost level
- Hybrid strategy: Balance performance and storage efficiency

See [memory_tuning.md](memory_tuning.md) for benchmark results.

## Deployment Patterns

### Standalone Server

```
THEMIS Server (Port 8765)
     │
     ├─> Data Directory: ./data/themis_server
     ├─> Config: ./config/config.json
     └─> Logs: stdout (spdlog)
```

### Docker Container

```
Docker Host
     │
     ├─> Container: vccdb:latest
     │    ├─> Port Mapping: 8765:8765
     │    ├─> Volume: /data (persistent storage)
     │    └─> Config Mount: /etc/vccdb/config.json
     │
     └─> External Access: http://localhost:8765
```

### Monitoring Stack (Prometheus + Grafana)

```
┌────────────┐       ┌─────────────┐       ┌──────────┐
│   THEMIS    │──────>│ Prometheus  │──────>│ Grafana  │
│ (Port 8765)│ scrape│ (Port 9090) │ query │ (Port    │
│  /metrics  │       │             │       │  3000)   │
└────────────┘       └─────────────┘       └──────────┘

Prometheus Scrape Config:
  scrape_interval: 15s
  metrics_path: /metrics
  targets: ['vccdb:8765']

Grafana Dashboards:
  - QPS, Error Rate, Latency (p50/p95/p99)
  - RocksDB: Cache Hit Rate, Compaction Stats, Memtable Size
  - System: CPU, Memory, Disk I/O
```

## Performance Tuning

### RocksDB Configuration

**For Write-Heavy Workloads:**
```json
{
  "memtable_size_mb": 512,      // Larger write buffer
  "max_write_buffer_number": 4,  // More concurrent memtables
  "compression": "lz4"           // Fast compression
}
```

**For Read-Heavy Workloads:**
```json
{
  "block_cache_size_mb": 4096,   // Larger read cache
  "enable_bloom_filters": true,  // Reduce disk seeks
  "compression": "zstd"          // Better compression ratio
}
```

### Query Engine Tuning

**Batch Processing Thresholds:**
```cpp
// Adjust in query_engine.cpp
constexpr size_t PARALLEL_THRESHOLD = 100;  // Entities before parallelization
constexpr size_t BATCH_SIZE = 50;           // Entities per batch

// For low-latency use cases:
PARALLEL_THRESHOLD = 50;   // More aggressive parallelization
BATCH_SIZE = 25;           // Smaller batches (lower latency variance)

// For high-throughput use cases:
PARALLEL_THRESHOLD = 200;  // Less overhead
BATCH_SIZE = 100;          // Larger batches (better CPU utilization)
```

### Index Maintenance

**Rebuild Strategy:**
- **Periodic**: Weekly rebuild for active tables (prevents fragmentation)
- **On-Demand**: After bulk inserts (>10K entities)
- **Parallel**: Use `bench_index_rebuild` pattern for large tables

**TTL Cleanup:**
```cpp
// Automatic expiration (no manual cleanup needed)
// TTL indexes prune expired entries during range scans
```

## Observability

### Health Check

```bash
curl http://localhost:8765/health
# Response: {"status":"ok","timestamp":"2025-10-28T10:30:00Z"}
```

### Configuration Inspection

```bash
curl http://localhost:8765/config | jq .
# Returns: server config, RocksDB config, runtime stats, metrics
```

### Metrics Export

```bash
curl http://localhost:8765/metrics
# Prometheus text format with 25+ metrics
```

### Statistics Analysis

```bash
curl http://localhost:8765/stats | jq .storage.rocksdb
# Detailed RocksDB stats: cache hit rate, compaction, files per level
```

## References

- [Base Entity Layer](base_entity.md)
- [Memory Tuning Guide](memory_tuning.md)
- [Index Documentation](indexes.md)
- [OpenAPI Specification](../docs/openapi.yaml)
