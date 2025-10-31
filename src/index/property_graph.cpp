// Property Graph Manager Implementation

#include "index/property_graph.h"
#include "storage/base_entity.h"
#include "utils/logger.h"
#include <sstream>
#include <algorithm>

namespace themis {

PropertyGraphManager::PropertyGraphManager(RocksDBWrapper& db) : db_(db) {}

// ===== Helper Methods =====

std::vector<std::string> PropertyGraphManager::extractLabels_(const BaseEntity& node) const {
    std::vector<std::string> labels;
    
    // Try to get _labels field as array
    auto labelsField = node.getField("_labels");
    if (!labelsField.has_value()) {
        return labels;  // No labels
    }

    // Value can be variant, check if it's a vector
    // For now, we'll use getFieldAsString and parse comma-separated (simplified)
    // TODO: Extend BaseEntity to support string arrays
    auto labelsStr = node.getFieldAsString("_labels");
    if (labelsStr.has_value()) {
        std::string labels_str = *labelsStr;
        std::stringstream ss(labels_str);
        std::string label;
        while (std::getline(ss, label, ',')) {
            // Trim whitespace
            label.erase(0, label.find_first_not_of(" \t"));
            label.erase(label.find_last_not_of(" \t") + 1);
            if (!label.empty()) {
                labels.push_back(label);
            }
        }
    }
    
    return labels;
}

std::optional<std::string> PropertyGraphManager::extractType_(const BaseEntity& edge) const {
    return edge.getFieldAsString("_type");
}

std::string PropertyGraphManager::makeLabelIndexKey_(std::string_view graph_id, std::string_view label, std::string_view pk) const {
    std::ostringstream oss;
    oss << "label:" << graph_id << ":" << label << ":" << pk;
    return oss.str();
}

std::string PropertyGraphManager::makeTypeIndexKey_(std::string_view graph_id, std::string_view type, std::string_view edgeId) const {
    std::ostringstream oss;
    oss << "type:" << graph_id << ":" << type << ":" << edgeId;
    return oss.str();
}

std::string PropertyGraphManager::makeNodeKey_(std::string_view graph_id, std::string_view pk) const {
    std::ostringstream oss;
    oss << "node:" << graph_id << ":" << pk;
    return oss.str();
}

std::string PropertyGraphManager::makeEdgeKey_(std::string_view graph_id, std::string_view edgeId) const {
    std::ostringstream oss;
    oss << "edge:" << graph_id << ":" << edgeId;
    return oss.str();
}

std::string PropertyGraphManager::makeGraphOutdexKey_(std::string_view graph_id, std::string_view fromPk, std::string_view edgeId) const {
    std::ostringstream oss;
    oss << "graph:out:" << graph_id << ":" << fromPk << ":" << edgeId;
    return oss.str();
}

std::string PropertyGraphManager::makeGraphIndegKey_(std::string_view graph_id, std::string_view toPk, std::string_view edgeId) const {
    std::ostringstream oss;
    oss << "graph:in:" << graph_id << ":" << toPk << ":" << edgeId;
    return oss.str();
}

// ===== Node Label Operations =====

PropertyGraphManager::Status PropertyGraphManager::addNode(const BaseEntity& node, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("addNode: Database not open");
    }

    auto pkOpt = node.getFieldAsString("id");
    if (!pkOpt.has_value()) {
        return Status::Error("addNode: Node must have 'id' field");
    }
    const std::string& pk = *pkOpt;

    // Extract labels
    std::vector<std::string> labels = extractLabels_(node);

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("addNode: Could not create write batch");
    }

    // Store node entity
    std::string nodeKey = makeNodeKey_(graph_id, pk);
    batch->put(nodeKey, node.serialize());

    // Create label index entries
    for (const auto& label : labels) {
        std::string labelKey = makeLabelIndexKey_(graph_id, label, pk);
        batch->put(labelKey, std::vector<uint8_t>());  // Empty value (index only needs key)
    }

    if (!batch->commit()) {
        return Status::Error("addNode: Failed to commit write batch");
    }

    return Status::OK();
}

PropertyGraphManager::Status PropertyGraphManager::deleteNode(std::string_view pk, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("deleteNode: Database not open");
    }

    // Load node to get labels
    std::string nodeKey = makeNodeKey_(graph_id, pk);
    auto blob = db_.get(nodeKey);
    if (!blob.has_value()) {
        return Status::OK();  // Already deleted (idempotent)
    }

    BaseEntity node = BaseEntity::deserialize(std::string(pk), *blob);
    std::vector<std::string> labels = extractLabels_(node);

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("deleteNode: Could not create write batch");
    }

    // Delete node entity
    batch->del(nodeKey);

    // Delete label index entries
    for (const auto& label : labels) {
        std::string labelKey = makeLabelIndexKey_(graph_id, label, pk);
        batch->del(labelKey);
    }

    if (!batch->commit()) {
        return Status::Error("deleteNode: Failed to commit write batch");
    }

    return Status::OK();
}

PropertyGraphManager::Status PropertyGraphManager::addNodeLabel(std::string_view pk, std::string_view label, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("addNodeLabel: Database not open");
    }

    // Load node
    std::string nodeKey = makeNodeKey_(graph_id, pk);
    auto blob = db_.get(nodeKey);
    if (!blob.has_value()) {
        return Status::Error("addNodeLabel: Node not found");
    }

    BaseEntity node = BaseEntity::deserialize(std::string(pk), *blob);
    std::vector<std::string> labels = extractLabels_(node);

    // Check if label already exists
    if (std::find(labels.begin(), labels.end(), label) != labels.end()) {
        return Status::OK();  // Label already exists (idempotent)
    }

    // Add label to node
    labels.push_back(std::string(label));
    std::string labelsStr;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) labelsStr += ",";
        labelsStr += labels[i];
    }
    node.setField("_labels", labelsStr);

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("addNodeLabel: Could not create write batch");
    }

    // Update node entity
    batch->put(nodeKey, node.serialize());

    // Add label index entry
    std::string labelKey = makeLabelIndexKey_(graph_id, label, pk);
    batch->put(labelKey, std::vector<uint8_t>());

    if (!batch->commit()) {
        return Status::Error("addNodeLabel: Failed to commit write batch");
    }

    return Status::OK();
}

PropertyGraphManager::Status PropertyGraphManager::removeNodeLabel(std::string_view pk, std::string_view label, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("removeNodeLabel: Database not open");
    }

    // Load node
    std::string nodeKey = makeNodeKey_(graph_id, pk);
    auto blob = db_.get(nodeKey);
    if (!blob.has_value()) {
        return Status::Error("removeNodeLabel: Node not found");
    }

    BaseEntity node = BaseEntity::deserialize(std::string(pk), *blob);
    std::vector<std::string> labels = extractLabels_(node);

    // Remove label
    auto it = std::find(labels.begin(), labels.end(), label);
    if (it == labels.end()) {
        return Status::OK();  // Label doesn't exist (idempotent)
    }
    labels.erase(it);

    // Update labels string
    std::string labelsStr;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) labelsStr += ",";
        labelsStr += labels[i];
    }
    node.setField("_labels", labelsStr);

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("removeNodeLabel: Could not create write batch");
    }

    // Update node entity
    batch->put(nodeKey, node.serialize());

    // Delete label index entry
    std::string labelKey = makeLabelIndexKey_(graph_id, label, pk);
    batch->del(labelKey);

    if (!batch->commit()) {
        return Status::Error("removeNodeLabel: Failed to commit write batch");
    }

    return Status::OK();
}

std::pair<PropertyGraphManager::Status, bool> PropertyGraphManager::hasNodeLabel(
    std::string_view pk, std::string_view label, std::string_view graph_id) const {
    if (!db_.isOpen()) {
        return {Status::Error("hasNodeLabel: Database not open"), false};
    }

    // Check label index
    std::string labelKey = makeLabelIndexKey_(graph_id, label, pk);
    auto blob = db_.get(labelKey);
    return {Status::OK(), blob.has_value()};
}

std::pair<PropertyGraphManager::Status, std::vector<std::string>> PropertyGraphManager::getNodesByLabel(
    std::string_view label, std::string_view graph_id) const {
    if (!db_.isOpen()) {
        return {Status::Error("getNodesByLabel: Database not open"), {}};
    }

    std::vector<std::string> nodes;
    std::ostringstream oss;
    oss << "label:" << graph_id << ":" << label << ":";
    std::string prefix = oss.str();

    db_.scanPrefix(prefix, [&nodes, &prefix](std::string_view key, std::string_view /*val*/) {
        // Extract PK from key: label:<graph_id>:<label>:<pk>
        std::string keyStr(key);
        size_t lastColon = keyStr.rfind(':');
        if (lastColon != std::string::npos && lastColon >= prefix.size() - 1) {
            std::string pk = keyStr.substr(lastColon + 1);
            if (!pk.empty()) {
                nodes.push_back(pk);
            }
        }
        return true;
    });

    return {Status::OK(), nodes};
}

std::pair<PropertyGraphManager::Status, std::vector<std::string>> PropertyGraphManager::getNodeLabels(
    std::string_view pk, std::string_view graph_id) const {
    if (!db_.isOpen()) {
        return {Status::Error("getNodeLabels: Database not open"), {}};
    }

    // Load node and extract labels
    std::string nodeKey = makeNodeKey_(graph_id, pk);
    auto blob = db_.get(nodeKey);
    if (!blob.has_value()) {
        return {Status::Error("getNodeLabels: Node not found"), {}};
    }

    BaseEntity node = BaseEntity::deserialize(std::string(pk), *blob);
    std::vector<std::string> labels = extractLabels_(node);

    return {Status::OK(), labels};
}

// ===== Relationship Type Operations =====

PropertyGraphManager::Status PropertyGraphManager::addEdge(const BaseEntity& edge, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("addEdge: Database not open");
    }

    auto edgeIdOpt = edge.getFieldAsString("id");
    auto fromOpt = edge.getFieldAsString("_from");
    auto toOpt = edge.getFieldAsString("_to");
    
    if (!edgeIdOpt || !fromOpt || !toOpt) {
        return Status::Error("addEdge: Edge must have 'id', '_from', and '_to' fields");
    }

    const std::string& edgeId = *edgeIdOpt;
    const std::string& from = *fromOpt;
    const std::string& to = *toOpt;

    // Extract type (optional)
    std::optional<std::string> typeOpt = extractType_(edge);

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("addEdge: Could not create write batch");
    }

    // Store edge entity
    std::string edgeKey = makeEdgeKey_(graph_id, edgeId);
    batch->put(edgeKey, edge.serialize());

    // Create graph adjacency indices
    std::string outdexKey = makeGraphOutdexKey_(graph_id, from, edgeId);
    std::string indegKey = makeGraphIndegKey_(graph_id, to, edgeId);
    batch->put(outdexKey, std::vector<uint8_t>(to.begin(), to.end()));
    batch->put(indegKey, std::vector<uint8_t>(from.begin(), from.end()));

    // Create type index entry if type exists
    if (typeOpt.has_value()) {
        std::string typeKey = makeTypeIndexKey_(graph_id, *typeOpt, edgeId);
        batch->put(typeKey, std::vector<uint8_t>());
    }

    if (!batch->commit()) {
        return Status::Error("addEdge: Failed to commit write batch");
    }

    return Status::OK();
}

PropertyGraphManager::Status PropertyGraphManager::deleteEdge(std::string_view edgeId, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("deleteEdge: Database not open");
    }

    // Load edge to get _from, _to, _type
    std::string edgeKey = makeEdgeKey_(graph_id, edgeId);
    auto blob = db_.get(edgeKey);
    if (!blob.has_value()) {
        return Status::OK();  // Already deleted (idempotent)
    }

    BaseEntity edge = BaseEntity::deserialize(std::string(edgeId), *blob);
    auto fromOpt = edge.getFieldAsString("_from");
    auto toOpt = edge.getFieldAsString("_to");
    std::optional<std::string> typeOpt = extractType_(edge);

    if (!fromOpt || !toOpt) {
        return Status::Error("deleteEdge: Edge has no _from/_to fields");
    }

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("deleteEdge: Could not create write batch");
    }

    // Delete edge entity
    batch->del(edgeKey);

    // Delete graph adjacency indices
    std::string outdexKey = makeGraphOutdexKey_(graph_id, *fromOpt, edgeId);
    std::string indegKey = makeGraphIndegKey_(graph_id, *toOpt, edgeId);
    batch->del(outdexKey);
    batch->del(indegKey);

    // Delete type index entry if type exists
    if (typeOpt.has_value()) {
        std::string typeKey = makeTypeIndexKey_(graph_id, *typeOpt, edgeId);
        batch->del(typeKey);
    }

    if (!batch->commit()) {
        return Status::Error("deleteEdge: Failed to commit write batch");
    }

    return Status::OK();
}

std::pair<PropertyGraphManager::Status, std::vector<PropertyGraphManager::EdgeInfo>> 
PropertyGraphManager::getEdgesByType(std::string_view type, std::string_view graph_id) const {
    if (!db_.isOpen()) {
        return {Status::Error("getEdgesByType: Database not open"), {}};
    }

    std::vector<EdgeInfo> edges;
    std::ostringstream oss;
    oss << "type:" << graph_id << ":" << type << ":";
    std::string prefix = oss.str();

    db_.scanPrefix(prefix, [this, &edges, &prefix, &graph_id](std::string_view key, std::string_view /*val*/) {
        // Extract edgeId from key: type:<graph_id>:<type>:<edgeId>
        std::string keyStr(key);
        size_t lastColon = keyStr.rfind(':');
        if (lastColon != std::string::npos && lastColon >= prefix.size() - 1) {
            std::string edgeId = keyStr.substr(lastColon + 1);
            if (edgeId.empty()) return true;
            
            // Load edge entity to get _from, _to
            std::string edgeKey = makeEdgeKey_(graph_id, edgeId);
            auto blob = db_.get(edgeKey);
            if (blob.has_value()) {
                BaseEntity edge = BaseEntity::deserialize(edgeId, *blob);
                auto fromOpt = edge.getFieldAsString("_from");
                auto toOpt = edge.getFieldAsString("_to");
                auto typeOpt = extractType_(edge);
                
                if (fromOpt && toOpt && typeOpt) {
                    edges.push_back({
                        edgeId,
                        *fromOpt,
                        *toOpt,
                        *typeOpt,
                        std::string(graph_id)
                    });
                }
            }
        }
        return true;
    });

    return {Status::OK(), edges};
}

std::pair<PropertyGraphManager::Status, std::string> PropertyGraphManager::getEdgeType(
    std::string_view edgeId, std::string_view graph_id) const {
    if (!db_.isOpen()) {
        return {Status::Error("getEdgeType: Database not open"), ""};
    }

    std::string edgeKey = makeEdgeKey_(graph_id, edgeId);
    auto blob = db_.get(edgeKey);
    if (!blob.has_value()) {
        return {Status::Error("getEdgeType: Edge not found"), ""};
    }

    BaseEntity edge = BaseEntity::deserialize(std::string(edgeId), *blob);
    std::optional<std::string> typeOpt = extractType_(edge);
    
    if (!typeOpt.has_value()) {
        return {Status::Error("getEdgeType: Edge has no _type field"), ""};
    }

    return {Status::OK(), *typeOpt};
}

std::pair<PropertyGraphManager::Status, std::vector<PropertyGraphManager::EdgeInfo>>
PropertyGraphManager::getTypedOutEdges(
    std::string_view fromPk,
    std::string_view type,
    std::string_view graph_id) const {
    if (!db_.isOpen()) {
        return {Status::Error("getTypedOutEdges: Database not open"), {}};
    }

    std::vector<EdgeInfo> edges;
    std::ostringstream oss;
    oss << "graph:out:" << graph_id << ":" << fromPk << ":";
    std::string prefix = oss.str();

    db_.scanPrefix(prefix, [this, &edges, &type, &graph_id, &fromPk](std::string_view key, std::string_view val) {
        // Extract edgeId from key: graph:out:<graph_id>:<from_pk>:<edgeId>
        std::string keyStr(key);
        size_t lastColon = keyStr.rfind(':');
        if (lastColon == std::string::npos) return true;
        
        std::string edgeId = keyStr.substr(lastColon + 1);
        std::string toPk(val);
        
        // Load edge to check type
        std::string edgeKey = makeEdgeKey_(graph_id, edgeId);
        auto blob = db_.get(edgeKey);
        if (!blob.has_value()) return true;
        
        BaseEntity edge = BaseEntity::deserialize(edgeId, *blob);
        std::optional<std::string> edgeTypeOpt = extractType_(edge);
        
        // Filter by type
        if (edgeTypeOpt.has_value() && *edgeTypeOpt == type) {
            edges.push_back({
                edgeId,
                std::string(fromPk),
                toPk,
                *edgeTypeOpt,
                std::string(graph_id)
            });
        }
        
        return true;
    });

    return {Status::OK(), edges};
}

// ===== Multi-Graph Federation =====

std::pair<PropertyGraphManager::Status, std::vector<std::string>> PropertyGraphManager::listGraphs() const {
    if (!db_.isOpen()) {
        return {Status::Error("listGraphs: Database not open"), {}};
    }

    std::unordered_set<std::string> graphSet;
    
    // Scan node keys: node:<graph_id>:*
    db_.scanPrefix("node:", [&graphSet](std::string_view key, std::string_view /*val*/) {
        std::string keyStr(key);
        // Extract graph_id from key: node:<graph_id>:<pk>
        size_t firstColon = keyStr.find(':');
        size_t secondColon = keyStr.find(':', firstColon + 1);
        if (firstColon != std::string::npos && secondColon != std::string::npos) {
            std::string graphId = keyStr.substr(firstColon + 1, secondColon - firstColon - 1);
            graphSet.insert(graphId);
        }
        return true;
    });

    std::vector<std::string> graphs(graphSet.begin(), graphSet.end());
    std::sort(graphs.begin(), graphs.end());
    
    return {Status::OK(), graphs};
}

std::pair<PropertyGraphManager::Status, PropertyGraphManager::GraphStats> 
PropertyGraphManager::getGraphStats(std::string_view graph_id) const {
    if (!db_.isOpen()) {
        return {Status::Error("getGraphStats: Database not open"), {}};
    }

    GraphStats stats;
    stats.graph_id = std::string(graph_id);
    stats.node_count = 0;
    stats.edge_count = 0;
    
    std::unordered_set<std::string> labels;
    std::unordered_set<std::string> types;

    // Count nodes
    std::ostringstream nodePrefix;
    nodePrefix << "node:" << graph_id << ":";
    db_.scanPrefix(nodePrefix.str(), [&stats](std::string_view /*key*/, std::string_view /*val*/) {
        stats.node_count++;
        return true;
    });

    // Count edges
    std::ostringstream edgePrefix;
    edgePrefix << "edge:" << graph_id << ":";
    db_.scanPrefix(edgePrefix.str(), [&stats](std::string_view /*key*/, std::string_view /*val*/) {
        stats.edge_count++;
        return true;
    });

    // Count unique labels
    std::ostringstream labelPrefix;
    labelPrefix << "label:" << graph_id << ":";
    db_.scanPrefix(labelPrefix.str(), [&labels, &labelPrefix](std::string_view key, std::string_view /*val*/) {
        std::string keyStr(key);
        // Extract label from key: label:<graph_id>:<label>:<pk>
        size_t prefixLen = labelPrefix.str().size();
        size_t nextColon = keyStr.find(':', prefixLen);
        if (nextColon != std::string::npos) {
            std::string label = keyStr.substr(prefixLen, nextColon - prefixLen);
            labels.insert(label);
        }
        return true;
    });
    stats.label_count = labels.size();

    // Count unique types
    std::ostringstream typePrefix;
    typePrefix << "type:" << graph_id << ":";
    db_.scanPrefix(typePrefix.str(), [&types, &typePrefix](std::string_view key, std::string_view /*val*/) {
        std::string keyStr(key);
        // Extract type from key: type:<graph_id>:<type>:<edgeId>
        size_t prefixLen = typePrefix.str().size();
        size_t nextColon = keyStr.find(':', prefixLen);
        if (nextColon != std::string::npos) {
            std::string type = keyStr.substr(prefixLen, nextColon - prefixLen);
            types.insert(type);
        }
        return true;
    });
    stats.type_count = types.size();

    return {Status::OK(), stats};
}

std::pair<PropertyGraphManager::Status, PropertyGraphManager::FederationResult>
PropertyGraphManager::federatedQuery(const std::vector<FederationPattern>& patterns) const {
    if (!db_.isOpen()) {
        return {Status::Error("federatedQuery: Database not open"), {}};
    }

    FederationResult result;

    for (const auto& pattern : patterns) {
        if (pattern.pattern_type == "node") {
            // Query nodes by label
            auto [st, nodes] = getNodesByLabel(pattern.label_or_type, pattern.graph_id);
            if (!st.ok) {
                return {st, {}};
            }
            
            for (const auto& pk : nodes) {
                result.nodes.push_back({pk, {pattern.label_or_type}, pattern.graph_id});
            }
        } else if (pattern.pattern_type == "edge") {
            // Query edges by type
            auto [st, edges] = getEdgesByType(pattern.label_or_type, pattern.graph_id);
            if (!st.ok) {
                return {st, {}};
            }
            
            result.edges.insert(result.edges.end(), edges.begin(), edges.end());
        }
    }

    return {Status::OK(), result};
}

// ===== Batch Operations =====

PropertyGraphManager::Status PropertyGraphManager::addNodesBatch(
    const std::vector<BaseEntity>& nodes, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("addNodesBatch: Database not open");
    }

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("addNodesBatch: Could not create write batch");
    }

    for (const auto& node : nodes) {
        auto pkOpt = node.getFieldAsString("id");
        if (!pkOpt.has_value()) {
            batch->rollback();
            return Status::Error("addNodesBatch: Node missing 'id' field");
        }
        const std::string& pk = *pkOpt;

        // Store node
        std::string nodeKey = makeNodeKey_(graph_id, pk);
        batch->put(nodeKey, node.serialize());

        // Create label indices
        std::vector<std::string> labels = extractLabels_(node);
        for (const auto& label : labels) {
            std::string labelKey = makeLabelIndexKey_(graph_id, label, pk);
            batch->put(labelKey, std::vector<uint8_t>());
        }
    }

    if (!batch->commit()) {
        return Status::Error("addNodesBatch: Failed to commit write batch");
    }

    return Status::OK();
}

PropertyGraphManager::Status PropertyGraphManager::addEdgesBatch(
    const std::vector<BaseEntity>& edges, std::string_view graph_id) {
    if (!db_.isOpen()) {
        return Status::Error("addEdgesBatch: Database not open");
    }

    auto batch = db_.createWriteBatch();
    if (!batch) {
        return Status::Error("addEdgesBatch: Could not create write batch");
    }

    for (const auto& edge : edges) {
        auto edgeIdOpt = edge.getFieldAsString("id");
        auto fromOpt = edge.getFieldAsString("_from");
        auto toOpt = edge.getFieldAsString("_to");
        
        if (!edgeIdOpt || !fromOpt || !toOpt) {
            batch->rollback();
            return Status::Error("addEdgesBatch: Edge missing 'id', '_from', or '_to' field");
        }

        const std::string& edgeId = *edgeIdOpt;
        const std::string& from = *fromOpt;
        const std::string& to = *toOpt;

        // Store edge
        std::string edgeKey = makeEdgeKey_(graph_id, edgeId);
        batch->put(edgeKey, edge.serialize());

        // Create adjacency indices
        std::string outdexKey = makeGraphOutdexKey_(graph_id, from, edgeId);
        std::string indegKey = makeGraphIndegKey_(graph_id, to, edgeId);
        batch->put(outdexKey, std::vector<uint8_t>(to.begin(), to.end()));
        batch->put(indegKey, std::vector<uint8_t>(from.begin(), from.end()));

        // Create type index if present
        std::optional<std::string> typeOpt = extractType_(edge);
        if (typeOpt.has_value()) {
            std::string typeKey = makeTypeIndexKey_(graph_id, *typeOpt, edgeId);
            batch->put(typeKey, std::vector<uint8_t>());
        }
    }

    if (!batch->commit()) {
        return Status::Error("addEdgesBatch: Failed to commit write batch");
    }

    return Status::OK();
}

} // namespace themis
