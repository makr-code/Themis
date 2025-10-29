#include "storage/key_schema.h"
#include <sstream>

namespace themis {

std::string KeySchema::makeRelationalKey(std::string_view table, std::string_view pk) {
    std::ostringstream oss;
    oss << table << SEPARATOR << pk;
    return oss.str();
}

std::string KeySchema::makeDocumentKey(std::string_view collection, std::string_view pk) {
    std::ostringstream oss;
    oss << collection << SEPARATOR << pk;
    return oss.str();
}

std::string KeySchema::makeGraphNodeKey(std::string_view pk) {
    std::ostringstream oss;
    oss << "node" << SEPARATOR << pk;
    return oss.str();
}

std::string KeySchema::makeGraphEdgeKey(std::string_view pk) {
    std::ostringstream oss;
    oss << "edge" << SEPARATOR << pk;
    return oss.str();
}

std::string KeySchema::makeVectorKey(std::string_view object_name, std::string_view pk) {
    std::ostringstream oss;
    oss << object_name << SEPARATOR << pk;
    return oss.str();
}

std::string KeySchema::makeSecondaryIndexKey(
    std::string_view table,
    std::string_view column,
    std::string_view value,
    std::string_view pk
) {
    std::ostringstream oss;
    oss << "idx" << SEPARATOR << table << SEPARATOR << column << SEPARATOR << value << SEPARATOR << pk;
    return oss.str();
}

std::string KeySchema::makeGraphOutdexKey(std::string_view pk_start, std::string_view pk_edge) {
    std::ostringstream oss;
    oss << "graph" << SEPARATOR << "out" << SEPARATOR << pk_start << SEPARATOR << pk_edge;
    return oss.str();
}

std::string KeySchema::makeGraphIndexKey(std::string_view pk_target, std::string_view pk_edge) {
    std::ostringstream oss;
    oss << "graph" << SEPARATOR << "in" << SEPARATOR << pk_target << SEPARATOR << pk_edge;
    return oss.str();
}

KeySchema::KeyType KeySchema::parseKeyType(std::string_view key) {
    if (key.starts_with("idx:")) return KeyType::SECONDARY_INDEX;
    if (key.starts_with("graph:out:")) return KeyType::GRAPH_OUTDEX;
    if (key.starts_with("graph:in:")) return KeyType::GRAPH_INDEX;
    if (key.starts_with("node:")) return KeyType::GRAPH_NODE;
    if (key.starts_with("edge:")) return KeyType::GRAPH_EDGE;
    
    // Default: check if it looks like table:pk or collection:pk
    return KeyType::RELATIONAL; // or DOCUMENT - ambiguous without schema
}

std::string KeySchema::extractPrimaryKey(std::string_view key) {
    auto last_sep = key.rfind(SEPARATOR);
    if (last_sep != std::string_view::npos) {
        return std::string(key.substr(last_sep + 1));
    }
    return std::string(key);
}

} // namespace themis
