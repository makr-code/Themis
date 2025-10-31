#pragma once

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/property_graph.h"
#include "index/vector_index.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>

namespace themis {

/// GNN Embedding Manager
/// 
/// Generates and manages graph neural network embeddings for nodes and edges.
/// Integrates with PropertyGraphManager for graph structure and VectorIndexManager for storage.
///
/// Features:
/// - Node embeddings (based on node features + graph structure)
/// - Edge embeddings (based on edge features + connected nodes)
/// - Graph-level embeddings (aggregated from node/edge embeddings)
/// - Batch processing for efficient embedding generation
/// - Incremental updates when graph changes
/// - Multiple embedding models/versions support
///
/// Architecture:
/// - Graph structure: PropertyGraphManager provides nodes, edges, labels, types
/// - Feature extraction: BaseEntity fields → feature vectors
/// - Model inference: External GNN model (Python bridge or native C++)
/// - Storage: VectorIndexManager for embedding similarity search
/// - Metadata: Tracks model version, generation timestamp, source entity
///
/// Example:
/// ```cpp
/// GNNEmbeddingManager gnn(db, pgm, vim);
/// 
/// // Generate node embeddings for all Person nodes in social graph
/// auto st = gnn.generateNodeEmbeddings("social", "Person", "gcn_v1");
/// // Stores embeddings in vector index: node_emb:social:Person:*
/// 
/// // Query similar nodes
/// auto [st2, similar] = gnn.findSimilarNodes("alice", "social", 10);
/// // Result: Top 10 nodes with most similar embeddings
/// 
/// // Generate edge embeddings
/// auto st3 = gnn.generateEdgeEmbeddings("social", "FOLLOWS", "gat_v1");
/// 
/// // Incremental update (when graph changes)
/// auto st4 = gnn.updateNodeEmbedding("bob", "social", "gcn_v1");
/// ```

class GNNEmbeddingManager {
public:
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    struct EmbeddingInfo {
        std::string entity_id;       // Node PK or Edge ID
        std::string entity_type;     // "node" or "edge"
        std::string graph_id;
        std::string model_name;      // e.g., "gcn_v1", "graphsage_v2"
        int64_t timestamp;           // Generation timestamp
        std::vector<float> embedding;
    };

    struct SimilarityResult {
        std::string entity_id;
        float similarity;            // Cosine similarity (higher = more similar)
        std::string entity_type;
        std::string graph_id;
    };

    /// Constructor
    /// @param db RocksDB wrapper
    /// @param pgm Property graph manager (for graph structure)
    /// @param vim Vector index manager (for embedding storage)
    explicit GNNEmbeddingManager(
        RocksDBWrapper& db,
        PropertyGraphManager& pgm,
        VectorIndexManager& vim
    );

    // ===== Node Embedding Generation =====

    /// Generate embeddings for all nodes with specific label in graph
    /// @param graph_id Target graph
    /// @param label Node label (e.g., "Person")
    /// @param model_name Model identifier (e.g., "gcn_v1")
    /// @param feature_fields Fields to use as node features (default: all fields)
    /// @return Status
    Status generateNodeEmbeddings(
        std::string_view graph_id,
        std::string_view label,
        std::string_view model_name,
        const std::vector<std::string>& feature_fields = {}
    );

    /// Generate embedding for single node (incremental update)
    /// @param node_pk Node primary key
    /// @param graph_id Target graph
    /// @param model_name Model identifier
    /// @param feature_fields Fields to use as features
    /// @return Status
    Status updateNodeEmbedding(
        std::string_view node_pk,
        std::string_view graph_id,
        std::string_view model_name,
        const std::vector<std::string>& feature_fields = {}
    );

    // ===== Edge Embedding Generation =====

    /// Generate embeddings for all edges with specific type in graph
    /// @param graph_id Target graph
    /// @param edge_type Edge type (e.g., "FOLLOWS")
    /// @param model_name Model identifier
    /// @param feature_fields Fields to use as edge features
    /// @return Status
    Status generateEdgeEmbeddings(
        std::string_view graph_id,
        std::string_view edge_type,
        std::string_view model_name,
        const std::vector<std::string>& feature_fields = {}
    );

    /// Generate embedding for single edge (incremental update)
    /// @param edge_id Edge identifier
    /// @param graph_id Target graph
    /// @param model_name Model identifier
    /// @param feature_fields Fields to use as features
    /// @return Status
    Status updateEdgeEmbedding(
        std::string_view edge_id,
        std::string_view graph_id,
        std::string_view model_name,
        const std::vector<std::string>& feature_fields = {}
    );

    // ===== Graph-Level Embeddings =====

    /// Generate graph-level embedding (aggregated from node/edge embeddings)
    /// @param graph_id Target graph
    /// @param model_name Model identifier
    /// @param aggregation_method "mean", "sum", "max", "attention"
    /// @return Pair of Status and embedding vector
    std::pair<Status, std::vector<float>> generateGraphEmbedding(
        std::string_view graph_id,
        std::string_view model_name,
        std::string_view aggregation_method = "mean"
    );

    // ===== Embedding Retrieval =====

    /// Get embedding for specific node
    /// @param node_pk Node primary key
    /// @param graph_id Target graph
    /// @param model_name Model identifier
    /// @return Pair of Status and EmbeddingInfo
    std::pair<Status, EmbeddingInfo> getNodeEmbedding(
        std::string_view node_pk,
        std::string_view graph_id,
        std::string_view model_name
    ) const;

    /// Get embedding for specific edge
    /// @param edge_id Edge identifier
    /// @param graph_id Target graph
    /// @param model_name Model identifier
    /// @return Pair of Status and EmbeddingInfo
    std::pair<Status, EmbeddingInfo> getEdgeEmbedding(
        std::string_view edge_id,
        std::string_view graph_id,
        std::string_view model_name
    ) const;

    // ===== Similarity Search =====

    /// Find similar nodes based on embedding similarity
    /// @param node_pk Query node
    /// @param graph_id Target graph
    /// @param k Number of results
    /// @param model_name Model identifier
    /// @return Pair of Status and similarity results
    std::pair<Status, std::vector<SimilarityResult>> findSimilarNodes(
        std::string_view node_pk,
        std::string_view graph_id,
        int k,
        std::string_view model_name
    ) const;

    /// Find similar edges based on embedding similarity
    /// @param edge_id Query edge
    /// @param graph_id Target graph
    /// @param k Number of results
    /// @param model_name Model identifier
    /// @return Pair of Status and similarity results
    std::pair<Status, std::vector<SimilarityResult>> findSimilarEdges(
        std::string_view edge_id,
        std::string_view graph_id,
        int k,
        std::string_view model_name
    ) const;

    // ===== Model Management =====

    /// Register GNN model for embedding generation
    /// @param model_name Model identifier
    /// @param model_type "gcn", "graphsage", "gat", "gin", "custom"
    /// @param embedding_dim Output embedding dimension
    /// @param config Model-specific configuration (JSON string)
    /// @return Status
    Status registerModel(
        std::string_view model_name,
        std::string_view model_type,
        int embedding_dim,
        std::string_view config = "{}"
    );

    /// List all registered models
    /// @return Pair of Status and model names
    std::pair<Status, std::vector<std::string>> listModels() const;

    /// Get model info
    struct ModelInfo {
        std::string name;
        std::string type;
        int embedding_dim;
        std::string config;
        int64_t registered_at;
    };
    std::pair<Status, ModelInfo> getModelInfo(std::string_view model_name) const;

    // ===== Batch Operations =====

    /// Generate node embeddings in batches (more efficient)
    /// @param node_pks List of node primary keys
    /// @param graph_id Target graph
    /// @param model_name Model identifier
    /// @param batch_size Batch size for processing
    /// @return Status
    Status generateNodeEmbeddingsBatch(
        const std::vector<std::string>& node_pks,
        std::string_view graph_id,
        std::string_view model_name,
        size_t batch_size = 32
    );

    /// Generate edge embeddings in batches
    /// @param edge_ids List of edge identifiers
    /// @param graph_id Target graph
    /// @param model_name Model identifier
    /// @param batch_size Batch size for processing
    /// @return Status
    Status generateEdgeEmbeddingsBatch(
        const std::vector<std::string>& edge_ids,
        std::string_view graph_id,
        std::string_view model_name,
        size_t batch_size = 32
    );

    // ===== Statistics =====

    struct EmbeddingStats {
        size_t total_node_embeddings;
        size_t total_edge_embeddings;
        std::unordered_map<std::string, size_t> embeddings_per_model;
        std::unordered_map<std::string, size_t> embeddings_per_graph;
    };

    std::pair<Status, EmbeddingStats> getStats() const;

private:
    RocksDBWrapper& db_;
    PropertyGraphManager& pgm_;
    VectorIndexManager& vim_;

    // Model registry
    std::unordered_map<std::string, ModelInfo> models_;

    // Helper: Extract feature vector from entity fields
    std::vector<float> extractFeatures_(
        const BaseEntity& entity,
        const std::vector<std::string>& feature_fields
    ) const;

    // Helper: Build embedding key
    std::string makeEmbeddingKey_(
        std::string_view entity_type,  // "node" or "edge"
        std::string_view graph_id,
        std::string_view entity_id,
        std::string_view model_name
    ) const;

    // Helper: Parse embedding key
    struct EmbeddingKeyParts {
        std::string entity_type;
        std::string graph_id;
        std::string entity_id;
        std::string model_name;
    };
    std::optional<EmbeddingKeyParts> parseEmbeddingKey_(std::string_view key) const;

    // Helper: Compute embedding using registered model
    // For MVP: Simple feature-based embedding (mean pooling)
    // For production: Call external GNN model via Python bridge or native inference
    std::pair<Status, std::vector<float>> computeEmbedding_(
        std::string_view model_name,
        const std::vector<float>& features,
        const std::vector<std::string>& neighbor_ids,  // For GNN context
        std::string_view graph_id
    ) const;

    // Helper: Get neighbors for GNN aggregation
    std::vector<std::string> getNeighbors_(
        std::string_view node_pk,
        std::string_view graph_id,
        int hop_count = 1
    ) const;
};

} // namespace themis
