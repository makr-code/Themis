# ThemisDB Infrastructure Roadmap 2025-2026

**Version:** 1.0  
**Erstellt:** 09. November 2025  
**Scope:** Horizontal Sharding → Replication → Client SDKs → Admin UI

---

## Executive Summary

ThemisDB verfügt über ein **solides Multi-Model Feature-Set** (Relational, Graph, Vector, Document, Time-Series) mit **100% Encryption Coverage**. Die kritische Lücke ist **Infrastructure**: Keine Horizontal Scalability, keine High Availability, keine Client-SDKs, keine Admin-UI.

**Strategic Direction:**
1. **Phase 1 (Q1 2026):** URN-basiertes Föderales Sharding - Scale-out auf 10+ Nodes
2. **Phase 2 (Q2 2026):** Log-based Replication - High Availability via Raft Consensus
3. **Phase 3 (Q2-Q3 2026):** Client SDKs - Python, JavaScript, Java Libraries
4. **Phase 4 (Q3 2026):** Admin UI - React-basierte Web-Console

**Investment:** ~12-18 Monate Engineering-Zeit  
**ROI:** Enterprise-Ready Database Platform

---

## Table of Contents

1. [URN-based Sharding Architecture](#1-urn-based-sharding-architecture)
2. [Replication Protocol](#2-replication-protocol)
  - [2.5 Read Path: Caching & Lookup Strategies](#25-read-path-caching--lookup-strategies)
3. [Client SDK Design](#3-client-sdk-design)
4. [Admin UI Design](#4-admin-ui-design)
5. [Implementation Timeline](#5-implementation-timeline)
6. [Migration Strategy](#6-migration-strategy)
7. [Risk Assessment](#7-risk-assessment)

---

## 1. URN-based Sharding Architecture

### 1.1 Design Philosophy: Föderale Abstraktion

**Problem:** Traditionelles Sharding (Hash-based, Range-based) ist **starr** und erfordert Downtime bei Shard-Bewegungen.

**Lösung:** **URN-based Federated Sharding** - Ressourcen-Identifikatoren entkoppeln Logical Keys von Physical Locations.

**URN Syntax:**
```
urn:themis:{model}:{namespace}:{collection}:{uuid}

Examples:
  urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000
  urn:themis:graph:social:nodes:7c9e6679-7425-40de-944b-e07fc1f90ae7
  urn:themis:vector:embeddings:documents:f47ac10b-58cc-4372-a567-0e02b2c3d479
  urn:themis:timeseries:metrics:cpu_usage:3d6e3e3e-4c5d-4f5e-9e7f-8a9b0c1d2e3f
```

**UUID Format:** RFC 4122 UUID v4 (36 characters with hyphens)

**Benefits:**
- ✅ **Location Transparency** - Clients wissen nicht, auf welchem Shard Daten liegen
- ✅ **Dynamic Resharding** - Shards können verschoben werden ohne Client-Changes
- ✅ **Multi-Tenancy** - Namespaces isolieren Mandanten
- ✅ **Cross-Model Queries** - URN-basiertes Routing über alle Datenmodelle

### 1.2 Architecture Components

```
┌─────────────────────────────────────────────────────────────────┐
│                         Client Layer                             │
│  (Python SDK, JS SDK, HTTP Client)                              │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Routing Layer                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ URN Resolver │  │ Shard Router │  │ Load Balancer│          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│  - URN → Shard Mapping (Consistent Hashing)                     │
│  - Locality Awareness (Data Center, Rack)                       │
│  - Query Routing (Single-Shard vs. Scatter-Gather)              │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Metadata Layer                               │
│  ┌──────────────────────────────────────────────────────┐       │
│  │  Shard Map (etcd / Consul)                           │       │
│  │  - URN Namespace → Shard ID Mapping                  │       │
│  │  - Shard Topology (Primary, Replicas, Locations)     │       │
│  │  - Schema Registry (Collections, Indexes, Encryption)│       │
│  └──────────────────────────────────────────────────────┘       │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Storage Layer                               │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │ Shard 1  │  │ Shard 2  │  │ Shard 3  │  │ Shard N  │        │
│  │ RocksDB  │  │ RocksDB  │  │ RocksDB  │  │ RocksDB  │        │
│  │ (Primary)│  │ (Primary)│  │ (Primary)│  │ (Primary)│        │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘        │
│       │             │             │             │                │
│  ┌────▼───┐   ┌────▼───┐   ┌────▼───┐   ┌────▼───┐           │
│  │Replica1│   │Replica1│   │Replica1│   │Replica1│           │
│  └────────┘   └────────┘   └────────┘   └────────┘           │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 URN Resolver Implementation

**File:** `include/sharding/urn_resolver.h`

```cpp
#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <memory>

namespace themis::sharding {

/// URN Structure: urn:themis:{model}:{namespace}:{collection}:{uuid}
struct URN {
    std::string model;        // relational, graph, vector, timeseries, document
    std::string namespace_;   // customer_a, tenant_123, global
    std::string collection;   // users, nodes, documents, edges
    std::string uuid;         // RFC 4122 UUID v4 (e.g., 550e8400-e29b-41d4-a716-446655440000)
    
    /// Parse URN string into components
    static std::optional<URN> parse(std::string_view urn_str);
    
    /// Serialize URN to string
    std::string toString() const;
    
    /// Hash URN for consistent hashing (uses UUID for distribution)
    uint64_t hash() const;
    
    /// Validate UUID format (RFC 4122)
    bool isValidUUID() const;
    
    /// Get full resource identifier (collection:uuid)
    std::string getResourceId() const { return collection + ":" + uuid; }
};

/// Shard Information
struct ShardInfo {
    std::string shard_id;           // shard_001, shard_002, ...
    std::string primary_endpoint;   // themis-shard001.dc1.example.com:8080
    std::vector<std::string> replica_endpoints; // replica nodes
    std::string datacenter;         // dc1, dc2, us-east-1, eu-west-1
    std::string rack;               // rack01, rack02 (locality awareness)
    uint64_t token_start;           // Consistent Hash Range Start
    uint64_t token_end;             // Consistent Hash Range End
    bool is_healthy;                // Health check status
};

/// URN Resolver - Maps URNs to Shard Locations
class URNResolver {
public:
    /// Initialize resolver with shard topology
    URNResolver(std::shared_ptr<class ShardTopology> topology);
    
    /// Resolve URN to Shard Info (Primary)
    std::optional<ShardInfo> resolvePrimary(const URN& urn) const;
    
    /// Resolve URN to all Replicas (for read scaling)
    std::vector<ShardInfo> resolveReplicas(const URN& urn) const;
    
    /// Check if URN is local to this node
    bool isLocal(const URN& urn) const;
    
    /// Get Shard ID for URN (without full ShardInfo)
    std::string getShardId(const URN& urn) const;
    
    /// Get all Shards in cluster
    std::vector<ShardInfo> getAllShards() const;
    
    /// Reload topology from metadata store (etcd)
    void refreshTopology();
    
private:
    std::shared_ptr<ShardTopology> topology_;
    std::string local_shard_id_; // This node's shard ID
};

} // namespace themis::sharding
```

### 1.4 Consistent Hashing Ring

**File:** `include/sharding/consistent_hash.h`

```cpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <functional>

namespace themis::sharding {

/// Consistent Hashing Ring for even data distribution
class ConsistentHashRing {
public:
    /// Add a shard to the ring with virtual nodes
    /// @param shard_id Unique shard identifier
    /// @param virtual_nodes Number of virtual nodes (higher = better balance)
    void addShard(const std::string& shard_id, size_t virtual_nodes = 150);
    
    /// Remove a shard from the ring
    void removeShard(const std::string& shard_id);
    
    /// Get shard for a given key hash
    std::string getShardForHash(uint64_t hash) const;
    
    /// Get shard for a URN
    std::string getShardForURN(const URN& urn) const;
    
    /// Get N successor shards (for replication)
    std::vector<std::string> getSuccessors(uint64_t hash, size_t count) const;
    
    /// Get hash range for a shard (min, max)
    std::pair<uint64_t, uint64_t> getShardRange(const std::string& shard_id) const;
    
    /// Get all shards in ring order
    std::vector<std::string> getAllShards() const;
    
    /// Calculate balance factor (std dev of keys per shard)
    double getBalanceFactor() const;
    
private:
    // Token (hash) → Shard ID mapping
    std::map<uint64_t, std::string> ring_;
    
    // Shard ID → Virtual Node Tokens
    std::map<std::string, std::vector<uint64_t>> shard_tokens_;
    
    // Hash function (MurmurHash3 or xxHash)
    uint64_t hash(const std::string& key) const;
};

} // namespace themis::sharding
```

### 1.5 Shard Router

**File:** `include/sharding/shard_router.h`

```cpp
#pragma once

#include "sharding/urn_resolver.h"
#include "query/query_engine.h"
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace themis::sharding {

/// Query Routing Strategy
enum class RoutingStrategy {
    SINGLE_SHARD,     // Query hits one shard (e.g., GET by URN)
    SCATTER_GATHER,   // Query spans all shards (e.g., full table scan)
    NAMESPACE_LOCAL,  // Query scoped to namespace (multi-shard but not all)
    CROSS_SHARD_JOIN  // Join across shards (expensive)
};

/// Result from a remote shard
struct ShardResult {
    std::string shard_id;
    nlohmann::json data;
    bool success;
    std::string error_msg;
    uint64_t execution_time_ms;
};

/// Shard Router - Routes queries to appropriate shards
class ShardRouter {
public:
    ShardRouter(
        std::shared_ptr<URNResolver> resolver,
        std::shared_ptr<class RemoteExecutor> executor
    );
    
    /// Route a GET request by URN
    /// @return Result from primary shard
    std::optional<nlohmann::json> get(const URN& urn);
    
    /// Route a PUT request by URN
    bool put(const URN& urn, const nlohmann::json& data);
    
    /// Route a DELETE request by URN
    bool del(const URN& urn);
    
    /// Route an AQL query
    /// @param query AQL query string
    /// @return Combined results from all shards
    nlohmann::json executeQuery(const std::string& query);
    
    /// Determine routing strategy for a query
    RoutingStrategy analyzeQuery(const std::string& query) const;
    
    /// Execute scatter-gather query
    /// @param query Query to execute on all shards
    /// @return Merged results (union of all shard results)
    std::vector<ShardResult> scatterGather(const std::string& query);
    
    /// Execute cross-shard join (two-phase)
    /// Phase 1: Fetch from first collection
    /// Phase 2: Lookup in second collection
    nlohmann::json executeCrossShardJoin(
        const std::string& query,
        const std::string& join_field
    );
    
private:
    std::shared_ptr<URNResolver> resolver_;
    std::shared_ptr<RemoteExecutor> executor_;
    
    /// Merge results from multiple shards
    nlohmann::json mergeResults(const std::vector<ShardResult>& results);
    
    /// Apply LIMIT/OFFSET across shards
    nlohmann::json applyPagination(
        const nlohmann::json& merged,
        size_t offset,
        size_t limit
    );
};

/// Remote Executor - HTTP client for shard-to-shard communication
class RemoteExecutor {
public:
    /// Execute HTTP GET on remote shard
    std::optional<nlohmann::json> get(
        const std::string& endpoint,
        const std::string& path
    );
    
    /// Execute HTTP POST on remote shard
    std::optional<nlohmann::json> post(
        const std::string& endpoint,
        const std::string& path,
        const nlohmann::json& body
    );
    
    /// Execute batch requests in parallel
    std::vector<ShardResult> batchExecute(
        const std::vector<std::string>& endpoints,
        const std::string& path,
        const nlohmann::json& body
    );
    
private:
    // Connection pool for shard-to-shard HTTP
    // Reuse connections, timeout handling, retry logic
};

} // namespace themis::sharding
```

### 1.6 Shard Topology Manager

**File:** `include/sharding/shard_topology.h`

```cpp
#pragma once

#include "sharding/urn_resolver.h"
#include "sharding/consistent_hash.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace themis::sharding {

/// Metadata Store Backend (etcd, Consul, ZooKeeper)
class MetadataStore {
public:
    virtual ~MetadataStore() = default;
    
    /// Get value by key
    virtual std::optional<std::string> get(const std::string& key) = 0;
    
    /// Set key-value pair
    virtual bool put(const std::string& key, const std::string& value) = 0;
    
    /// Delete key
    virtual bool del(const std::string& key) = 0;
    
    /// List keys with prefix
    virtual std::vector<std::string> list(const std::string& prefix) = 0;
    
    /// Watch key for changes (blocking)
    virtual void watch(
        const std::string& key,
        std::function<void(const std::string&)> callback
    ) = 0;
};

/// Shard Topology - Manages cluster layout
class ShardTopology {
public:
    /// Initialize with metadata store (etcd)
    ShardTopology(std::shared_ptr<MetadataStore> metadata);
    
    /// Load topology from metadata store
    void load();
    
    /// Add a new shard to topology
    void addShard(const ShardInfo& shard);
    
    /// Remove shard from topology (triggers rebalancing)
    void removeShard(const std::string& shard_id);
    
    /// Update shard health status
    void updateShardHealth(const std::string& shard_id, bool is_healthy);
    
    /// Get all shards
    std::vector<ShardInfo> getAllShards() const;
    
    /// Get shard by ID
    std::optional<ShardInfo> getShard(const std::string& shard_id) const;
    
    /// Get consistent hash ring
    const ConsistentHashRing& getHashRing() const { return hash_ring_; }
    
    /// Trigger rebalancing (move data between shards)
    void rebalance();
    
    /// Watch for topology changes
    void startWatching(std::function<void()> on_change_callback);
    
private:
    std::shared_ptr<MetadataStore> metadata_;
    ConsistentHashRing hash_ring_;
    std::map<std::string, ShardInfo> shards_;
    mutable std::shared_mutex mutex_;
    
    /// Persist topology to metadata store
    void persist();
    
    /// Calculate rebalance plan
    struct RebalancePlan {
        std::string from_shard;
        std::string to_shard;
        uint64_t token_range_start;
        uint64_t token_range_end;
        size_t estimated_keys;
    };
    std::vector<RebalancePlan> calculateRebalancePlan();
};

} // namespace themis::sharding
```

### 1.7 Migration from Single-Node to Sharded

**Step-by-Step:**

1. **Setup Metadata Store** (etcd cluster)
   ```bash
   # Install etcd 3-node cluster
   docker-compose -f etcd-cluster.yml up -d
   ```

2. **Initialize Shard Topology**
   ```cpp
   auto metadata = std::make_shared<EtcdMetadataStore>("http://etcd:2379");
   auto topology = std::make_shared<ShardTopology>(metadata);
   
   // Add initial shard (existing single node)
   ShardInfo shard1;
   shard1.shard_id = "shard_001";
   shard1.primary_endpoint = "themis-node1:8080";
   shard1.datacenter = "dc1";
   shard1.token_start = 0;
   shard1.token_end = UINT64_MAX;
   topology->addShard(shard1);
   
   // Note: All existing keys will be migrated to UUID format
   // Old: "users:123" → New: "users:550e8400-e29b-41d4-a716-446655440000"
   ```

3. **Add New Shards** (Scale-out)
   ```cpp
   ShardInfo shard2;
   shard2.shard_id = "shard_002";
   shard2.primary_endpoint = "themis-node2:8080";
   shard2.datacenter = "dc1";
   topology->addShard(shard2); // Triggers rebalancing
   ```

4. **Data Migration** (Background Process)
   - **UUID Conversion:** Convert existing keys to UUID format
     - Generate deterministic UUIDs from old keys (namespace UUID v5)
     - Maintain mapping: old_key → uuid for backward compatibility
   - Consistent Hashing determines uuid→shard mapping
   - Stream data from old shard to new shard
   - Atomic cutover (update metadata store)

5. **Client Update**
   - SDKs refresh topology from metadata store
   - Automatic rerouting to new shards

**Zero-Downtime Migration:**
- Dual-write during migration (write to old + new shard)
- Read from old shard until migration complete
- Atomic flip in metadata store

---

## 2. Replication Protocol

### 2.1 Design: Log-based Replication with Raft

**Why Raft?**
- ✅ **Proven** - Used by etcd, Consul, TiKV
- ✅ **Understandable** - Simpler than Paxos
- ✅ **Strong Consistency** - Linearizable reads/writes
- ✅ **Leader Election** - Automatic failover

**Architecture:**

```
┌──────────────────────────────────────────────────────────────┐
│                    Raft Consensus Group                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                   │
│  │ Leader   │  │ Follower │  │ Follower │                   │
│  │ (Primary)│  │ (Replica)│  │ (Replica)│                   │
│  └────┬─────┘  └────▲─────┘  └────▲─────┘                   │
│       │             │             │                           │
│       │  AppendEntries (Log Replication)                     │
│       └─────────────┴─────────────┘                           │
└──────────────────────────────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────────┐
│                     WAL (Write-Ahead Log)                     │
│  ┌────────────────────────────────────────────────────┐      │
│  │ [1] PUT users:123 {"name":"Alice"}                 │      │
│  │ [2] DEL orders:456                                 │      │
│  │ [3] PUT graph:edge:e1 {"from":"A","to":"B"}      │      │
│  │ [4] COMMIT txn_789                                 │      │
│  └────────────────────────────────────────────────────┘      │
└──────────────────────────────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────────┐
│                    RocksDB Storage                            │
│  (State Machine - applies committed WAL entries)             │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 Replication Manager

**File:** `include/replication/replication_manager.h`

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace themis::replication {

/// WAL Entry Type
enum class WALEntryType : uint8_t {
    PUT,
    DELETE,
    TRANSACTION_BEGIN,
    TRANSACTION_COMMIT,
    TRANSACTION_ABORT,
    SNAPSHOT_MARKER
};

/// WAL Entry - Single operation in Write-Ahead Log
struct WALEntry {
    uint64_t log_index;      // Raft log index
    uint64_t term;           // Raft term
    WALEntryType type;       // Operation type
    std::string key;         // RocksDB key
    std::vector<uint8_t> value; // RocksDB value (empty for DELETE)
    uint64_t timestamp_ms;   // Wall clock time
    
    /// Serialize to binary
    std::vector<uint8_t> serialize() const;
    
    /// Deserialize from binary
    static WALEntry deserialize(const std::vector<uint8_t>& data);
};

/// Raft Node State
enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

/// Replication Manager - Raft-based consensus
class ReplicationManager {
public:
    struct Config {
        std::string node_id;                // themis-node1, themis-node2, ...
        std::vector<std::string> peers;     // Other nodes in cluster
        std::string wal_dir = "./wal";      // WAL directory
        uint32_t election_timeout_ms = 1500; // 1.5 seconds
        uint32_t heartbeat_interval_ms = 500; // 500ms
        size_t snapshot_interval_entries = 10000; // Snapshot every 10k entries
    };
    
    ReplicationManager(
        const Config& config,
        std::shared_ptr<class RocksDBWrapper> storage
    );
    
    ~ReplicationManager();
    
    /// Start replication (join Raft group)
    void start();
    
    /// Stop replication
    void stop();
    
    /// Append entry to WAL (only on leader)
    /// @return true if committed (majority replicated)
    bool appendEntry(const WALEntry& entry);
    
    /// Get current node state
    NodeState getState() const;
    
    /// Get current leader ID (empty if no leader)
    std::string getLeaderID() const;
    
    /// Check if this node is leader
    bool isLeader() const;
    
    /// Force leadership election
    void electLeader();
    
    /// Get WAL statistics
    struct WALStats {
        uint64_t last_log_index;
        uint64_t last_applied_index;
        uint64_t commit_index;
        size_t pending_entries;
    };
    WALStats getWALStats() const;
    
    /// Take snapshot of current state
    void takeSnapshot();
    
    /// Register callback for leadership changes
    void onLeadershipChange(std::function<void(bool is_leader)> callback);
    
private:
    Config config_;
    std::shared_ptr<RocksDBWrapper> storage_;
    NodeState state_ = NodeState::FOLLOWER;
    uint64_t current_term_ = 0;
    std::string voted_for_;
    std::string leader_id_;
    
    // WAL state
    std::vector<WALEntry> log_;
    uint64_t commit_index_ = 0;
    uint64_t last_applied_ = 0;
    
    // Raft RPC handlers
    void handleRequestVote(/* ... */);
    void handleAppendEntries(/* ... */);
    void handleInstallSnapshot(/* ... */);
    
    // Background threads
    void electionTimerLoop();
    void heartbeatLoop();
    void applyCommittedEntries();
};

} // namespace themis::replication
```

### 2.3 Read Replicas (Scale-out Reads)

```cpp
namespace themis::replication {

/// Read Replica - Follower node optimized for read queries
class ReadReplica {
public:
    /// Initialize read replica
    ReadReplica(
        std::shared_ptr<ReplicationManager> replication,
        std::shared_ptr<RocksDBWrapper> storage
    );
    
    /// Serve read query (eventually consistent)
    std::optional<std::vector<uint8_t>> get(const std::string& key);
    
    /// Execute AQL query on replica
    nlohmann::json executeQuery(const std::string& query);
    
    /// Get replication lag (ms behind leader)
    uint64_t getReplicationLag() const;
    
    /// Check if replica is caught up
    bool isCaughtUp() const;
    
private:
    std::shared_ptr<ReplicationManager> replication_;
    std::shared_ptr<RocksDBWrapper> storage_;
};

} // namespace themis::replication
```

### 2.4 Failover Strategy

**Scenario:** Leader node fails

1. **Detection** - Followers stop receiving heartbeats (500ms timeout)
2. **Election** - Followers become candidates, request votes
3. **New Leader** - Candidate with most votes becomes leader
4. **Catchup** - New leader sends missing WAL entries to followers
5. **Resume** - Cluster continues normal operation

**Recovery Time:** ~2-3 seconds (1.5s election timeout + catchup)

---

### 2.5 Read Path: Caching & Lookup Strategies

Ziel: Massive parallele Lesezugriffe mit niedriger Latenz (p99 < 20ms) und hohem Durchsatz (100k+ QPS über den Cluster) durch mehrstufiges Caching, Request-Coalescing, Batching/Multi-Get und probabilistische Filter – strikt korrekt in Gegenwart von Replikation und Rebalancing.

#### 2.5.1 Prinzipien

- URN-First: Cache-Keys sind immer die vollständige URN (inkl. `{collection}:{uuid}`) für Entities und der normalisierte Plan-Hash für AQL-Resultsets.
- Sicherheit & Isolation: Namespace ist Teil der URN → strikte Tenant-Isolation in allen Caches.
- Freshness durch Versionierung: Jede Entity trägt `version` (monoton), Caches speichern `(value, version, ts)`.
- Invalidation via WAL/Changefeed: Writes erzeugen Events, die Cache-Layer invalidieren/aktualisieren.
- Admission/Eviction datengetrieben: TinyLFU/Windowed-LFU, getrennte Policies für Hot Keys und Query-Resultsets.

#### 2.5.2 Cache-Layer

- L1 In-Process Cache (pro Server):
  - Datenstruktur: lock-arme HashMap + TinyLFU/ARC; TTL konfigurierbar; Negative Caching für 404 (kurze TTL, z.B. 1–5s).
  - Scope: URN→Entity, Shard-Directory (URN→ShardID), Plan-Hash→Resultpage (mit Cursor).
  - Größe: 1–4 GB pro Prozess; optional „pinned hot set“.

- L2 Shard-Lokaler Cache (pro Shard):
  - Backend: RocksDB Secondary CF oder Shared-Memory-Cache (memcached/redis optional) – Keyspace ist URN.
  - Nutzen: Interprozess- und Reboot-Resilienz; kann Rebuilds überstehen.

- Ergebnis-Cache (AQL):
  - Key: `plan_hash(query_normalized, params_normalized)` + `namespace` + `shard_scope`.
  - Speichert Seiten (page of results) + „continuation token“; kurze TTL (5–60s), invalidiert bei betroffenen Writes.

Hinweis: `include/cache/semantic_cache.h` existiert bereits als TTL-basierter Exact-Match-Cache. Er dient als Grundlage; generische Interfaces unter `include/cache/*` erweitern dies um Entity-/Result-Caches.

#### 2.5.3 Coherency & Invalidierung

- Write-Through Pfad: Erfolgreiche PUT/DELETE aktualisieren WAL → Changefeed → L1/L2 Invalidate/Update (Versionbasiert).
- Replikationsbewusst:
  - Leader: schreibt sofort in Cache (new version) und publiziert Event.
  - Follower/Read-Replica: akzeptiert Cache-Hit nur, wenn `cached.version ≥ applied_version` oder `replication_lag < threshold`; sonst Read-Through.
- Rebalancing/Epochs: Topology-Änderungen bumpen `cache_epoch`. Keys mit älterer Epoch werden verifiziert oder kalt gelesen.

#### 2.5.4 Request Coalescing („singleflight“)

Mehrfache gleichzeitige GETs derselben URN werden zusammengelegt. Nur eine Backend-Abfrage; andere warten auf dasselbe Future.

```cpp
// include/cache/request_coalescer.h
class RequestCoalescer {
public:
    // Führt f() einmal pro key aus; parallele Aufrufer warten auf dasselbe Resultat
    template<typename F>
    auto Do(const std::string& key, F&& f) -> std::shared_ptr<struct Result>;
};
```

#### 2.5.5 Batching & Multi-Get

- API: `batch_get(model, collection, uuids[])` auf Router/SDK-Ebene; gruppiert nach Shard und führt parallele Multi-GETs aus.
- AQL: Normalisierte „IN“-Lookups erzeugen Shard-lokale Batch-Pfade; Ergebnis-Cache optional pro Shard-Teilmenge.

```python
# Python SDK – Multi-Get
users = client.batch_get("relational", "users", [uuid1, uuid2, uuid3])
```

```typescript
// JS SDK – Multi-Get
const docs = await client.batchGet('document', 'posts', [u1, u2, u3]);
```

#### 2.5.6 Probabilistische Filter

- Bloom-Filter pro sekundärem Index/Shard für schnelle „exists?“-Checks und negative Ergebnisse.
- Counting Bloom/Quotient Filter, um Deletes korrekt zu reflektieren.

#### 2.5.7 RocksDB Read-Pfad Tuning

- Block-Cache vergrößern und sharden; `pin_l0_filter_and_index_blocks_in_cache=true`.
- Partitioned Index/Filter; zstd-komprimierte Blöcke; Prefetch/Read-Ahead für Range-Scans.

#### 2.5.8 Schnittstellen (C++)

```cpp
// include/cache/cache_provider.h
struct CacheValue {
    std::string json;   // serialisierte Entity/Resultseite
    uint64_t version;   // monoton (WAL index / vector clock)
    uint64_t ts_ms;     // Einfügezeit (für TTL)
};

class CacheProvider {
public:
    virtual ~CacheProvider() = default;
    virtual bool Get(std::string_view key, CacheValue& out) = 0;
    virtual void Put(std::string_view key, const CacheValue& v, uint64_t ttl_ms) = 0;
    virtual void Invalidate(std::string_view key) = 0;
};

// Entity-Cache-Helper
inline std::string EntityKey(const URN& urn) { return urn.toString(); }
```

```cpp
// Router – Read-Through mit Coalescing + Versioncheck
auto ShardRouter::get(const URN& urn) -> std::optional<nlohmann::json> {
    const auto key = urn.toString();
    CacheValue cv;
    if (l1_->Get(key, cv) && isFresh(cv)) return nlohmann::json::parse(cv.json);
    auto res = coalescer_->Do(key, [&]{
        // Remote lesen
        auto shard = resolver_->resolvePrimary(urn);
        return executor_->get(shard->primary_endpoint, "/entity/" + key);
    });
    if (res && res->success) {
        CacheValue nv{res->data.dump(), res->version, now_ms()};
        l1_->Put(key, nv, ttl_entity_ms);
        l2_->Put(key, nv, ttl_entity_ms);
        return res->data;
    }
    return std::nullopt;
}
```

#### 2.5.9 SDK-Anpassungen

- Batch-APIs: `batch_get`, `batch_put`, `batch_delete` (Shard-aware, parallel, mit partiellen Fehlern).
- Ergebnis-Cache-Header: SDK kann `Cache-Control`/ETag-Versionen nutzen; bedingte GETs (`If-None-Match`).

#### 2.5.10 Metriken & SLOs

- Cache: `hits/misses`, `hit_ratio`, `evictions`, `bytes`, `ttl_expired`, `coalesce_wait_ms` (p50/p95/p99).
- Read-Path: `end-to-end latency` (p50/p95/p99), `backend qps`, `replication_lag_ms`, `negative_cache_hits`.

#### 2.5.11 Implementierungsplan (inkrementell)

1) L1-Entity-Cache + Request-Coalescer (nur GET by URN) – guarded per Feature-Flag.  
2) Invalidierung via WAL-Changefeed (Leader) + Propagation zu Replikas.  
3) Batch/Multi-Get in Router + SDKs.  
4) L2-Shard-Cache (RocksDB CF) + Hot-Set Pinning.  
5) Ergebnis-Cache (Plan-Hash) für häufige AQLs.  
6) Bloom-Filter für negative Lookups pro Index.  

Risiken: Stale Reads bei Lag → mitigiert durch Versionstempel/ETag; Overadmission → TinyLFU; Split-Brain → nur Leader invalidiert authoritative, Replikas respektieren Lag-Schranken.

---

## 3. Client SDK Design

### 3.1 Python SDK

**File:** `clients/python/themis/__init__.py`

```python
from typing import Optional, List, Dict, Any
import httpx
import json

class ThemisClient:
    """ThemisDB Python Client SDK"""
    
    def __init__(
        self,
        endpoints: List[str],
        namespace: str = "default",
        timeout: int = 30,
        max_retries: int = 3
    ):
        """
        Initialize ThemisDB client
        
        Args:
            endpoints: List of ThemisDB endpoints (e.g., ["http://shard1:8080", "http://shard2:8080"])
            namespace: URN namespace for multi-tenancy
            timeout: Request timeout in seconds
            max_retries: Number of retries on failure
        """
        self.endpoints = endpoints
        self.namespace = namespace
        self.timeout = timeout
        self.max_retries = max_retries
        self._topology_cache = None
        self._http_client = httpx.Client(timeout=timeout)
    
    def get(self, model: str, collection: str, uuid: str) -> Optional[Dict[str, Any]]:
        """
        Get entity by URN
        
        Args:
            model: Data model (relational, graph, vector, timeseries)
            collection: Collection/table name (e.g., "users", "nodes")
            uuid: RFC 4122 UUID v4 (e.g., "550e8400-e29b-41d4-a716-446655440000")
        
        Returns:
            Entity data or None if not found
        """
        urn = f"urn:themis:{model}:{self.namespace}:{collection}:{uuid}"
        endpoint = self._resolve_endpoint(urn)
        
        response = self._http_client.get(f"{endpoint}/entity/{urn}")
        if response.status_code == 200:
            return response.json()
        elif response.status_code == 404:
            return None
        else:
            response.raise_for_status()
    
    def put(self, model: str, collection: str, uuid: str, data: Dict[str, Any]) -> bool:
        """Put entity by URN"""
        urn = f"urn:themis:{model}:{self.namespace}:{collection}:{uuid}"
        endpoint = self._resolve_endpoint(urn)
        
        response = self._http_client.put(
            f"{endpoint}/entity/{urn}",
            json=data
        )
        return response.status_code == 200
    
    def delete(self, model: str, collection: str, uuid: str) -> bool:
        """Delete entity by URN"""
        urn = f"urn:themis:{model}:{self.namespace}:{collection}:{uuid}"
        endpoint = self._resolve_endpoint(urn)
        
        response = self._http_client.delete(f"{endpoint}/entity/{urn}")
        return response.status_code == 200
    
    def query(self, aql: str, **params) -> List[Dict[str, Any]]:
        """
        Execute AQL query
        
        Args:
            aql: AQL query string
            **params: Query parameters for parameterized queries
        
        Returns:
            List of result entities
        """
        # Determine if query is single-shard or scatter-gather
        if self._is_single_shard_query(aql):
            endpoint = self._resolve_query_endpoint(aql)
            return self._execute_query(endpoint, aql, params)
        else:
            # Scatter-gather across all shards
            return self._scatter_gather_query(aql, params)
    
    def graph_traverse(
        self,
        start_node: str,
        max_depth: int = 3,
        edge_type: Optional[str] = None
    ) -> List[str]:
        """
        Graph traversal (BFS)
        
        Args:
            start_node: Starting node URN
            max_depth: Maximum traversal depth
            edge_type: Optional edge type filter
        
        Returns:
            List of node URNs
        """
        endpoint = self._resolve_endpoint(start_node)
        response = self._http_client.post(
            f"{endpoint}/graph/traverse",
            json={
                "start": start_node,
                "max_depth": max_depth,
                "edge_type": edge_type
            }
        )
        response.raise_for_status()
        return response.json()["nodes"]
    
    def vector_search(
        self,
        embedding: List[float],
        top_k: int = 10,
        metadata_filter: Optional[Dict[str, Any]] = None
    ) -> List[Dict[str, Any]]:
        """
        Vector similarity search
        
        Args:
            embedding: Query embedding vector
            top_k: Number of nearest neighbors
            metadata_filter: Optional metadata filter
        
        Returns:
            List of similar documents with scores
        """
        # Vector search is scatter-gather across all shards
        results = []
        for endpoint in self.endpoints:
            response = self._http_client.post(
                f"{endpoint}/vector/search",
                json={
                    "embedding": embedding,
                    "k": top_k,
                    "filter": metadata_filter
                }
            )
            if response.status_code == 200:
                results.extend(response.json()["results"])
        
        # Merge and re-rank results
        results.sort(key=lambda x: x["score"], reverse=True)
        return results[:top_k]
    
    def _resolve_endpoint(self, urn: str) -> str:
        """Resolve URN to shard endpoint using consistent hashing"""
        if self._topology_cache is None:
            self._refresh_topology()
        
        # Simple hash-based routing (production would use consistent hashing)
        hash_val = hash(urn) % len(self.endpoints)
        return self.endpoints[hash_val]
    
    def _refresh_topology(self):
        """Fetch topology from metadata store"""
        # TODO: Query etcd for shard topology
        self._topology_cache = {"shards": self.endpoints}
    
    def _scatter_gather_query(self, aql: str, params: Dict) -> List[Dict]:
        """Execute query on all shards and merge results"""
        all_results = []
        for endpoint in self.endpoints:
            results = self._execute_query(endpoint, aql, params)
            all_results.extend(results)
        return all_results
    
    def _execute_query(self, endpoint: str, aql: str, params: Dict) -> List[Dict]:
        """Execute query on a single endpoint"""
        response = self._http_client.post(
            f"{endpoint}/query/aql",
            json={"query": aql, "params": params}
        )
        response.raise_for_status()
        return response.json()["results"]
    
    def _is_single_shard_query(self, aql: str) -> bool:
        """Analyze if query can be routed to single shard"""
        # Simple heuristic: if query has URN filter, it's single-shard
        return "urn:themis:" in aql.lower()

# Example Usage
if __name__ == "__main__":
    import uuid
    
    client = ThemisClient(
        endpoints=["http://shard1:8080", "http://shard2:8080"],
        namespace="customer_a"
    )
    
    # PUT (generate UUID)
    user_uuid = str(uuid.uuid4())  # "550e8400-e29b-41d4-a716-446655440000"
    client.put("relational", "users", user_uuid, {"name": "Alice", "age": 30})
    
    # GET
    user = client.get("relational", "users", user_uuid)
    print(user)  # {"name": "Alice", "age": 30}
    
    # Query (UUIDs in results)
    results = client.query("FOR u IN users FILTER u.age > 25 RETURN u")
    # Results contain _key field with UUID
    
    # Graph Traverse
    node_uuid = "7c9e6679-7425-40de-944b-e07fc1f90ae7"
    friends = client.graph_traverse(
        f"urn:themis:graph:social:nodes:{node_uuid}", 
        max_depth=2
    )
    
    # Vector Search (returns UUIDs)
    similar = client.vector_search([0.1, 0.2, ...], top_k=5)
    # Result: [{"uuid": "f47ac10b-...", "score": 0.95, ...}]
```

### 3.2 JavaScript SDK

**File:** `clients/javascript/src/index.ts`

```typescript
export interface ThemisConfig {
  endpoints: string[];
  namespace?: string;
  timeout?: number;
  maxRetries?: number;
}

export interface QueryResult<T = any> {
  results: T[];
  cursor?: string;
  explain?: any;
}

export class ThemisClient {
  private endpoints: string[];
  private namespace: string;
  private timeout: number;
  private maxRetries: number;
  
  constructor(config: ThemisConfig) {
    this.endpoints = config.endpoints;
    this.namespace = config.namespace || 'default';
    this.timeout = config.timeout || 30000;
    this.maxRetries = config.maxRetries || 3;
  }
  
  async get<T = any>(model: string, collection: string, uuid: string): Promise<T | null> {
    const urn = `urn:themis:${model}:${this.namespace}:${collection}:${uuid}`;
    const endpoint = this.resolveEndpoint(urn);
    
    const response = await fetch(`${endpoint}/entity/${urn}`, {
      method: 'GET',
      signal: AbortSignal.timeout(this.timeout)
    });
    
    if (response.status === 404) return null;
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    
    return response.json();
  }
  
  async put(model: string, collection: string, uuid: string, data: any): Promise<boolean> {
    const urn = `urn:themis:${model}:${this.namespace}:${collection}:${uuid}`;
    const endpoint = this.resolveEndpoint(urn);
    
    const response = await fetch(`${endpoint}/entity/${urn}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data),
      signal: AbortSignal.timeout(this.timeout)
    });
    
    return response.ok;
  }
  
  async query<T = any>(aql: string, params?: any): Promise<QueryResult<T>> {
    if (this.isSingleShardQuery(aql)) {
      const endpoint = this.endpoints[0]; // Simplified
      return this.executeQuery(endpoint, aql, params);
    } else {
      return this.scatterGatherQuery(aql, params);
    }
  }
  
  async graphTraverse(
    startNode: string,
    maxDepth: number = 3,
    edgeType?: string
  ): Promise<string[]> {
    const endpoint = this.resolveEndpoint(startNode);
    
    const response = await fetch(`${endpoint}/graph/traverse`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ start: startNode, max_depth: maxDepth, edge_type: edgeType })
    });
    
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    
    const result = await response.json();
    return result.nodes;
  }
  
  async vectorSearch(
    embedding: number[],
    topK: number = 10,
    filter?: any
  ): Promise<any[]> {
    const allResults: any[] = [];
    
    // Scatter-gather across all shards
    const promises = this.endpoints.map(endpoint =>
      fetch(`${endpoint}/vector/search`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ embedding, k: topK, filter })
      }).then(r => r.json())
    );
    
    const results = await Promise.all(promises);
    results.forEach(r => allResults.push(...r.results));
    
    // Merge and re-rank
    allResults.sort((a, b) => b.score - a.score);
    return allResults.slice(0, topK);
  }
  
  private resolveEndpoint(urn: string): string {
    // Simple hash-based routing
    const hash = urn.split('').reduce((acc, c) => acc + c.charCodeAt(0), 0);
    return this.endpoints[hash % this.endpoints.length];
  }
  
  private async executeQuery(endpoint: string, aql: string, params?: any): Promise<QueryResult> {
    const response = await fetch(`${endpoint}/query/aql`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ query: aql, params })
    });
    
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }
  
  private async scatterGatherQuery(aql: string, params?: any): Promise<QueryResult> {
    const promises = this.endpoints.map(endpoint =>
      this.executeQuery(endpoint, aql, params)
    );
    
    const results = await Promise.all(promises);
    
    // Merge results
    const allResults = results.flatMap(r => r.results);
    return { results: allResults };
  }
  
  private isSingleShardQuery(aql: string): boolean {
    return aql.toLowerCase().includes('urn:themis:');
  }
}

// Example Usage
import { v4 as uuidv4 } from 'uuid';

const client = new ThemisClient({
  endpoints: ['http://shard1:8080', 'http://shard2:8080'],
  namespace: 'customer_a'
});

// GET by UUID
const userUuid = '550e8400-e29b-41d4-a716-446655440000';
const user = await client.get('relational', 'users', userUuid);

// PUT with new UUID
const newUuid = uuidv4(); // Generates UUID v4
await client.put('relational', 'users', newUuid, { name: 'Bob', age: 25 });

// Query returns entities with _key field containing UUID
const results = await client.query('FOR u IN users FILTER u.age > 25 RETURN u');
// Result: [{ _key: "550e8400-...", name: "Alice", age: 30 }]
```

---

## 4. Admin UI Design

### 4.1 Technology Stack

**Frontend:**
- React 18 + TypeScript
- Material-UI (MUI) for components
- Monaco Editor for AQL query editor
- Recharts for metrics visualization
- React Query for data fetching

**Backend:**
- Admin API endpoints in C++ HTTP Server
- Prometheus metrics scraping
- etcd topology queries

### 4.2 Core Features

#### 4.2.1 Query Editor

```tsx
// components/QueryEditor.tsx
import React, { useState } from 'react';
import Editor from '@monaco-editor/react';
import { Box, Button, CircularProgress } from '@mui/material';
import { useQuery } from '@tanstack/react-query';

export const QueryEditor: React.FC = () => {
  const [aql, setAql] = useState('FOR u IN users RETURN u');
  const [results, setResults] = useState<any[]>([]);
  
  const executeQuery = async () => {
    const response = await fetch('/api/query/aql', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ query: aql })
    });
    const data = await response.json();
    setResults(data.results);
  };
  
  return (
    <Box>
      <Editor
        height="300px"
        language="aql"
        value={aql}
        onChange={(value) => setAql(value || '')}
        theme="vs-dark"
      />
      <Button onClick={executeQuery} variant="contained">
        Execute
      </Button>
      <ResultsTable data={results} />
    </Box>
  );
};
```

#### 4.2.2 Shard Topology Visualization

```tsx
// components/ShardTopology.tsx
import React from 'react';
import { useQuery } from '@tanstack/react-query';
import { Box, Card, Grid, Typography } from '@mui/material';

interface ShardInfo {
  shard_id: string;
  primary_endpoint: string;
  replicas: string[];
  health: 'healthy' | 'degraded' | 'down';
  token_range: [number, number];
}

export const ShardTopology: React.FC = () => {
  const { data: shards } = useQuery<ShardInfo[]>({
    queryKey: ['topology'],
    queryFn: () => fetch('/api/admin/topology').then(r => r.json())
  });
  
  return (
    <Grid container spacing={2}>
      {shards?.map(shard => (
        <Grid item xs={12} md={4} key={shard.shard_id}>
          <Card>
            <Typography variant="h6">{shard.shard_id}</Typography>
            <Typography color={shard.health === 'healthy' ? 'green' : 'red'}>
              {shard.health}
            </Typography>
            <Typography variant="body2">
              Primary: {shard.primary_endpoint}
            </Typography>
            <Typography variant="body2">
              Replicas: {shard.replicas.length}
            </Typography>
          </Card>
        </Grid>
      ))}
    </Grid>
  );
};
```

#### 4.2.3 Metrics Dashboard

```tsx
// components/MetricsDashboard.tsx
import React from 'react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip } from 'recharts';
import { useQuery } from '@tanstack/react-query';

export const MetricsDashboard: React.FC = () => {
  const { data: metrics } = useQuery({
    queryKey: ['metrics'],
    queryFn: () => fetch('/api/admin/metrics').then(r => r.json()),
    refetchInterval: 5000 // Refresh every 5 seconds
  });
  
  return (
    <Box>
      <Typography variant="h5">Query Latency (p95)</Typography>
      <LineChart width={600} height={300} data={metrics?.query_latency_p95}>
        <CartesianGrid strokeDasharray="3 3" />
        <XAxis dataKey="timestamp" />
        <YAxis />
        <Tooltip />
        <Line type="monotone" dataKey="value" stroke="#8884d8" />
      </LineChart>
      
      <Typography variant="h5">Throughput (QPS)</Typography>
      <LineChart width={600} height={300} data={metrics?.throughput}>
        <CartesianGrid strokeDasharray="3 3" />
        <XAxis dataKey="timestamp" />
        <YAxis />
        <Tooltip />
        <Line type="monotone" dataKey="value" stroke="#82ca9d" />
      </LineChart>
    </Box>
  );
};
```

### 4.3 Admin API Endpoints

**File:** `src/server/admin_api_handler.cpp`

```cpp
// GET /api/admin/topology - Get shard topology
nlohmann::json handleGetTopology(const http::request<http::string_body>& req) {
    auto topology = shard_topology_->getAllShards();
    nlohmann::json result = nlohmann::json::array();
    
    for (const auto& shard : topology) {
        result.push_back({
            {"shard_id", shard.shard_id},
            {"primary_endpoint", shard.primary_endpoint},
            {"replicas", shard.replica_endpoints},
            {"health", shard.is_healthy ? "healthy" : "down"},
            {"token_range", {shard.token_start, shard.token_end}},
            {"datacenter", shard.datacenter}
        });
    }
    
    return result;
}

// GET /api/admin/metrics - Get Prometheus metrics
nlohmann::json handleGetMetrics(const http::request<http::string_body>& req) {
    // Query Prometheus for metrics
    auto prom = prometheus_client_->query({
        "themis_query_duration_seconds_bucket",
        "themis_http_requests_total",
        "themis_rocksdb_compaction_pending"
    });
    
    return prom.toJson();
}

// POST /api/admin/rebalance - Trigger rebalancing
nlohmann::json handleRebalance(const http::request<http::string_body>& req) {
    shard_topology_->rebalance();
    return {{"status", "rebalancing started"}};
}
```

---

## 5. Implementation Timeline

### Phase 1: URN-based Sharding (Q1 2026 - 3 months)

**Month 1: Foundation**
- [ ] URN Parser & Resolver
- [ ] Consistent Hashing Ring
- [ ] Metadata Store Integration (etcd)
- [ ] Unit Tests (100+ tests)

**Month 2: Routing Layer**
- [ ] Shard Router Implementation
- [ ] Remote Executor (HTTP client)
- [ ] Scatter-Gather Logic
- [ ] Integration Tests (50+ tests)

**Month 3: Migration & Deployment**
- [ ] Single-Node → Sharded Migration Tool
- [ ] Rebalancing Algorithm
- [ ] Performance Benchmarks
- [ ] Production Deployment

**Deliverables:**
- ✅ Horizontal Scaling auf 10+ Nodes
- ✅ URN-based Routing
- ✅ Zero-Downtime Rebalancing

---

### Phase 2: Replication (Q2 2026 - 3 months)

**Month 1: Raft Consensus**
- [ ] Raft State Machine
- [ ] Leader Election
- [ ] Log Replication
- [ ] Unit Tests (200+ tests)

**Month 2: WAL & Snapshots**
- [ ] Write-Ahead Log Implementation
- [ ] Snapshot Transfer
- [ ] Replay Logic
- [ ] Integration Tests (100+ tests)

**Month 3: Failover & HA**
- [ ] Automatic Failover
- [ ] Read Replicas
- [ ] Health Checks
- [ ] Chaos Testing (Jepsen-style)

**Deliverables:**
- ✅ High Availability (99.9% uptime)
- ✅ Automatic Failover (<3s RTO)
- ✅ Read Scaling via Replicas

---

### Phase 3: Client SDKs (Q2-Q3 2026 - 2 months)

**Month 1: Python & JavaScript**
- [ ] Python SDK (themis-python)
- [ ] JavaScript SDK (themis-js)
- [ ] Connection Pooling
- [ ] Retry Logic
- [ ] Unit Tests (300+ tests)

**Month 2: Java & Documentation**
- [ ] Java SDK (themis-java)
- [ ] API Documentation
- [ ] Code Examples
- [ ] Integration Tests

**Deliverables:**
- ✅ Python, JS, Java SDKs
- ✅ Published to PyPI, npm, Maven Central
- ✅ Comprehensive Documentation

---

### Phase 4: Admin UI (Q3 2026 - 2 months)

**Month 1: Core UI**
- [ ] React App Setup
- [ ] Query Editor (Monaco)
- [ ] Results Viewer
- [ ] Metrics Dashboard

**Month 2: Advanced Features**
- [ ] Shard Topology Visualization
- [ ] Schema Browser
- [ ] Admin Operations
- [ ] User Management

**Deliverables:**
- ✅ Web-based Admin Console
- ✅ Real-time Metrics
- ✅ Visual Query Builder

---

## 6. Migration Strategy

### 6.1 From Single-Node to Sharded

**Pre-Migration Checklist:**
1. Backup full database
2. Setup etcd cluster (3 nodes)
3. Deploy new shard nodes
4. Configure monitoring

**Migration Steps:**

```bash
# 1. Bootstrap first shard (existing node)
themis-admin init-shard \
  --shard-id=shard_001 \
  --endpoint=themis-node1:8080 \
  --datacenter=dc1

# 2. Add second shard
themis-admin add-shard \
  --shard-id=shard_002 \
  --endpoint=themis-node2:8080 \
  --datacenter=dc1

# 3. Trigger rebalancing
themis-admin rebalance \
  --mode=gradual \
  --max-transfer-rate=100MB/s

# 4. Monitor migration
themis-admin rebalance-status
# Output:
# Shard 001 → 002: 45% complete (12GB / 26GB)
# ETA: 15 minutes

# 5. Verify data integrity
themis-admin verify-shards --checksums
```

**Rollback Plan:**
- Keep old single-node running during migration
- Dual-write to old + new shards
- Atomic cutover in metadata store
- Rollback = flip metadata back to single-node

---

## 7. Risk Assessment

### 7.1 Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Data Loss during Rebalancing** | Medium | Critical | Dual-write + checksums + rollback plan |
| **Raft Consensus Bugs** | Low | Critical | Use proven library (etcd-raft) + extensive testing |
| **Network Partitions** | High | High | Split-brain protection, quorum-based writes |
| **Performance Degradation** | Medium | High | Benchmarks before/after, tuning knobs |
| **SDK Adoption** | Medium | Medium | Comprehensive docs + examples |

### 7.2 Operational Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Increased Ops Complexity** | High | Medium | Admin UI, automated monitoring, runbooks |
| **etcd Cluster Failure** | Low | Critical | etcd HA (3-5 nodes), regular backups |
| **Shard Imbalance** | Medium | Medium | Automated rebalancing, alerting |
| **Version Skew** | Medium | High | Rolling upgrades, backward compatibility |

### 7.3 Business Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Timeline Slip** | High | Medium | Phased rollout, MVP first |
| **Resource Constraints** | Medium | High | Prioritize critical features |
| **Market Competition** | High | Low | Focus on differentiation (Multi-Model + Encryption) |

---

## 8. Success Metrics

### 8.1 Phase 1: Sharding

- ✅ **Scalability:** 10x data capacity (100GB → 1TB)
- ✅ **Throughput:** 10k QPS sustained
- ✅ **Rebalancing:** <1% performance impact during migration
- ✅ **Zero Downtime:** No service interruptions

### 8.2 Phase 2: Replication

- ✅ **Availability:** 99.9% uptime (< 8h downtime/year)
- ✅ **Failover:** <3s RTO (Recovery Time Objective)
- ✅ **Replication Lag:** <100ms (p99)
- ✅ **Data Durability:** 99.999999999% (11 nines via 3x replication)

### 8.3 Phase 3: SDKs

- ✅ **Adoption:** 100+ GitHub stars per SDK
- ✅ **Downloads:** 1000+ per month (PyPI/npm)
- ✅ **Documentation:** 100% API coverage
- ✅ **Examples:** 20+ code samples

### 8.4 Phase 4: Admin UI

- ✅ **User Satisfaction:** >80% positive feedback
- ✅ **Query Editor Usage:** 50% of queries via UI
- ✅ **Ops Efficiency:** 30% reduction in support tickets

---

## 9. Conclusion

**Strategic Imperative:** ThemisDB must evolve from **Feature-Rich Single-Node** to **Enterprise-Ready Distributed System**.

**Investment Required:**
- **Engineering:** ~12-18 months
- **Infrastructure:** etcd cluster, monitoring stack
- **Documentation:** User guides, API docs, runbooks

**Expected ROI:**
- ✅ **Market Fit:** Enterprise customers requiring scale + HA
- ✅ **Competitive Edge:** Multi-Model + Encryption + URN Abstraction
- ✅ **Revenue:** Licensing based on cluster size

**Next Steps:**
1. **Approve Roadmap** - Stakeholder alignment
2. **Resource Allocation** - Assign 2-3 engineers
3. **Phase 1 Kickoff** - Begin URN Sharding implementation

**This roadmap transforms ThemisDB from a promising prototype into a production-grade distributed database.**

