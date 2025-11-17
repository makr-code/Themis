# Temporal Graphs - Themis

## Overview

Themis' Temporal Graph implementation adds time-awareness to graph edges, enabling historical queries and point-in-time graph traversals. This is critical for tracking relationship evolution, knowledge graph versioning, and time-series network analysis.

**Key Features:**
- **Temporal Edges**: Edges with `valid_from` and `valid_to` timestamps
- **Point-in-Time Queries**: Traverse graph as it existed at specific timestamp
- **Historical Analysis**: Track how relationships changed over time
- **Flexible Validity**: Unbounded intervals supported (null = forever/since beginning)
- **Efficient Filtering**: Temporal checks integrated into BFS/Dijkstra algorithms

---

## Architecture

### Data Model

**Temporal Edge Schema:**
```cpp
struct Edge {
    std::string id;
    std::string _from;              // Source node
    std::string _to;                // Target node
    double _weight = 1.0;           // Edge weight for pathfinding
    
    // Temporal fields (optional)
    std::optional<int64_t> valid_from;  // Start of validity (ms since epoch)
    std::optional<int64_t> valid_to;    // End of validity (ms since epoch)
}
```

**Validity Semantics:**
- `valid_from = null`: Edge valid since beginning of time
- `valid_to = null`: Edge valid indefinitely into future
- `valid_from = T1, valid_to = T2`: Edge valid during interval [T1, T2]
- Both `null`: Edge always valid (eternal)

**Temporal Filter Logic:**
```cpp
bool isValid(query_timestamp, valid_from, valid_to) {
    if (valid_from.has_value() && query_timestamp < valid_from) {
        return false;  // Not yet valid
    }
    if (valid_to.has_value() && query_timestamp > valid_to) {
        return false;  // No longer valid
    }
    return true;  // Valid at query time
}
```

---

## API Reference

### C++ API

#### 1. BFS At Time

Breadth-first search with temporal filtering.

**Signature:**
```cpp
std::pair<Status, std::vector<std::string>> bfsAtTime(
    std::string_view startPk,
    int64_t timestamp_ms,
    int maxDepth = 3
) const;
```

**Parameters:**
- `startPk`: Starting node primary key
- `timestamp_ms`: Query timestamp (milliseconds since epoch)
- `maxDepth`: Maximum traversal depth

**Returns:**
- `Status`: Success/error status
- `std::vector<std::string>`: Reachable nodes (BFS order)

**Example:**
```cpp
// Query: Which nodes could Alice reach in January 2023?
auto [st, nodes] = graph_mgr->bfsAtTime("Alice", 1672531200000, 5);
if (st.ok) {
    for (const auto& node : nodes) {
        std::cout << "Reachable: " << node << "\n";
    }
}
```

#### 2. Dijkstra At Time

Shortest path with temporal filtering.

**Signature:**
```cpp
std::pair<Status, PathResult> dijkstraAtTime(
    std::string_view startPk,
    std::string_view targetPk,
    int64_t timestamp_ms
) const;
```

**Returns:**
- `PathResult.path`: Nodes from start to target
- `PathResult.totalCost`: Total path cost (sum of weights)

**Example:**
```cpp
// Query: Shortest path from Alice to CompanyX in 2022?
auto [st, path] = graph_mgr->dijkstraAtTime("Alice", "CompanyX", 1640995200000);
if (st.ok) {
    std::cout << "Path cost: " << path.totalCost << "\n";
    for (const auto& node : path.path) {
        std::cout << node << " -> ";
    }
}
```

### AQL Integration (Future)

Planned AQL syntax for temporal queries:

```aql
// Find all documents cited by Doc1 in 2022
FOR v IN 1..3 OUTBOUND 'Doc1' citations
    FILTER e.valid_from <= @timestamp AND e.valid_to >= @timestamp
    RETURN v

// Shortest path at specific time
FOR p IN SHORTEST_PATH 'Alice' TO 'CompanyX' GRAPH employment_graph
    FILTER PATH.ALL(e, e.valid_from <= @timestamp AND e.valid_to >= @timestamp)
    RETURN p
```

---

## Test Validation (30.10.2025)

### Test Suite: 18 Tests - ALL PASSED ✅

#### Unit Tests (TemporalFilter)

**Test 1: NoFilter_AcceptsAll** ✅
- Filter with `timestamp = null` accepts all edges
- Validates unbounded interval support

**Test 2: WithTimestamp_FiltersCorrectly** ✅
- Edges before `valid_from` → rejected
- Edges after `valid_to` → rejected
- Edges during validity period → accepted
- Unbounded intervals handled correctly

**Test 3: BoundaryConditions** ✅
- Query at exact `valid_from` → accepted
- Query at exact `valid_to` → accepted
- Edge valid only at query time → accepted

#### BFS Temporal Tests

**Test 4: NoTemporalEdges_ReturnsAllNeighbors** ✅
- Graph without temporal constraints behaves normally
- All nodes reachable regardless of timestamp

**Test 5: FiltersByValidFrom** ✅
- Edge A→B valid from 2022 onwards
- Query at 2021: Only A reachable
- Query at 2023: A→B→C reachable

**Test 6: FiltersByValidTo** ✅
- Edge A→B valid until 2022
- Query at 2021: Full graph accessible
- Query at 2023: A isolated (edge expired)

**Test 7: FiltersByValidRange** ✅
- Edge A→B valid from 2021 to 2023
- Query at 2020: A isolated
- Query at 2022: Full graph
- Query at 2024: A isolated

**Test 8: MultiplePathsOverTime** ✅
- Complex scenario: paths change over time
- Period 2020-2021: A→B→D
- Period 2022-2023: A→C→D
- Period 2024+: Both paths active

**Test 9: IsolatedNodeAfterExpiration** ✅
- All outgoing edges expire
- Node becomes isolated after expiration time

#### Dijkstra Temporal Tests

**Test 10: FindsShortestPathAtTime** ✅
- Two paths: A→B→D (cost 2) and A→C→D (cost 6)
- Before C→D becomes valid: uses A→B→D
- After both paths valid: still uses shorter path

**Test 11: PathChangesOverTime** ✅
- Path A→B→D valid 2020-2022 (cost 3)
- Path A→C→D valid 2023+ (cost 2)
- Algorithm correctly switches to cheaper path when available

**Test 12: NoPathAtTime** ✅
- All edges to target expired
- Returns error: "Kein Pfad gefunden"

#### Edge Cases

**Test 13-15: Input Validation** ✅
- Empty node names → error
- Negative depth → error
- Proper error messages returned

**Test 16: MaxDepthZero_ReturnsOnlyStart** ✅
- Depth limit of 0 returns only starting node

#### Real-World Scenarios

**Test 17: EmploymentHistory** ✅
- Models: Alice worked at CompanyA (2020-2022), CompanyB (2023+)
- Query 2021: Alice→CompanyA
- Query 2023: Alice→CompanyB

**Test 18: KnowledgeGraphEvolution** ✅
- Document citation network evolves over time
- Citations added/retracted
- Historical queries return correct citation graph state

---

## Use Cases

### 1. Employment/Organizational Networks

Track employee-company relationships over time:

```cpp
// Create temporal employment edge
auto e1 = createTemporalEdge(
    "emp1",
    "Alice",
    "CompanyA",
    toTimestamp(2020, 1, 1),  // started Jan 2020
    toTimestamp(2022, 12, 31) // ended Dec 2022
);
graph_mgr->addEdge(e1);

// Query: Where did Alice work in 2021?
auto [st, nodes] = graph_mgr->bfsAtTime("Alice", toTimestamp(2021, 6, 1), 1);
// Returns: ["Alice", "CompanyA"]
```

### 2. Knowledge Graph Versioning

Track evolving knowledge and citations:

```cpp
// Citation retracted in 2023
auto cite = createTemporalEdge(
    "cite1",
    "Paper1",
    "Paper2",
    toTimestamp(2020, 1, 1),
    toTimestamp(2023, 1, 1)  // retracted
);

// Query: What did Paper1 cite in 2021?
auto [st, citations] = graph_mgr->bfsAtTime("Paper1", toTimestamp(2021, 1, 1), 1);
// Returns: ["Paper1", "Paper2"]

// Query: What does Paper1 cite in 2024?
auto [st2, citations2] = graph_mgr->bfsAtTime("Paper1", toTimestamp(2024, 1, 1), 1);
// Returns: ["Paper1"] (citation retracted)
```

### 3. Social Network Evolution

Model friendships, follows, and connections over time:

```cpp
// Alice followed Bob from 2020-2022, then unfollowed
auto follow = createTemporalEdge(
    "follow1",
    "Alice",
    "Bob",
    toTimestamp(2020, 1, 1),
    toTimestamp(2022, 12, 31)
);

// Query: Who could Alice reach in 2021?
auto [st, network] = graph_mgr->bfsAtTime("Alice", toTimestamp(2021, 1, 1), 2);
// Returns: Alice's network including Bob

// Query: Who can Alice reach in 2023?
auto [st2, network2] = graph_mgr->bfsAtTime("Alice", toTimestamp(2023, 1, 1), 2);
// Returns: Alice's network excluding Bob
```

### 4. Infrastructure/Network Changes

Track network topology changes:

```cpp
// Router1 -> Router2 link active 2020-2022
// Router1 -> Router3 link active 2023+
// Query shortest path at different times
auto [st1, path1] = graph_mgr->dijkstraAtTime("Router1", "Server1", toTimestamp(2021, 1, 1));
auto [st2, path2] = graph_mgr->dijkstraAtTime("Router1", "Server1", toTimestamp(2024, 1, 1));
// Paths differ based on network topology at query time
```

---

## Performance Considerations

### Current Implementation

**Time Complexity:**
- BFS at time: O(V + E * T) where T = edge load time
- Dijkstra at time: O((V + E) * log V * T)

**Edge Load Overhead:**
Each edge requires:
1. RocksDB Get (~1-2ms for SSD)
2. Deserialization (~0.1ms)
3. Temporal filter check (~0.001ms)

**Optimization Strategies:**

1. **Batch Edge Loading:**
   ```cpp
   // Instead of individual Gets, use MultiGet
   std::vector<std::string> edge_keys;
   for (auto& adj : adjacency) {
       edge_keys.push_back(makeGraphEdgeKey(adj.edgeId));
   }
   auto edges = db_.multiGet(edge_keys);  // Single batch call
   ```

2. **Temporal Index:**
   ```cpp
   // Secondary index: valid_at_timestamp -> [edge_ids]
   // Enables fast "give me all edges valid at time T"
   Key: "temporal_index:2023-01-01:edge1" -> ""
   ```

3. **Caching:**
   ```cpp
   // Cache edge validity info (avoid deserialization)
   struct EdgeValidityCache {
       std::string edge_id;
       std::optional<int64_t> valid_from;
       std::optional<int64_t> valid_to;
   };
   ```

---

## Limitations & Future Enhancements

### Current Limitations

1. **No Temporal Aggregation:** Cannot query "How many times did this edge exist?"
2. **No Event Streams:** Cannot subscribe to temporal changes
3. **Single Timestamp Queries:** No interval queries (e.g., "valid anytime during 2020-2022")
4. **No Temporal Joins:** Cannot correlate temporal patterns across graphs

### Planned Enhancements (Sprint C+)

1. **Temporal Range Queries:**
   ```cpp
   // Find all edges that were ever valid during interval
   auto edges = graph_mgr->getEdgesValidDuring(t_start, t_end);
   ```

2. **Temporal Aggregations:**
   ```cpp
   // How long was this edge valid?
   auto duration = graph_mgr->getTotalValidDuration(edge_id);
   ```

3. **AQL Integration:**
   ```aql
   FOR v IN 1..3 OUTBOUND 'Doc1' citations
       OPTIONS {timestamp: @query_time}
       RETURN v
   ```

4. **Temporal Predicates:**
   ```aql
   // Find nodes reachable at ANY point during 2022
   FOR v IN 1..3 OUTBOUND 'Alice' friends
       FILTER e.valid_from <= '2022-12-31' AND e.valid_to >= '2022-01-01'
       RETURN DISTINCT v
   ```

5. **Change Stream:**
   ```cpp
   // Subscribe to temporal edge changes
   graph_mgr->watchTemporalChanges(callback);
   ```

---

## Implementation Notes

### Temporal Filter

Located in `include/index/temporal_graph.h`:

```cpp
struct TemporalFilter {
    std::optional<int64_t> timestamp_ms;
    
    bool isValid(std::optional<int64_t> valid_from, 
                 std::optional<int64_t> valid_to) const {
        if (!timestamp_ms.has_value()) return true;  // No filter
        
        int64_t t = *timestamp_ms;
        if (valid_from.has_value() && t < *valid_from) return false;
        if (valid_to.has_value() && t > *valid_to) return false;
        return true;
    }
    
    static TemporalFilter now();
    static TemporalFilter at(int64_t timestamp_ms);
    static TemporalFilter all();  // No temporal filtering
};
```

### BFS Implementation

Located in `src/index/graph_index.cpp`:

```cpp
std::pair<Status, std::vector<std::string>> 
GraphIndexManager::bfsAtTime(std::string_view startPk, 
                             int64_t timestamp_ms, 
                             int maxDepth) const {
    TemporalFilter filter = TemporalFilter::at(timestamp_ms);
    
    // Standard BFS with temporal edge filtering
    for (const auto& info : adjacency) {
        // Load edge to check validity
        BaseEntity edge = BaseEntity::deserialize(edgeKey, *blob);
        
        std::optional<int64_t> valid_from = edge.getFieldAsInt("valid_from");
        std::optional<int64_t> valid_to = edge.getFieldAsInt("valid_to");
        
        if (!filter.isValid(valid_from, valid_to)) {
            continue;  // Skip invalid edge
        }
        
        // Process valid neighbor
        // ...
    }
}
```

---

## Testing

### Run Temporal Graph Tests

```bash
# Run all temporal graph tests
cd build
.\Release\themis_tests.exe --gtest_filter="TemporalGraphTest.*"

# Expected output:
# [==========] Running 18 tests from 1 test suite.
# [  PASSED  ] 18 tests.
```

### Test Coverage

- ✅ TemporalFilter logic (3 tests)
- ✅ BFS temporal traversal (6 tests)
- ✅ Dijkstra shortest path (3 tests)
- ✅ Edge cases & validation (3 tests)
- ✅ Real-world scenarios (3 tests)

**Total: 18 tests, 100% passing**

---

## Configuration

### Enable Temporal Graph (Default: Enabled)

No special configuration required - temporal edges work alongside regular edges.

**Optional: Temporal Index (Future):**
```json
{
  "graph": {
    "temporal_index": true,
    "temporal_cache_size_mb": 256
  }
}
```

---

## Summary

**Sprint B - Temporal Graphs: ✅ PRODUCTION READY**

- ✅ Temporal edge support with `valid_from`/`valid_to`
- ✅ BFS at time implementation
- ✅ Dijkstra at time implementation
- ✅ 18 comprehensive Google Tests (all passing)
- ✅ Real-world scenario validation
- ✅ Documentation complete

**Use Cases Validated:**
- Employment history tracking
- Knowledge graph versioning
- Social network evolution
- Infrastructure change management

**Next Steps:**
- Temporal range queries
- AQL integration
- Temporal aggregations
- Change streams
