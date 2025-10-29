#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace themis {

/// Key schema definitions for multi-model storage
/// All data models (relational, document, graph, vector) map to key-value pairs
class KeySchema {
public:
    /// Key types for different data models
    enum class KeyType : uint8_t {
        RELATIONAL,   // table_name:pk_value
        DOCUMENT,     // collection_name:pk_value
        GRAPH_NODE,   // node:pk_value
        GRAPH_EDGE,   // edge:pk_value
        VECTOR,       // object_name:pk_value
        
        // Index keys
        SECONDARY_INDEX,  // idx:table:column:value:pk
        GRAPH_OUTDEX,     // graph:out:pk_start:pk_edge
        GRAPH_INDEX,      // graph:in:pk_target:pk_edge
    };

    /// Construct key for relational table row
    static std::string makeRelationalKey(std::string_view table, std::string_view pk);
    
    /// Construct key for document
    static std::string makeDocumentKey(std::string_view collection, std::string_view pk);
    
    /// Construct key for graph node
    static std::string makeGraphNodeKey(std::string_view pk);
    
    /// Construct key for graph edge
    static std::string makeGraphEdgeKey(std::string_view pk);
    
    /// Construct key for vector object
    static std::string makeVectorKey(std::string_view object_name, std::string_view pk);
    
    /// Construct key for secondary index entry
    static std::string makeSecondaryIndexKey(
        std::string_view table,
        std::string_view column,
        std::string_view value,
        std::string_view pk
    );
    
    /// Construct key for graph outdex (outgoing edges)
    static std::string makeGraphOutdexKey(
        std::string_view pk_start,
        std::string_view pk_edge
    );
    
    /// Construct key for graph index (incoming edges)
    static std::string makeGraphIndexKey(
        std::string_view pk_target,
        std::string_view pk_edge
    );
    
    /// Parse key type from key string
    static KeyType parseKeyType(std::string_view key);
    
    /// Extract primary key from any key type
    static std::string extractPrimaryKey(std::string_view key);
    
private:
    static constexpr char SEPARATOR = ':';
};

} // namespace themis
