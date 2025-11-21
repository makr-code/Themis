# Property Graph Model & Multi-Graph Federation

**Status:** ✅ Implemented & Tested (13/13 tests passing)  
**Feature:** Property Graph Model with Node Labels, Relationship Types, and Multi-Graph Federation  
**Date:** 2025-01-15

---

## Overview

This feature extends Themis's graph capabilities with **Property Graph Model** semantics and **Multi-Graph Federation**. You can now:

- Assign **multiple labels** to nodes (e.g., `:Person`, `:Employee`)
- Define **typed relationships** (e.g., `FOLLOWS`, `LIKES`, `REPORTS_TO`)
- Manage **multiple isolated graphs** with cross-graph queries
- Perform **federated pattern matching** across graphs

### Use Cases

- **Social Networks:** `:Person -[FOLLOWS]-> :Person`, `:User -[LIKES]-> :Post`
- **Knowledge Graphs:** `:Entity -[RELATES_TO]-> :Entity`, `:Concept -[IS_A]-> :Category`
- **Enterprise Graphs:** `:Employee -[REPORTS_TO]-> :Manager`, `:Department -[CONTAINS]-> :Team`
- **Multi-Tenant Systems:** Separate graphs per tenant with cross-tenant analytics

---

## Architecture

### Key Schema Design

```
# Nodes (with labels)
node:<graph_id>:<pk> -> BaseEntity(id, name, _labels, ...)

# Edges (with types)
edge:<graph_id>:<edge_id> -> BaseEntity(id, _from, _to, _type, ...)

# Label Index (for fast label-based queries)
label:<graph_id>:<label>:<pk> -> (empty)

# Type Index (for fast type-based queries)
type:<graph_id>:<type>:<edge_id> -> (empty)

# Graph Adjacency Indices (federated)
graph:out:<graph_id>:<from_pk>:<edge_id> -> <to_pk>
graph:in:<graph_id>:<to_pk>:<edge_id> -> <from_pk>
```

**Design Principles:**
- **Graph Isolation:** `graph_id` prefix ensures complete isolation between graphs
- **Label Multiplicity:** Nodes can have 0+ labels (stored as comma-separated string)
- **Type Singularity:** Edges have exactly one type (or none)
- **Index Efficiency:** Separate indices for labels/types enable O(N_label)/O(E_type) queries

---

## API Reference

### Property Graph Manager

```cpp
#include "index/property_graph.h"

PropertyGraphManager pgm(db);
```

### Node Label Operations

#### Add Node with Labels

```cpp
Status addNode(const BaseEntity& node, std::string_view graph_id = "default");
```

**Parameters:**
- `node`: BaseEntity with `_labels` field (comma-separated string)
- `graph_id`: Graph identifier (default: "default")

**Returns:** Status (ok/error)

**Example:**
```cpp
BaseEntity alice("alice");
alice.setField("id", "alice");
alice.setField("name", "Alice Smith");
alice.setField("age", 30);
alice.setField("_labels", "Person,Employee,Manager");

auto st = pgm.addNode(alice, "corporate");
// Creates 3 label index entries:
// - label:corporate:Person:alice
// - label:corporate:Employee:alice
// - label:corporate:Manager:alice
```

---

#### Add Label to Existing Node

```cpp
Status addNodeLabel(std::string_view pk, std::string_view label, 
                    std::string_view graph_id = "default");
```

**Example:**
```cpp
auto st = pgm.addNodeLabel("alice", "Director", "corporate");
// Updates node: _labels = "Person,Employee,Manager,Director"
// Creates index: label:corporate:Director:alice
```

---

#### Remove Label from Node

```cpp
Status removeNodeLabel(std::string_view pk, std::string_view label,
                       std::string_view graph_id = "default");
```

**Example:**
```cpp
auto st = pgm.removeNodeLabel("alice", "Employee", "corporate");
// Updates node: _labels = "Person,Manager,Director"
// Deletes index: label:corporate:Employee:alice
```

---

#### Query Nodes by Label

```cpp
std::pair<Status, std::vector<std::string>> 
getNodesByLabel(std::string_view label, std::string_view graph_id = "default") const;
```

**Returns:** Vector of primary keys matching label

**Time Complexity:** O(N_label) where N_label = nodes with label

**Example:**
```cpp
auto [st, people] = pgm.getNodesByLabel("Person", "corporate");
// Result: ["alice", "bob", "charlie", ...]
```

---

#### Check if Node Has Label

```cpp
std::pair<Status, bool> 
hasNodeLabel(std::string_view pk, std::string_view label,
             std::string_view graph_id = "default") const;
```

**Example:**
```cpp
auto [st, hasLabel] = pgm.hasNodeLabel("alice", "Manager", "corporate");
// Result: true (alice is a Manager)
```

---

### Relationship Type Operations

#### Add Edge with Type

```cpp
Status addEdge(const BaseEntity& edge, std::string_view graph_id = "default");
```

**Parameters:**
- `edge`: BaseEntity with `_from`, `_to`, `_type` fields
- `graph_id`: Graph identifier

**Example:**
```cpp
BaseEntity follows("follows_1");
follows.setField("id", "follows_1");
follows.setField("_from", "alice");
follows.setField("_to", "bob");
follows.setField("_type", "FOLLOWS");
follows.setField("since", 2020);
follows.setField("strength", 0.8);

auto st = pgm.addEdge(follows, "social");
// Creates indices:
// - edge:social:follows_1 -> BaseEntity
// - graph:out:social:alice:follows_1 -> bob
// - graph:in:social:bob:follows_1 -> alice
// - type:social:FOLLOWS:follows_1 -> (empty)
```

---

#### Query Edges by Type

```cpp
struct EdgeInfo {
    std::string edgeId;
    std::string fromPk;
    std::string toPk;
    std::string type;
    std::string graph_id;
};

std::pair<Status, std::vector<EdgeInfo>>
getEdgesByType(std::string_view type, std::string_view graph_id = "default") const;
```

**Returns:** All edges with specified type

**Time Complexity:** O(E_type) where E_type = edges with type

**Example:**
```cpp
auto [st, followsEdges] = pgm.getEdgesByType("FOLLOWS", "social");
// Result: [
//   {edgeId: "follows_1", fromPk: "alice", toPk: "bob", type: "FOLLOWS"},
//   {edgeId: "follows_2", fromPk: "bob", toPk: "charlie", type: "FOLLOWS"},
//   ...
// ]
```

---

#### Query Typed Outgoing Edges from Node

```cpp
std::pair<Status, std::vector<EdgeInfo>>
getTypedOutEdges(std::string_view fromPk, std::string_view type,
                 std::string_view graph_id = "default") const;
```

**Returns:** Outgoing edges from node with specified type

**Time Complexity:** O(d_type) where d_type = out-degree for type

**Example:**
```cpp
auto [st, aliceFollows] = pgm.getTypedOutEdges("alice", "FOLLOWS", "social");
// Result: All FOLLOWS edges originating from alice
```

---

### Multi-Graph Federation

#### List All Graphs

```cpp
std::pair<Status, std::vector<std::string>> listGraphs() const;
```

**Example:**
```cpp
auto [st, graphs] = pgm.listGraphs();
// Result: ["default", "social", "corporate", "knowledge"]
```

---

#### Get Graph Statistics

```cpp
struct GraphStats {
    std::string graph_id;
    size_t node_count;
    size_t edge_count;
    size_t label_count;
    size_t type_count;
};

std::pair<Status, GraphStats> getGraphStats(std::string_view graph_id) const;
```

**Example:**
```cpp
auto [st, stats] = pgm.getGraphStats("social");
// Result: {
//   graph_id: "social",
//   node_count: 1500,
//   edge_count: 8200,
//   label_count: 5,  // Person, Post, Comment, Tag, Group
//   type_count: 7    // FOLLOWS, LIKES, COMMENTS, TAGGED, MEMBER_OF, ...
// }
```

---

#### Federated Pattern Matching

```cpp
struct FederationPattern {
    std::string graph_id;
    std::string label_or_type;  // Node label or edge type
    std::string pattern_type;   // "node" or "edge"
};

struct FederationResult {
    std::vector<NodeInfo> nodes;
    std::vector<EdgeInfo> edges;
};

std::pair<Status, FederationResult>
federatedQuery(const std::vector<FederationPattern>& patterns) const;
```

**Example:**
```cpp
// Find all Person nodes in social graph and Employee nodes in corporate graph
// Plus all FOLLOWS edges in social and REPORTS_TO edges in corporate
std::vector<PropertyGraphManager::FederationPattern> patterns = {
    {"social", "Person", "node"},
    {"corporate", "Employee", "node"},
    {"social", "FOLLOWS", "edge"},
    {"corporate", "REPORTS_TO", "edge"}
};

auto [st, result] = pgm.federatedQuery(patterns);
// Result: {
//   nodes: [NodeInfo{pk: "alice", labels: ["Person"], graph_id: "social"}, ...],
//   edges: [EdgeInfo{edgeId: "follows_1", type: "FOLLOWS", ...}, ...]
// }
```

---

### Batch Operations

#### Add Multiple Nodes (Atomic)

```cpp
Status addNodesBatch(const std::vector<BaseEntity>& nodes,
                     std::string_view graph_id = "default");
```

**Example:**
```cpp
std::vector<BaseEntity> people;
for (int i = 0; i < 1000; ++i) {
    BaseEntity person("person_" + std::to_string(i));
    person.setField("id", "person_" + std::to_string(i));
    person.setField("_labels", "Person");
    people.push_back(person);
}

auto st = pgm.addNodesBatch(people, "social");
// Atomic: All 1000 nodes + label indices added in one transaction
```

---

#### Add Multiple Edges (Atomic)

```cpp
Status addEdgesBatch(const std::vector<BaseEntity>& edges,
                     std::string_view graph_id = "default");
```

**Example:**
```cpp
std::vector<BaseEntity> relationships;
for (int i = 0; i < 100; ++i) {
    BaseEntity follows("follows_" + std::to_string(i));
    follows.setField("id", "follows_" + std::to_string(i));
    follows.setField("_from", "person_" + std::to_string(i));
    follows.setField("_to", "person_" + std::to_string(i + 1));
    follows.setField("_type", "FOLLOWS");
    relationships.push_back(follows);
}

auto st = pgm.addEdgesBatch(relationships, "social");
// Atomic: All 100 edges + type/adjacency indices added in one transaction
```

---

## Usage Examples

### Example 1: Social Network Graph

```cpp
PropertyGraphManager pgm(db);

// Create Person nodes
BaseEntity alice("alice");
alice.setField("id", "alice");
alice.setField("name", "Alice");
alice.setField("_labels", "Person,Influencer");
pgm.addNode(alice, "social");

BaseEntity bob("bob");
bob.setField("id", "bob");
bob.setField("name", "Bob");
bob.setField("_labels", "Person");
pgm.addNode(bob, "social");

// Create typed relationships
BaseEntity follows("follows_1");
follows.setField("id", "follows_1");
follows.setField("_from", "alice");
follows.setField("_to", "bob");
follows.setField("_type", "FOLLOWS");
follows.setField("since", 2020);
pgm.addEdge(follows, "social");

BaseEntity likes("likes_1");
likes.setField("id", "likes_1");
likes.setField("_from", "bob");
likes.setField("_to", "alice");
likes.setField("_type", "LIKES");
pgm.addEdge(likes, "social");

// Query: Find all Influencers
auto [st1, influencers] = pgm.getNodesByLabel("Influencer", "social");
// Result: ["alice"]

// Query: Find all FOLLOWS relationships
auto [st2, followsEdges] = pgm.getEdgesByType("FOLLOWS", "social");
// Result: [{edgeId: "follows_1", fromPk: "alice", toPk: "bob", ...}]

// Query: Who does alice follow?
auto [st3, aliceFollows] = pgm.getTypedOutEdges("alice", "FOLLOWS", "social");
// Result: [{toPk: "bob", ...}]
```

---

### Example 2: Enterprise Org Chart

```cpp
// Corporate graph with Employee-Manager hierarchy
BaseEntity emp1("emp1");
emp1.setField("id", "emp1");
emp1.setField("name", "John Doe");
emp1.setField("_labels", "Employee,Developer");
pgm.addNode(emp1, "corporate");

BaseEntity emp2("emp2");
emp2.setField("id", "emp2");
emp2.setField("name", "Jane Smith");
emp2.setField("_labels", "Employee,Manager");
pgm.addNode(emp2, "corporate");

BaseEntity reports("reports_1");
reports.setField("id", "reports_1");
reports.setField("_from", "emp1");
reports.setField("_to", "emp2");
reports.setField("_type", "REPORTS_TO");
pgm.addEdge(reports, "corporate");

// Query: Find all Managers
auto [st, managers] = pgm.getNodesByLabel("Manager", "corporate");
// Result: ["emp2"]

// Query: Find reporting structure
auto [st2, reportingEdges] = pgm.getEdgesByType("REPORTS_TO", "corporate");
// Result: [{fromPk: "emp1", toPk: "emp2", type: "REPORTS_TO"}]
```

---

### Example 3: Cross-Graph Federation

```cpp
// Setup social graph
BaseEntity alice_social("alice");
alice_social.setField("id", "alice");
alice_social.setField("_labels", "Person");
pgm.addNode(alice_social, "social");

// Setup corporate graph
BaseEntity alice_corp("alice");
alice_corp.setField("id", "alice");
alice_corp.setField("_labels", "Employee");
pgm.addNode(alice_corp, "corporate");

// Federated query: Find Person in social AND Employee in corporate
std::vector<PropertyGraphManager::FederationPattern> patterns = {
    {"social", "Person", "node"},
    {"corporate", "Employee", "node"}
};

auto [st, result] = pgm.federatedQuery(patterns);
// Result combines data from both graphs:
// nodes: [
//   {pk: "alice", labels: ["Person"], graph_id: "social"},
//   {pk: "alice", labels: ["Employee"], graph_id: "corporate"}
// ]
```

---

## Performance Characteristics

### Label Queries

- **Time Complexity:** O(N_label) where N_label = nodes with label
- **Space Complexity:** O(N_label × L) where L = avg labels per node
- **Index Structure:** Prefix scan on `label:<graph_id>:<label>:*`

**Optimization:**
- Labels stored as comma-separated string (trade-off: compact vs. array parsing)
- Label index enables fast `getNodesByLabel()` queries
- Multi-label nodes create multiple index entries (denormalized)

### Type Queries

- **Time Complexity:** O(E_type) where E_type = edges with type
- **Space Complexity:** O(E_type)
- **Index Structure:** Prefix scan on `type:<graph_id>:<type>:*`

**Optimization:**
- Type index enables fast `getEdgesByType()` queries
- Server-side type filtering during traversal ✅ (BFS/Dijkstra/RPQ)

### Multi-Graph Operations

- **Graph Isolation:** O(1) via `graph_id` prefix
- **List Graphs:** O(N) where N = total nodes (full scan to extract graph_ids)
- **Graph Stats:** O(N + E + L + T) for counts (prefix scans)
- **Federation:** O(P × (N_p + E_p)) where P = patterns, N_p/E_p = matches per pattern

**Optimization Opportunities:**
- Maintain graph metadata index (graph registry)
- Cache graph stats in memory
- Parallel federation queries (concurrent pattern matching)

---

## Cypher-Like Query Examples

While Themis doesn't support Cypher syntax directly, here's how to express common patterns:

### Pattern: `MATCH (p:Person) RETURN p`

```cpp
auto [st, people] = pgm.getNodesByLabel("Person");
for (const auto& pk : people) {
    // Load full node entity if needed
    std::string nodeKey = "node:default:" + pk;
    auto blob = db.get(nodeKey);
    BaseEntity person = BaseEntity::deserialize(pk, *blob);
    // Use person data...
}
```

### Pattern: `MATCH ()-[r:FOLLOWS]->() RETURN r`

```cpp
auto [st, followsEdges] = pgm.getEdgesByType("FOLLOWS");
for (const auto& edge : followsEdges) {
    // edge.fromPk, edge.toPk, edge.edgeId available
}
```

### Pattern: `MATCH (a:Person)-[r:FOLLOWS]->(b:Person) RETURN a, r, b`

```cpp
auto [st1, people] = pgm.getNodesByLabel("Person");
auto [st2, followsEdges] = pgm.getEdgesByType("FOLLOWS");

// Filter edges where both endpoints are Person
for (const auto& edge : followsEdges) {
    bool fromIsPerson = std::find(people.begin(), people.end(), edge.fromPk) != people.end();
    bool toIsPerson = std::find(people.begin(), people.end(), edge.toPk) != people.end();
    
    if (fromIsPerson && toIsPerson) {
        // Matching pattern: Person -[FOLLOWS]-> Person
    }
}
```

### Pattern: `MATCH (a)-[:FOLLOWS]->(b)-[:FOLLOWS]->(c) RETURN a, c`

```cpp
// 2-hop traversal with type filtering
auto [st, edges] = pgm.getTypedOutEdges("alice", "FOLLOWS");
for (const auto& edge1 : edges) {
    std::string intermediate = edge1.toPk;
    auto [st2, edges2] = pgm.getTypedOutEdges(intermediate, "FOLLOWS");
    for (const auto& edge2 : edges2) {
        // alice -> intermediate -> edge2.toPk (2-hop path)
    }
}
```

---

## Migration Guide

### From Simple Graph to Property Graph

**Before (Simple Graph):**
```cpp
GraphIndexManager graph(db);

BaseEntity edge("e1");
edge.setField("_from", "alice");
edge.setField("_to", "bob");
graph.addEdge(edge);

auto [st, neighbors] = graph.outNeighbors("alice");
```

**After (Property Graph):**
```cpp
PropertyGraphManager pgm(db);

// Add nodes with labels
BaseEntity alice("alice");
alice.setField("id", "alice");
alice.setField("_labels", "Person");
pgm.addNode(alice);

BaseEntity bob("bob");
bob.setField("id", "bob");
bob.setField("_labels", "Person");
pgm.addNode(bob);

// Add edge with type
BaseEntity edge("e1");
edge.setField("id", "e1");
edge.setField("_from", "alice");
edge.setField("_to", "bob");
edge.setField("_type", "FOLLOWS");
pgm.addEdge(edge);

// Query by label
auto [st, people] = pgm.getNodesByLabel("Person");

// Query by type
auto [st2, followsEdges] = pgm.getEdgesByType("FOLLOWS");
```

**Key Changes:**
1. Nodes require explicit `addNode()` call (not just edges)
2. Nodes can have `_labels` field (comma-separated)
3. Edges can have `_type` field
4. New query methods: `getNodesByLabel()`, `getEdgesByType()`
5. Multi-graph support via `graph_id` parameter

---

## Integration with Existing Features

### Works With Temporal Graphs

```cpp
// Temporal edge with type
BaseEntity edge("e1");
edge.setField("id", "e1");
edge.setField("_from", "alice");
edge.setField("_to", "bob");
edge.setField("_type", "FOLLOWS");
edge.setField("valid_from", 1609459200000);  // 2021-01-01
edge.setField("valid_to", 1640995200000);    // 2022-01-01
pgm.addEdge(edge);

// Query: FOLLOWS edges active in 2021
auto [st, followsEdges] = pgm.getEdgesByType("FOLLOWS");
// Then filter by valid_from/valid_to (client-side)

// TODO: Combine with getEdgesInTimeRange() for server-side filtering
```

### Works With Recursive Path Queries

```cpp
// Recursive query with type filtering
RecursivePathQuery rpq;
rpq.start_node = "alice";
rpq.end_node = "charlie";
rpq.max_depth = 3;
rpq.edge_type = "FOLLOWS";   // ✅ server-side type filtering
rpq.graph_id = "social";     // optional, defaults to "default"
```

---

## Known Limitations

1. **Labels as String:** Labels stored as comma-separated string (not array)
   - **Workaround:** Parse string manually or extend BaseEntity
2. ~~**No Server-Side Type Filtering in Traversal:**~~ **✅ IMPLEMENTED:** Server-side type filtering now available
   - `GraphIndexManager::bfs(start, depth, edge_type, graph_id)` - BFS with type filtering
   - `GraphIndexManager::dijkstra(start, target, edge_type, graph_id)` - Dijkstra with type filtering
   - `RecursivePathQuery` supports `edge_type` and `graph_id` fields for filtered traversals
3. **No Property Constraints:** Cannot enforce label/type schemas
   - **Future:** Add schema validation
4. **Federation is Simplified:** No complex joins (only union of patterns)
   - **Future:** Add join operators (nested loop, hash join)
5. **No Cypher Parser:** Manual API calls required
   - **Future:** Cypher-to-API translator

---

## Future Enhancements

### 1. Array-Based Labels

**Problem:** Comma-separated string requires parsing  
**Solution:** Extend BaseEntity to support `std::vector<std::string>`

```cpp
alice.setField("_labels", std::vector<std::string>{"Person", "Employee"});
```

### 2. ~~Type-Aware Graph Traversal~~ ✅ **IMPLEMENTED (Nov 2025)**

~~**Problem:** BFS/Dijkstra don't filter by type~~  
**Status:** Server-side type filtering now available in GraphIndexManager

**Implementation:**
```cpp
// BFS with type filtering
auto [st, nodes] = graphIdx->bfs("alice", 3, "FOLLOWS", "social");
// Returns nodes reachable via FOLLOWS edges only

// Dijkstra with type filtering
auto [st, path] = graphIdx->dijkstra("alice", "bob", "FOLLOWS", "social");
// Only traverse FOLLOWS edges

// RecursivePathQuery integration
RecursivePathQuery q;
q.start_node = "alice";
q.edge_type = "FOLLOWS";
q.graph_id = "social";
q.max_depth = 5;
auto [st, paths] = queryEngine->executeRecursivePathQuery(q);
```

**Features:**
- Multi-graph aware: `graph_id` parameter scopes traversal to specific graph
- Server-side filtering: Edge type checked during traversal (not post-processing)
- Full integration: Works with temporal filters and recursive path queries

### 3. Schema Validation

**Problem:** No enforcement of valid labels/types  
**Solution:** Add schema definition and validation

```cpp
PropertyGraphSchema schema;
schema.defineNodeLabel("Person", {{"name", "string"}, {"age", "int"}});
schema.defineEdgeType("FOLLOWS", {{"since", "int"}});
pgm.setSchema(schema);

// Validation on insert
pgm.addNode(invalidNode);  // Error: Missing required field 'name'
```

### 4. Complex Federated Joins

**Problem:** Only union of patterns supported  
**Solution:** Add join operators

```cpp
FederatedJoinQuery fjq;
fjq.addPattern("social", "Person", "node");
fjq.addPattern("corporate", "Employee", "node");
fjq.setJoinKey("id");  // Join on node primary key
auto [st, result] = pgm.federatedJoin(fjq);
// Result: Person nodes that are also Employees
```

### 5. Cypher Query Language

**Problem:** Manual API calls verbose  
**Solution:** Cypher-to-API translator

```cpp
std::string cypher = "MATCH (p:Person)-[r:FOLLOWS]->(f:Person) RETURN p, f";
auto [st, result] = pgm.executeCypher(cypher, "social");
```

---

## Changelog

- **2025-01-15:** Initial implementation
  - Added `PropertyGraphManager` class
  - Implemented node labels (`_labels` field)
  - Implemented relationship types (`_type` field)
  - Added multi-graph federation (`graph_id` prefix)
  - Created label/type indices
  - Implemented federated pattern matching
  - Added batch operations
  - Created 13 comprehensive tests (all passing)
  - Documentation created

---

## See Also

- [Graph Index](./indexes.md#graph-index) - Base graph adjacency index
- [Temporal Graphs](./temporal_time_range_queries.md) - Time-window queries
- [Recursive Path Queries](./recursive_path_queries.md) - Multi-hop traversal
- [Base Entity](./base_entity.md) - Flexible schema-less storage
