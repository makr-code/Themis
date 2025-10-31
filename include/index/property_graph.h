#pragma once

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <optional>

namespace themis {

/// Property Graph Extension for GraphIndexManager
/// 
/// Features:
/// - Node Labels: Nodes can have multiple labels (e.g., :Person, :Employee)
/// - Relationship Types: Edges have a type (e.g., FOLLOWS, LIKES, WORKS_AT)
/// - Multi-Graph Federation: Support multiple isolated graphs with cross-graph queries
/// 
/// Schema Extensions:
/// - Node: BaseEntity with _labels array field (e.g., ["Person", "Employee"])
/// - Edge: BaseEntity with _type string field (e.g., "FOLLOWS")
/// - Graph ID: Prefix to isolate multiple graphs (default: "default")
/// 
/// Key Schemas:
/// - Label Index: label:<graph_id>:<label>:<pk> -> (empty)
/// - Type Index: type:<graph_id>:<type>:<edge_id> -> (empty)
/// - Graph Outdex: graph:out:<graph_id>:<from_pk>:<edge_id> -> <to_pk>
/// - Graph Indeg: graph:in:<graph_id>:<to_pk>:<edge_id> -> <from_pk>
/// 
/// Performance:
/// - Label queries: O(N_label) via prefix scan
/// - Type queries: O(E_type) via prefix scan
/// - Cross-graph isolation: O(1) via graph_id prefix
///
/// Example:
/// ```cpp
/// PropertyGraphManager pgm(db);
/// 
/// // Create node with labels
/// BaseEntity alice("alice");
/// alice.setField("name", "Alice");
/// alice.setField("_labels", std::vector<std::string>{"Person", "Employee"});
/// pgm.addNode(alice, "social");  // graph_id = "social"
/// 
/// // Create typed edge
/// BaseEntity follows("follows_1");
/// follows.setField("_from", "alice");
/// follows.setField("_to", "bob");
/// follows.setField("_type", "FOLLOWS");
/// follows.setField("since", 2020);
/// pgm.addEdge(follows, "social");
/// 
/// // Query by label
/// auto [st, nodes] = pgm.getNodesByLabel("Person", "social");
/// // Result: [alice, bob, ...]
/// 
/// // Query by type
/// auto [st2, edges] = pgm.getEdgesByType("FOLLOWS", "social");
/// // Result: [follows_1, follows_2, ...]
/// 
/// // Cross-graph query
/// auto [st3, results] = pgm.federatedQuery({
///     {"social", "Person", "FOLLOWS"},
///     {"corporate", "Employee", "REPORTS_TO"}
/// });
/// ```

class PropertyGraphManager {
public:
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    struct NodeInfo {
        std::string pk;
        std::vector<std::string> labels;
        std::string graph_id;
    };

    struct EdgeInfo {
        std::string edgeId;
        std::string fromPk;
        std::string toPk;
        std::string type;
        std::string graph_id;
    };

    struct FederationPattern {
        std::string graph_id;
        std::string label_or_type;  // Node label or edge type
        std::string pattern_type;   // "node" or "edge"
    };

    explicit PropertyGraphManager(RocksDBWrapper& db);

    // ===== Node Label Operations =====

    /// Add node with labels (entity must have _labels array field)
    /// Creates label index entries for each label
    Status addNode(const BaseEntity& node, std::string_view graph_id = "default");

    /// Remove node and all label indices
    Status deleteNode(std::string_view pk, std::string_view graph_id = "default");

    /// Add label to existing node (updates label index)
    Status addNodeLabel(std::string_view pk, std::string_view label, std::string_view graph_id = "default");

    /// Remove label from node (updates label index)
    Status removeNodeLabel(std::string_view pk, std::string_view label, std::string_view graph_id = "default");

    /// Check if node has specific label
    std::pair<Status, bool> hasNodeLabel(std::string_view pk, std::string_view label, std::string_view graph_id = "default") const;

    /// Get all nodes with specific label
    /// Returns vector of primary keys
    std::pair<Status, std::vector<std::string>> getNodesByLabel(std::string_view label, std::string_view graph_id = "default") const;

    /// Get all labels for a node
    std::pair<Status, std::vector<std::string>> getNodeLabels(std::string_view pk, std::string_view graph_id = "default") const;

    // ===== Relationship Type Operations =====

    /// Add edge with type (entity must have _type string field)
    /// Creates type index entry
    Status addEdge(const BaseEntity& edge, std::string_view graph_id = "default");

    /// Remove edge and type index
    Status deleteEdge(std::string_view edgeId, std::string_view graph_id = "default");

    /// Get all edges with specific type
    /// Returns EdgeInfo with full metadata
    std::pair<Status, std::vector<EdgeInfo>> getEdgesByType(std::string_view type, std::string_view graph_id = "default") const;

    /// Get type of specific edge
    std::pair<Status, std::string> getEdgeType(std::string_view edgeId, std::string_view graph_id = "default") const;

    /// Get typed edges from specific node
    std::pair<Status, std::vector<EdgeInfo>> getTypedOutEdges(
        std::string_view fromPk,
        std::string_view type,
        std::string_view graph_id = "default"
    ) const;

    // ===== Multi-Graph Federation =====

    /// List all graph IDs in database
    std::pair<Status, std::vector<std::string>> listGraphs() const;

    /// Get statistics for specific graph
    struct GraphStats {
        std::string graph_id;
        size_t node_count;
        size_t edge_count;
        size_t label_count;
        size_t type_count;
    };
    std::pair<Status, GraphStats> getGraphStats(std::string_view graph_id) const;

    /// Cross-graph pattern matching (simplified federated query)
    /// Example: Find Person nodes in "social" graph and Employee nodes in "corporate" graph
    struct FederationResult {
        std::vector<NodeInfo> nodes;
        std::vector<EdgeInfo> edges;
    };
    std::pair<Status, FederationResult> federatedQuery(const std::vector<FederationPattern>& patterns) const;

    // ===== Batch Operations =====

    /// Add multiple nodes with labels (atomic)
    Status addNodesBatch(const std::vector<BaseEntity>& nodes, std::string_view graph_id = "default");

    /// Add multiple edges with types (atomic)
    Status addEdgesBatch(const std::vector<BaseEntity>& edges, std::string_view graph_id = "default");

private:
    RocksDBWrapper& db_;

    // Helper: Extract labels from node entity
    std::vector<std::string> extractLabels_(const BaseEntity& node) const;

    // Helper: Extract type from edge entity
    std::optional<std::string> extractType_(const BaseEntity& edge) const;

    // Helper: Build label index key
    std::string makeLabelIndexKey_(std::string_view graph_id, std::string_view label, std::string_view pk) const;

    // Helper: Build type index key
    std::string makeTypeIndexKey_(std::string_view graph_id, std::string_view type, std::string_view edgeId) const;

    // Helper: Build node key
    std::string makeNodeKey_(std::string_view graph_id, std::string_view pk) const;

    // Helper: Build edge key
    std::string makeEdgeKey_(std::string_view graph_id, std::string_view edgeId) const;

    // Helper: Build graph outdex key
    std::string makeGraphOutdexKey_(std::string_view graph_id, std::string_view fromPk, std::string_view edgeId) const;

    // Helper: Build graph indeg key
    std::string makeGraphIndegKey_(std::string_view graph_id, std::string_view toPk, std::string_view edgeId) const;
};

} // namespace themis
