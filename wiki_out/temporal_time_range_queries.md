# Temporal Time-Range Queries

**Status:** ✅ Implemented & Tested (8/8 tests passing)  
**Feature:** Extended temporal graph queries with time-window filtering  
**Date:** 2025-01-15

---

## Overview

This feature extends Themis's temporal graph capabilities from single-timestamp queries to **time-range queries**. You can now find all edges that overlap with or are fully contained within a specified time window.

### Use Cases

- **Audit Queries:** "Show all relationships valid during Q4 2024"
- **Compliance:** "Find edges fully contained within investigation period"
- **Historical Analysis:** "What connections existed between 2020-2022?"
- **Temporal Analytics:** "Relationships overlapping with event timeframe"

---

## API Reference

### TimeRangeFilter Structure

```cpp
struct TimeRangeFilter {
    int64_t start_ms;  // Range start (milliseconds since epoch)
    int64_t end_ms;    // Range end (milliseconds since epoch)
    
    // Factory methods
    static TimeRangeFilter between(int64_t start, int64_t end);
    static TimeRangeFilter since(int64_t start);
    static TimeRangeFilter until(int64_t end);
    static TimeRangeFilter all();
    
    // Filtering methods
    bool hasOverlap(std::optional<int64_t> edge_valid_from, 
                    std::optional<int64_t> edge_valid_to) const;
    bool fullyContains(std::optional<int64_t> edge_valid_from,
                       std::optional<int64_t> edge_valid_to) const;
};
```

### EdgeInfo Structure

```cpp
struct EdgeInfo {
    std::string edgeId;                     // Edge identifier
    std::string fromPk;                     // Source node primary key
    std::string toPk;                       // Target node primary key
    std::optional<int64_t> valid_from;      // Edge valid from (ms)
    std::optional<int64_t> valid_to;        // Edge valid to (ms)
};
```

### Query Methods

#### Global Time-Range Query

```cpp
std::pair<Status, std::vector<EdgeInfo>> 
getEdgesInTimeRange(int64_t range_start_ms, 
                    int64_t range_end_ms,
                    bool require_full_containment = false) const;
```

**Parameters:**
- `range_start_ms`: Query time window start (milliseconds since epoch)
- `range_end_ms`: Query time window end (milliseconds since epoch)
- `require_full_containment`: 
  - `false` (default): Returns edges with **any overlap** with query window
  - `true`: Returns edges **fully contained** within query window

**Returns:** 
- `Status`: Operation success/failure
- `vector<EdgeInfo>`: All matching edges with temporal metadata

**Time Complexity:** O(E) where E = total edges in database

---

#### Node-Specific Time-Range Query

```cpp
std::pair<Status, std::vector<EdgeInfo>>
getOutEdgesInTimeRange(std::string_view fromPk,
                       int64_t range_start_ms,
                       int64_t range_end_ms, 
                       bool require_full_containment = false) const;
```

**Parameters:**
- `fromPk`: Source node primary key
- `range_start_ms`: Query time window start (milliseconds since epoch)
- `range_end_ms`: Query time window end (milliseconds since epoch)
- `require_full_containment`: Same as global query

**Returns:** 
- `Status`: Operation success/failure
- `vector<EdgeInfo>`: All matching outgoing edges from `fromPk`

**Time Complexity:** O(d) where d = out-degree of node

---

## Usage Examples

### Example 1: Overlap Query (Default)

Find all edges with **any overlap** with time window [1000, 2000]:

```cpp
GraphIndexManager graph(db);

// Add edges with different temporal periods
BaseEntity e1("edge1");
e1.setField("_from", "A");
e1.setField("_to", "B");
e1.setField("valid_from", 500);   // Partially overlaps
e1.setField("valid_to", 1500);
graph.addEdge(e1);

BaseEntity e2("edge2");
e2.setField("_from", "A");
e2.setField("_to", "C");
e2.setField("valid_from", 1200);  // Fully inside
e2.setField("valid_to", 1800);
graph.addEdge(e2);

BaseEntity e3("edge3");
e3.setField("_from", "B");
e3.setField("_to", "C");
e3.setField("valid_from", 2500);  // No overlap
e3.setField("valid_to", 3000);
graph.addEdge(e3);

// Query: Find edges overlapping [1000, 2000]
auto [status, edges] = graph.getEdgesInTimeRange(1000, 2000);

// Result: edges = [edge1, edge2]
// edge1: overlaps (500-1500 overlaps with 1000-2000)
// edge2: fully inside (1200-1800 inside 1000-2000)
// edge3: no overlap (2500-3000 is after 2000)
```

---

### Example 2: Full Containment Query

Find edges **fully contained** within time window [1000, 3000]:

```cpp
// Same edges as Example 1

// Query: Find edges FULLY INSIDE [1000, 3000]
auto [status, edges] = graph.getEdgesInTimeRange(1000, 3000, true);

// Result: edges = [edge2, edge3]
// edge1: NOT included (500-1500 starts before 1000)
// edge2: included (1200-1800 fully inside 1000-3000)
// edge3: included (2500-3000 fully inside 1000-3000)
```

---

### Example 3: Node-Specific Time-Range Query

Find outgoing edges from specific node in time window:

```cpp
// Add edges from node "user1"
BaseEntity e1("follow1");
e1.setField("_from", "user1");
e1.setField("_to", "user2");
e1.setField("valid_from", 1000000);
e1.setField("valid_to", 2000000);
graph.addEdge(e1);

BaseEntity e2("follow2");
e2.setField("_from", "user1");
e2.setField("_to", "user3");
e2.setField("valid_from", 1500000);
e2.setField("valid_to", 2500000);
graph.addEdge(e2);

BaseEntity e3("follow3");
e3.setField("_from", "user2");  // Different source!
e3.setField("_to", "user3");
e3.setField("valid_from", 1200000);
e3.setField("valid_to", 1800000);
graph.addEdge(e3);

// Query: Find user1's outgoing edges in [1100000, 1900000]
auto [status, edges] = graph.getOutEdgesInTimeRange("user1", 1100000, 1900000);

// Result: edges = [follow1, follow2]
// follow1: from user1, overlaps query window
// follow2: from user1, overlaps query window
// follow3: NOT included (from user2, not user1)
```

---

### Example 4: Unbounded Edges (Always Valid)

Edges without `valid_from`/`valid_to` match all time queries:

```cpp
BaseEntity unbounded("always_active");
unbounded.setField("_from", "A");
unbounded.setField("_to", "B");
// NO valid_from/valid_to fields = unbounded temporal range
graph.addEdge(unbounded);

BaseEntity bounded("temporary");
bounded.setField("_from", "A");
bounded.setField("_to", "C");
bounded.setField("valid_from", 1000);
bounded.setField("valid_to", 2000);
graph.addEdge(bounded);

// Query: Find edges in [500, 1500]
auto [status, edges] = graph.getEdgesInTimeRange(500, 1500);

// Result: edges = [always_active, temporary]
// always_active: unbounded edges always included
// temporary: 1000-2000 overlaps 500-1500
```

---

## Filtering Semantics

### Overlap vs. Full Containment

**Overlap (`require_full_containment = false`):**
- Default behavior
- Returns edges with **any temporal overlap** with query window
- Includes partially overlapping edges
- Formula: `edge_start <= query_end AND edge_end >= query_start`

**Full Containment (`require_full_containment = true`):**
- Strict containment
- Returns edges **fully inside** query window
- Excludes partially overlapping edges
- Formula: `edge_start >= query_start AND edge_end <= query_end`

### TimeRangeFilter Behavior

| Edge Period | Query Window | hasOverlap() | fullyContains() |
|-------------|--------------|--------------|-----------------|
| [500, 1500] | [1000, 2000] | ✅ true      | ❌ false        |
| [1200, 1800]| [1000, 2000] | ✅ true      | ✅ true         |
| [2500, 3000]| [1000, 2000] | ❌ false     | ❌ false        |
| [null, null]| [1000, 2000] | ✅ true      | ✅ true         |
| [500, null] | [1000, 2000] | ✅ true      | ❌ false        |
| [null, 3000]| [1000, 2000] | ✅ true      | ❌ false        |

**Unbounded Edges:**
- Edges without `valid_from`/`valid_to` are treated as **unbounded** (always valid)
- `hasOverlap()` always returns `true` for unbounded edges
- `fullyContains()` always returns `true` for unbounded edges

---

## Performance Characteristics

### Global Query: `getEdgesInTimeRange()`

- **Time Complexity:** O(E) where E = total edges in database
- **Space Complexity:** O(R) where R = number of matching edges
- **Database Scans:** Full scan of `graph:out:*` prefix
- **Entity Loads:** One `db.get("edge:*")` per edge

**Optimization Opportunities:**
- Add temporal index for bounded time ranges
- Sorted temporal B-tree for range scans
- Materialized views for common time windows

### Node-Specific Query: `getOutEdgesInTimeRange()`

- **Time Complexity:** O(d) where d = out-degree of source node
- **Space Complexity:** O(R) where R = number of matching edges
- **Database Scans:** Prefix scan of `graph:out:<fromPk>:*`
- **Entity Loads:** One `db.get("edge:*")` per outgoing edge

**Much Faster Than Global Query:**
- Only scans edges from specific node
- Leverages existing `graph:out:` adjacency index
- Suitable for high-frequency queries on specific nodes

---

## Implementation Details

### Key Schema

```
# Edge entity storage
edge:<edge_id> -> BaseEntity(id, _from, _to, valid_from, valid_to, ...)

# Graph adjacency indices (temporal data stored in entity, not index)
graph:out:<from_pk>:<edge_id> -> <to_pk>
graph:in:<to_pk>:<edge_id> -> <from_pk>
```

**Design Choice:**
- Temporal fields (`valid_from`, `valid_to`) stored in edge entity, **not** in index keys
- Requires entity load to check temporal bounds
- Simplifies index structure (no temporal key encoding)
- Trade-off: Extra `db.get()` per edge vs. complex temporal index

### Algorithm (getEdgesInTimeRange)

```
1. Create TimeRangeFilter from query parameters
2. Scan all edges with prefix "graph:out:"
3. For each edge key "graph:out:<from>:<edgeId>":
   a. Parse edgeId from key
   b. Load edge entity from "edge:<edgeId>"
   c. Extract valid_from, valid_to fields
   d. Check temporal match (overlap or containment)
   e. If match, add EdgeInfo to results
4. Return filtered results
```

### Algorithm (getOutEdgesInTimeRange)

```
1. Create TimeRangeFilter from query parameters
2. Scan edges with prefix "graph:out:<fromPk>:"
3. For each edge (same as global query):
   a-e. (identical to global query)
4. Return filtered results
```

---

## Testing

### Test Coverage (8/8 Passing)

1. **TimeRangeFilter_Overlap** - Filter logic: overlap detection
2. **TimeRangeFilter_FullContainment** - Filter logic: containment check
3. **GetEdgesInTimeRange_Overlap** - Global query: overlap mode
4. **GetEdgesInTimeRange_FullContainment** - Global query: containment mode
5. **GetOutEdgesInTimeRange** - Node-specific query: basic functionality
6. **GetOutEdgesInTimeRange_NoMatch** - Node-specific query: no results
7. **UnboundedEdges_AlwaysIncluded** - Unbounded edges match all queries
8. **EdgeInfo_ContainsTemporalData** - Result structure validation

### Test File

```bash
# Run all time-range tests
./themis_tests --gtest_filter="TimeRangeQueryTest.*"

# Expected output:
# [  PASSED  ] 8 tests.
```

---

## Integration with Existing Features

### Works With Recursive Path Queries

```cpp
// Step 1: Find temporal path
RecursivePathQuery rpq;
rpq.start_node = "user1";
rpq.end_node = "user5";
rpq.max_depth = 3;
rpq.valid_from = 1500000;  // Single timestamp
rpq.valid_to = 1500000;
auto [status, path] = queryEngine.executeRecursivePathQuery(rpq);

// Step 2: Verify all edges in path valid during time window
auto [st, edges] = graph.getEdgesInTimeRange(1400000, 1600000);
for (const auto& edgeInfo : edges) {
    // Check if edge in path is valid throughout window
}
```

### Temporal Graph Capabilities

| Feature | Single Timestamp | Time Range | Status |
|---------|-----------------|------------|--------|
| BFS/Dijkstra at time T | ✅ `bfsAtTime()` | ❌ | Implemented |
| Shortest path at time T | ✅ `dijkstraAtTime()` | ❌ | Implemented |
| Find edges in window | ❌ | ✅ `getEdgesInTimeRange()` | Implemented ✨ |
| Find node edges in window | ❌ | ✅ `getOutEdgesInTimeRange()` | Implemented ✨ |
| Temporal aggregation | ❌ | ❌ | Future work |

---

## Future Enhancements

### 1. Temporal Index

**Problem:** O(E) scan for global queries  
**Solution:** B-tree index on `(valid_from, valid_to)` pairs

```cpp
// Hypothetical API
auto edges = graph.getEdgesInTimeRange_Indexed(1000, 2000);
// Time complexity: O(log E + R) vs. current O(E)
```

### 2. Time-Window Path Queries

**Problem:** Current path queries use single timestamp  
**Solution:** Extend `RecursivePathQuery` with time windows

```cpp
RecursivePathQuery rpq;
rpq.window_start = 1000000;
rpq.window_end = 2000000;
// Find paths where ALL edges valid during [1000000, 2000000]
```

### 3. Temporal Aggregations

**Problem:** No aggregate queries over time windows  
**Solution:** Add temporal statistics

```cpp
auto stats = graph.getTemporalStats(1000, 2000);
// { edge_count, avg_duration, node_degree_distribution, ... }
```

### 4. Streaming Time-Range Queries

**Problem:** Large result sets exhaust memory  
**Solution:** Iterator-based API

```cpp
auto iter = graph.streamEdgesInTimeRange(1000, 2000);
while (iter.hasNext()) {
    EdgeInfo edge = iter.next();
    // Process one edge at a time
}
```

---

## Known Limitations

1. **No Temporal Index:** Global queries scan all edges (O(E))
2. **Entity Load Overhead:** One `db.get()` per edge (network/disk I/O)
3. **No Streaming API:** Large result sets loaded into memory
4. **No Temporal Joins:** Cannot join time-range results with other queries
5. **No Unbounded Query Optimization:** Unbounded edges checked even when range is bounded

---

## Changelog

- **2025-01-15:** Initial implementation
  - Added `TimeRangeFilter` structure to `temporal_graph.h`
  - Added `EdgeInfo` structure to `graph_index.h`
  - Implemented `getEdgesInTimeRange()` in `graph_index.cpp`
  - Implemented `getOutEdgesInTimeRange()` in `graph_index.cpp`
  - Created 8 comprehensive tests (all passing)
  - Documentation created

---

## See Also

- [Recursive Path Queries](./recursive_path_queries.md) - Multi-hop temporal reasoning
- [Temporal Graph Design](./temporal_graph.md) - Overall temporal architecture
- [Graph Index](./indexes.md#graph-index) - Adjacency index design
- [MVCC Design](./mvcc_design.md) - Transaction temporal semantics
