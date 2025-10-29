#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <map>
#include <variant>

namespace themis {

/// Value type that can represent different data types
using Value = std::variant<
    std::monostate,           // null
    bool,                      // boolean
    int64_t,                   // integer
    double,                    // floating point
    std::string,               // string
    std::vector<float>,        // float vector (for embeddings)
    std::vector<uint8_t>       // binary blob
>;

/// Base Entity: The canonical storage unit for all data models
/// Each logical entity (row, document, node, edge, vector object) is stored as one blob
/// 
/// Architecture:
/// - Storage format: Custom binary serialization (similar to VelocyPack/MessagePack)
/// - Fast field extraction: simdjson on-demand parsing for index updates
/// - Multi-model support: Flexible schema-less document model
class BaseEntity {
public:
    using Blob = std::vector<uint8_t>;
    using Attributes = std::map<std::string, std::string>;
    using FieldMap = std::map<std::string, Value>;
    
    /// Storage format type
    enum class Format {
        BINARY,   // Custom binary format (fast, compact)
        JSON      // JSON text (human-readable, for compatibility)
    };
    
    BaseEntity() = default;
    explicit BaseEntity(std::string_view pk);
    BaseEntity(std::string_view pk, const FieldMap& fields);
    BaseEntity(std::string_view pk, Blob blob, Format format = Format::BINARY);
    
    /// Get primary key
    const std::string& getPrimaryKey() const { return primary_key_; }
    
    /// Set primary key
    void setPrimaryKey(std::string_view pk) { primary_key_ = pk; }
    
    /// Get binary blob
    const Blob& getBlob() const { return blob_; }
    
    /// Set binary blob (invalidates cache)
    void setBlob(Blob blob, Format format = Format::BINARY);
    
    /// Get storage format
    Format getFormat() const { return format_; }
    
    // ===== Field Access (lazy parsing) =====
    
    /// Check if field exists
    bool hasField(std::string_view field_name) const;
    
    /// Get field value (returns nullopt if not found)
    std::optional<Value> getField(std::string_view field_name) const;
    
    /// Get field as string (with type conversion)
    std::optional<std::string> getFieldAsString(std::string_view field_name) const;
    
    /// Get field as int64
    std::optional<int64_t> getFieldAsInt(std::string_view field_name) const;
    
    /// Get field as double
    std::optional<double> getFieldAsDouble(std::string_view field_name) const;
    
    /// Get field as bool
    std::optional<bool> getFieldAsBool(std::string_view field_name) const;
    
    /// Get field as float vector (for embeddings)
    std::optional<std::vector<float>> getFieldAsVector(std::string_view field_name) const;
    
    /// Set field value (modifies blob)
    void setField(std::string_view field_name, const Value& value);
    
    /// Get all fields (full parse)
    FieldMap getAllFields() const;
    
    // ===== Serialization =====
    
    /// Serialize current fields to binary blob
    Blob serialize() const;
    
    /// Serialize to JSON string
    std::string toJson() const;
    
    /// Create from JSON string (using simdjson for speed)
    static BaseEntity fromJson(std::string_view pk, std::string_view json_str);
    
    /// Create from field map
    static BaseEntity fromFields(std::string_view pk, const FieldMap& fields);
    
    /// Deserialize from binary blob
    static BaseEntity deserialize(std::string_view pk, const Blob& blob);
    
    // ===== Index Support (fast field extraction) =====
    
    /// Extract specific field without full deserialization
    /// Critical for index updates - uses simdjson on-demand API
    std::optional<std::string> extractField(std::string_view field_name) const;
    
    /// Extract vector embedding field (for ANN index)
    std::optional<std::vector<float>> extractVector(std::string_view field_name = "embedding") const;
    
    /// Get all indexable fields (for secondary index maintenance)
    /// Returns field_name -> string_value pairs
    Attributes extractAllFields() const;
    
    /// Extract fields matching a prefix (e.g., "metadata.*")
    Attributes extractFieldsWithPrefix(std::string_view prefix) const;
    
    // ===== Metadata =====
    
    /// Get blob size in bytes
    size_t getBlobSize() const { return blob_.size(); }
    
    /// Check if entity is empty
    bool isEmpty() const { return blob_.empty(); }
    
    /// Clear all data
    void clear();
    
private:
    std::string primary_key_;
    Blob blob_;
    Format format_ = Format::BINARY;
    
    // Lazy-parsed field cache (shared_ptr für Copy-Semantik)
    mutable std::shared_ptr<FieldMap> field_cache_;
    mutable bool cache_valid_ = false;
    
    // Parse blob into field cache
    void ensureCache() const;
    
    // Invalidate cache (after blob modification)
    void invalidateCache();
    
    // Parse JSON using simdjson (fast path for JSON format)
    FieldMap parseJson() const;
    
    // Parse binary format
    FieldMap parseBinary() const;
    
    // Rebuild blob from cache
    void rebuildBlob();
};

} // namespace themis
