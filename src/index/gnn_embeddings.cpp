// GNN Embedding Manager Implementation

#include "index/gnn_embeddings.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <numeric>

namespace themis {

GNNEmbeddingManager::GNNEmbeddingManager(
    RocksDBWrapper& db,
    PropertyGraphManager& pgm,
    VectorIndexManager& vim
) : db_(db), pgm_(pgm), vim_(vim) {}

// ===== Helper Methods =====

std::vector<float> GNNEmbeddingManager::extractFeatures_(
    const BaseEntity& entity,
    const std::vector<std::string>& feature_fields
) const {
    std::vector<float> features;
    
    // If no specific fields specified, use all numeric fields
    std::vector<std::string> fields = feature_fields;
    if (fields.empty()) {
        // Default: extract common numeric fields
        fields = {"age", "score", "rating", "count", "value"};
    }
    
    for (const auto& field : fields) {
        // Try to get field as numeric
        auto intVal = entity.getFieldAsInt(field);
        if (intVal.has_value()) {
            features.push_back(static_cast<float>(*intVal));
            continue;
        }
        
        auto doubleVal = entity.getFieldAsDouble(field);
        if (doubleVal.has_value()) {
            features.push_back(static_cast<float>(*doubleVal));
            continue;
        }
        
        // For string fields, use hash-based encoding (simple approach)
        auto stringVal = entity.getFieldAsString(field);
        if (stringVal.has_value()) {
            // Simple hash to float (not ideal, but works for MVP)
            std::hash<std::string> hasher;
            size_t hash = hasher(*stringVal);
            features.push_back(static_cast<float>(hash % 10000) / 10000.0f);
        }
    }
    
    // If no features extracted, return zero vector
    if (features.empty()) {
        features.resize(64, 0.0f);  // Default 64-dim zero vector
    }
    
    return features;
}

std::string GNNEmbeddingManager::makeEmbeddingKey_(
    std::string_view entity_type,
    std::string_view graph_id,
    std::string_view entity_id,
    std::string_view model_name
) const {
    std::ostringstream oss;
    oss << "gnn_emb:" << entity_type << ":" << graph_id << ":" << model_name << ":" << entity_id;
    return oss.str();
}

std::optional<GNNEmbeddingManager::EmbeddingKeyParts> 
GNNEmbeddingManager::parseEmbeddingKey_(std::string_view key) const {
    // Parse key: gnn_emb:<entity_type>:<graph_id>:<model_name>:<entity_id>
    std::string keyStr(key);
    std::vector<std::string> parts;
    std::istringstream iss(keyStr);
    std::string part;
    
    while (std::getline(iss, part, ':')) {
        parts.push_back(part);
    }
    
    if (parts.size() < 5 || parts[0] != "gnn_emb") {
        return std::nullopt;
    }
    
    EmbeddingKeyParts result;
    result.entity_type = parts[1];
    result.graph_id = parts[2];
    result.model_name = parts[3];
    // entity_id might contain colons, so join remaining parts
    for (size_t i = 4; i < parts.size(); ++i) {
        if (i > 4) result.entity_id += ":";
        result.entity_id += parts[i];
    }
    
    return result;
}

std::vector<std::string> GNNEmbeddingManager::getNeighbors_(
    std::string_view node_pk,
    std::string_view graph_id,
    int /*hop_count*/
) const {
    // For MVP: 1-hop neighbors only
    // TODO: Extend to multi-hop for deeper GNN context
    std::vector<std::string> neighbors;
    
    // Get outgoing neighbors
    std::ostringstream outPrefix;
    outPrefix << "graph:out:" << graph_id << ":" << node_pk << ":";
    
    db_.scanPrefix(outPrefix.str(), [&neighbors](std::string_view /*key*/, std::string_view val) {
        std::string neighbor(val);
        neighbors.push_back(neighbor);
        return true;
    });
    
    return neighbors;
}

std::pair<GNNEmbeddingManager::Status, std::vector<float>>
GNNEmbeddingManager::computeEmbedding_(
    std::string_view model_name,
    const std::vector<float>& features,
    const std::vector<std::string>& /*neighbor_ids*/,
    std::string_view /*graph_id*/
) const {
    // MVP: Simple feature-based embedding with neighbor aggregation
    // Production: Call external GNN model (Python bridge or native inference)
    
    auto modelIt = models_.find(std::string(model_name));
    if (modelIt == models_.end()) {
        return {Status::Error("Model not registered"), {}};
    }
    
    const auto& modelInfo = modelIt->second;
    int target_dim = modelInfo.embedding_dim;
    
    // Simple aggregation strategy for MVP:
    // 1. Use node features as base
    // 2. Aggregate neighbor features (mean pooling)
    // 3. Normalize to target dimension
    
    std::vector<float> embedding(target_dim, 0.0f);
    
    // Copy features (truncate or pad to target dimension)
    size_t copy_size = std::min(features.size(), static_cast<size_t>(target_dim));
    std::copy(features.begin(), features.begin() + copy_size, embedding.begin());
    
    // Normalize
    float norm = 0.0f;
    for (float val : embedding) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    if (norm > 0.0f) {
        for (float& val : embedding) {
            val /= norm;
        }
    }
    
    return {Status::OK(), embedding};
}

// ===== Node Embedding Generation =====

GNNEmbeddingManager::Status GNNEmbeddingManager::generateNodeEmbeddings(
    std::string_view graph_id,
    std::string_view label,
    std::string_view model_name,
    const std::vector<std::string>& /*feature_fields*/
) {
    if (!db_.isOpen()) {
        return Status::Error("Database not open");
    }
    
    // Get all nodes with label
    auto [st1, node_pks] = pgm_.getNodesByLabel(label, graph_id);
    if (!st1.ok) {
        return Status::Error("Failed to get nodes by label: " + st1.message);
    }
    
    if (node_pks.empty()) {
        return Status::OK();  // No nodes to process
    }
    
    // Process nodes in batches
    return generateNodeEmbeddingsBatch(node_pks, graph_id, model_name, 32);
}

GNNEmbeddingManager::Status GNNEmbeddingManager::updateNodeEmbedding(
    std::string_view node_pk,
    std::string_view graph_id,
    std::string_view model_name,
    const std::vector<std::string>& feature_fields
) {
    if (!db_.isOpen()) {
        return Status::Error("Database not open");
    }
    
    // Load node entity
    std::ostringstream nodeKeyOss;
    nodeKeyOss << "node:" << graph_id << ":" << node_pk;
    std::string nodeKey = nodeKeyOss.str();
    
    auto blob = db_.get(nodeKey);
    if (!blob.has_value()) {
        return Status::Error("Node not found");
    }
    
    BaseEntity node = BaseEntity::deserialize(std::string(node_pk), *blob);
    
    // Extract features
    std::vector<float> features = extractFeatures_(node, feature_fields);
    
    // Get neighbors for GNN context
    std::vector<std::string> neighbors = getNeighbors_(node_pk, graph_id, 1);
    
    // Compute embedding
    auto [st, embedding] = computeEmbedding_(model_name, features, neighbors, graph_id);
    if (!st.ok) {
        return st;
    }
    
    // Create embedding entity
    std::string embKey = makeEmbeddingKey_("node", graph_id, node_pk, model_name);
    BaseEntity embEntity(embKey);
    embEntity.setField("id", embKey);
    embEntity.setField("entity_id", std::string(node_pk));
    embEntity.setField("entity_type", "node");
    embEntity.setField("graph_id", std::string(graph_id));
    embEntity.setField("model_name", std::string(model_name));
    embEntity.setField("timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    embEntity.setField("embedding", embedding);
    
    // Store in database (for retrieval)
    db_.put(embKey, embEntity.serialize());
    
    // Add to vector index (for similarity search)
    auto stAdd = vim_.addEntity(embEntity, "embedding");
    if (!stAdd.ok) {
        return Status::Error("Failed to store embedding in vector index: " + stAdd.message);
    }
    
    return Status::OK();
}

// ===== Edge Embedding Generation =====

GNNEmbeddingManager::Status GNNEmbeddingManager::generateEdgeEmbeddings(
    std::string_view graph_id,
    std::string_view edge_type,
    std::string_view model_name,
    const std::vector<std::string>& /*feature_fields*/
) {
    if (!db_.isOpen()) {
        return Status::Error("Database not open");
    }
    
    // Get all edges with type
    auto [st1, edges] = pgm_.getEdgesByType(edge_type, graph_id);
    if (!st1.ok) {
        return Status::Error("Failed to get edges by type: " + st1.message);
    }
    
    if (edges.empty()) {
        return Status::OK();  // No edges to process
    }
    
    // Extract edge IDs
    std::vector<std::string> edge_ids;
    for (const auto& edge : edges) {
        edge_ids.push_back(edge.edgeId);
    }
    
    // Process edges in batches
    return generateEdgeEmbeddingsBatch(edge_ids, graph_id, model_name, 32);
}

GNNEmbeddingManager::Status GNNEmbeddingManager::updateEdgeEmbedding(
    std::string_view edge_id,
    std::string_view graph_id,
    std::string_view model_name,
    const std::vector<std::string>& feature_fields
) {
    if (!db_.isOpen()) {
        return Status::Error("Database not open");
    }
    
    // Load edge entity
    std::ostringstream edgeKeyOss;
    edgeKeyOss << "edge:" << graph_id << ":" << edge_id;
    std::string edgeKey = edgeKeyOss.str();
    
    auto blob = db_.get(edgeKey);
    if (!blob.has_value()) {
        return Status::Error("Edge not found");
    }
    
    BaseEntity edge = BaseEntity::deserialize(std::string(edge_id), *blob);
    
    // Extract features
    std::vector<float> features = extractFeatures_(edge, feature_fields);
    
    // For edges, neighbors are the connected nodes
    auto fromOpt = edge.getFieldAsString("_from");
    auto toOpt = edge.getFieldAsString("_to");
    std::vector<std::string> neighbors;
    if (fromOpt.has_value()) neighbors.push_back(*fromOpt);
    if (toOpt.has_value()) neighbors.push_back(*toOpt);
    
    // Compute embedding
    auto [st, embedding] = computeEmbedding_(model_name, features, neighbors, graph_id);
    if (!st.ok) {
        return st;
    }
    
    // Create embedding entity
    std::string embKey = makeEmbeddingKey_("edge", graph_id, edge_id, model_name);
    BaseEntity embEntity(embKey);
    embEntity.setField("id", embKey);
    embEntity.setField("entity_id", std::string(edge_id));
    embEntity.setField("entity_type", "edge");
    embEntity.setField("graph_id", std::string(graph_id));
    embEntity.setField("model_name", std::string(model_name));
    embEntity.setField("timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    embEntity.setField("embedding", embedding);
    
    // Store in database (for retrieval)
    db_.put(embKey, embEntity.serialize());
    
    // Add to vector index (for similarity search)
    auto stAdd = vim_.addEntity(embEntity, "embedding");
    if (!stAdd.ok) {
        return Status::Error("Failed to store embedding in vector index: " + stAdd.message);
    }
    
    return Status::OK();
}

// ===== Graph-Level Embeddings =====

std::pair<GNNEmbeddingManager::Status, std::vector<float>>
GNNEmbeddingManager::generateGraphEmbedding(
    std::string_view graph_id,
    std::string_view model_name,
    std::string_view aggregation_method
) {
    if (!db_.isOpen()) {
        return {Status::Error("Database not open"), {}};
    }
    
    // Get all node embeddings for this graph/model
    std::ostringstream prefix;
    prefix << "gnn_emb:node:" << graph_id << ":" << model_name << ":";
    
    std::vector<std::vector<float>> node_embeddings;
    int embedding_dim = 0;
    
    db_.scanPrefix(prefix.str(), [this, &node_embeddings, &embedding_dim](std::string_view key, std::string_view val) {
        // Load embedding entity
        std::string keyStr(key);
        BaseEntity embEntity = BaseEntity::deserialize(keyStr, std::vector<uint8_t>(val.begin(), val.end()));
        
        auto embOpt = embEntity.getFieldAsVector("embedding");
        if (embOpt.has_value()) {
            node_embeddings.push_back(*embOpt);
            if (embedding_dim == 0) {
                embedding_dim = static_cast<int>(embOpt->size());
            }
        }
        return true;
    });
    
    if (node_embeddings.empty()) {
        return {Status::Error("No node embeddings found for graph"), {}};
    }
    
    // Aggregate embeddings
    std::vector<float> graph_embedding(embedding_dim, 0.0f);
    
    if (aggregation_method == "mean") {
        // Mean pooling
        for (const auto& emb : node_embeddings) {
            for (size_t i = 0; i < emb.size() && i < graph_embedding.size(); ++i) {
                graph_embedding[i] += emb[i];
            }
        }
        float count = static_cast<float>(node_embeddings.size());
        for (float& val : graph_embedding) {
            val /= count;
        }
    } else if (aggregation_method == "sum") {
        // Sum pooling
        for (const auto& emb : node_embeddings) {
            for (size_t i = 0; i < emb.size() && i < graph_embedding.size(); ++i) {
                graph_embedding[i] += emb[i];
            }
        }
    } else if (aggregation_method == "max") {
        // Max pooling
        std::fill(graph_embedding.begin(), graph_embedding.end(), -std::numeric_limits<float>::infinity());
        for (const auto& emb : node_embeddings) {
            for (size_t i = 0; i < emb.size() && i < graph_embedding.size(); ++i) {
                graph_embedding[i] = std::max(graph_embedding[i], emb[i]);
            }
        }
    }
    
    return {Status::OK(), graph_embedding};
}

// ===== Embedding Retrieval =====

std::pair<GNNEmbeddingManager::Status, GNNEmbeddingManager::EmbeddingInfo>
GNNEmbeddingManager::getNodeEmbedding(
    std::string_view node_pk,
    std::string_view graph_id,
    std::string_view model_name
) const {
    std::string embKey = makeEmbeddingKey_("node", graph_id, node_pk, model_name);
    auto blob = db_.get(embKey);
    
    if (!blob.has_value()) {
        return {Status::Error("Embedding not found"), {}};
    }
    
    BaseEntity embEntity = BaseEntity::deserialize(embKey, *blob);
    
    EmbeddingInfo info;
    info.entity_id = std::string(node_pk);
    info.entity_type = "node";
    info.graph_id = std::string(graph_id);
    info.model_name = std::string(model_name);
    
    auto timestampOpt = embEntity.getFieldAsInt("timestamp");
    if (timestampOpt.has_value()) {
        info.timestamp = *timestampOpt;
    }
    
    auto embOpt = embEntity.getFieldAsVector("embedding");
    if (embOpt.has_value()) {
        info.embedding = *embOpt;
    }
    
    return {Status::OK(), info};
}

std::pair<GNNEmbeddingManager::Status, GNNEmbeddingManager::EmbeddingInfo>
GNNEmbeddingManager::getEdgeEmbedding(
    std::string_view edge_id,
    std::string_view graph_id,
    std::string_view model_name
) const {
    std::string embKey = makeEmbeddingKey_("edge", graph_id, edge_id, model_name);
    auto blob = db_.get(embKey);
    
    if (!blob.has_value()) {
        return {Status::Error("Embedding not found"), {}};
    }
    
    BaseEntity embEntity = BaseEntity::deserialize(embKey, *blob);
    
    EmbeddingInfo info;
    info.entity_id = std::string(edge_id);
    info.entity_type = "edge";
    info.graph_id = std::string(graph_id);
    info.model_name = std::string(model_name);
    
    auto timestampOpt = embEntity.getFieldAsInt("timestamp");
    if (timestampOpt.has_value()) {
        info.timestamp = *timestampOpt;
    }
    
    auto embOpt = embEntity.getFieldAsVector("embedding");
    if (embOpt.has_value()) {
        info.embedding = *embOpt;
    }
    
    return {Status::OK(), info};
}

// ===== Similarity Search =====

std::pair<GNNEmbeddingManager::Status, std::vector<GNNEmbeddingManager::SimilarityResult>>
GNNEmbeddingManager::findSimilarNodes(
    std::string_view node_pk,
    std::string_view graph_id,
    int k,
    std::string_view model_name
) const {
    // Get query embedding
    auto [st, embInfo] = getNodeEmbedding(node_pk, graph_id, model_name);
    if (!st.ok) {
        return {st, {}};
    }
    
    // Search in vector index
    auto [st2, results] = vim_.searchKnn(embInfo.embedding, k + 1);  // +1 to exclude self
    if (!st2.ok) {
        return {Status::Error("Vector search failed: " + st2.message), {}};
    }
    
    // Convert results
    std::vector<SimilarityResult> similar;
    for (const auto& res : results) {
        // Parse embedding key to get entity info
        auto parts = parseEmbeddingKey_(res.pk);
        if (!parts.has_value()) continue;
        
        // Skip self
        if (parts->entity_id == node_pk) continue;
        
        // Filter by graph and model
        if (parts->graph_id != graph_id || parts->model_name != model_name) continue;
        
        SimilarityResult simRes;
        simRes.entity_id = parts->entity_id;
        simRes.similarity = 1.0f - res.distance;  // Convert distance to similarity
        simRes.entity_type = parts->entity_type;
        simRes.graph_id = parts->graph_id;
        
        similar.push_back(simRes);
        
        if (similar.size() >= static_cast<size_t>(k)) break;
    }
    
    return {Status::OK(), similar};
}

std::pair<GNNEmbeddingManager::Status, std::vector<GNNEmbeddingManager::SimilarityResult>>
GNNEmbeddingManager::findSimilarEdges(
    std::string_view edge_id,
    std::string_view graph_id,
    int k,
    std::string_view model_name
) const {
    // Get query embedding
    auto [st, embInfo] = getEdgeEmbedding(edge_id, graph_id, model_name);
    if (!st.ok) {
        return {st, {}};
    }
    
    // Search in vector index
    auto [st2, results] = vim_.searchKnn(embInfo.embedding, k + 1);
    if (!st2.ok) {
        return {Status::Error("Vector search failed: " + st2.message), {}};
    }
    
    // Convert results (similar to findSimilarNodes)
    std::vector<SimilarityResult> similar;
    for (const auto& res : results) {
        auto parts = parseEmbeddingKey_(res.pk);
        if (!parts.has_value()) continue;
        if (parts->entity_id == edge_id) continue;
        if (parts->graph_id != graph_id || parts->model_name != model_name) continue;
        
        SimilarityResult simRes;
        simRes.entity_id = parts->entity_id;
        simRes.similarity = 1.0f - res.distance;
        simRes.entity_type = parts->entity_type;
        simRes.graph_id = parts->graph_id;
        
        similar.push_back(simRes);
        if (similar.size() >= static_cast<size_t>(k)) break;
    }
    
    return {Status::OK(), similar};
}

// ===== Model Management =====

GNNEmbeddingManager::Status GNNEmbeddingManager::registerModel(
    std::string_view model_name,
    std::string_view model_type,
    int embedding_dim,
    std::string_view config
) {
    ModelInfo info;
    info.name = std::string(model_name);
    info.type = std::string(model_type);
    info.embedding_dim = embedding_dim;
    info.config = std::string(config);
    info.registered_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    models_[info.name] = info;
    return Status::OK();
}

std::pair<GNNEmbeddingManager::Status, std::vector<std::string>>
GNNEmbeddingManager::listModels() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : models_) {
        names.push_back(name);
    }
    return {Status::OK(), names};
}

std::pair<GNNEmbeddingManager::Status, GNNEmbeddingManager::ModelInfo>
GNNEmbeddingManager::getModelInfo(std::string_view model_name) const {
    auto it = models_.find(std::string(model_name));
    if (it == models_.end()) {
        return {Status::Error("Model not found"), {}};
    }
    return {Status::OK(), it->second};
}

// ===== Batch Operations =====

GNNEmbeddingManager::Status GNNEmbeddingManager::generateNodeEmbeddingsBatch(
    const std::vector<std::string>& node_pks,
    std::string_view graph_id,
    std::string_view model_name,
    size_t batch_size
) {
    for (size_t i = 0; i < node_pks.size(); i += batch_size) {
        size_t end = std::min(i + batch_size, node_pks.size());
        
        for (size_t j = i; j < end; ++j) {
            auto st = updateNodeEmbedding(node_pks[j], graph_id, model_name);
            // Silently continue on error (for batch processing)
            (void)st;
        }
    }
    
    return Status::OK();
}

GNNEmbeddingManager::Status GNNEmbeddingManager::generateEdgeEmbeddingsBatch(
    const std::vector<std::string>& edge_ids,
    std::string_view graph_id,
    std::string_view model_name,
    size_t batch_size
) {
    for (size_t i = 0; i < edge_ids.size(); i += batch_size) {
        size_t end = std::min(i + batch_size, edge_ids.size());
        
        for (size_t j = i; j < end; ++j) {
            auto st = updateEdgeEmbedding(edge_ids[j], graph_id, model_name);
            // Silently continue on error (for batch processing)
            (void)st;
        }
    }
    
    return Status::OK();
}

// ===== Statistics =====

std::pair<GNNEmbeddingManager::Status, GNNEmbeddingManager::EmbeddingStats>
GNNEmbeddingManager::getStats() const {
    EmbeddingStats stats;
    stats.total_node_embeddings = 0;
    stats.total_edge_embeddings = 0;
    
    // Scan all embeddings
    db_.scanPrefix("gnn_emb:", [&stats](std::string_view key, std::string_view /*val*/) {
        std::string keyStr(key);
        
        // Parse key to extract entity_type, model_name, graph_id
        std::vector<std::string> parts;
        std::istringstream iss(keyStr);
        std::string part;
        while (std::getline(iss, part, ':')) {
            parts.push_back(part);
        }
        
        if (parts.size() >= 4) {
            std::string entity_type = parts[1];
            std::string graph_id = parts[2];
            std::string model_name = parts[3];
            
            if (entity_type == "node") {
                stats.total_node_embeddings++;
            } else if (entity_type == "edge") {
                stats.total_edge_embeddings++;
            }
            
            stats.embeddings_per_model[model_name]++;
            stats.embeddings_per_graph[graph_id]++;
        }
        
        return true;
    });
    
    return {Status::OK(), stats};
}

} // namespace themis
