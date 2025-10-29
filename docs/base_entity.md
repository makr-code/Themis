# Base Entity Layer - Implementation Guide

## Overview

The Base Entity layer is the canonical storage unit for all data models in THEMIS. Each logical entity (row, document, node, edge, vector object) is stored as a single binary blob with efficient serialization and fast field extraction capabilities.

## Key Features

### 1. Multi-Format Support
- **Binary Format**: Compact custom serialization (VelocyPack-like) for efficient storage
- **JSON Format**: Human-readable format for compatibility and debugging
- **Automatic Conversion**: Seamless conversion between formats

### 2. Type System
The `Value` variant supports:
- `std::monostate` - null values
- `bool` - boolean values
- `int64_t` - integer values
- `double` - floating-point values
- `std::string` - text strings
- `std::vector<float>` - float vectors (optimized for embeddings)
- `std::vector<uint8_t>` - binary blobs

### 3. Fast Field Extraction (Critical for Index Updates)

```cpp
// Fast path: Extract single field without full parse (using simdjson)
auto name = entity.extractField("name");

// Extract all fields for secondary index maintenance
auto attrs = entity.extractAllFields();

// Extract vector embedding for ANN index
auto embedding = entity.extractVector("embedding");

// Extract fields with prefix (e.g., metadata.*)
auto meta = entity.extractFieldsWithPrefix("meta_");
```

## Usage Examples

### Creating Entities

#### From Field Map
```cpp
BaseEntity::FieldMap fields;
fields["id"] = std::string("user_123");
fields["name"] = std::string("Alice");
fields["age"] = int64_t(30);
fields["active"] = true;
fields["score"] = 95.5;

BaseEntity entity = BaseEntity::fromFields("user_123", fields);
```

#### From JSON
```cpp
std::string json = R"({"name":"Alice","age":30,"active":true})";
BaseEntity entity = BaseEntity::fromJson("user_123", json);
```

### Field Access

```cpp
// Type-safe field access
auto name = entity.getFieldAsString("name");  // std::optional<std::string>
auto age = entity.getFieldAsInt("age");       // std::optional<int64_t>
auto score = entity.getFieldAsDouble("score"); // std::optional<double>
auto active = entity.getFieldAsBool("active"); // std::optional<bool>

// Check existence
if (entity.hasField("email")) {
    // ...
}

// Generic field access
auto value = entity.getField("name");  // std::optional<Value>
```

### Serialization

```cpp
// Serialize to binary blob for storage
auto blob = entity.serialize();

// Store in RocksDB
db.put(key, blob);

// Deserialize from blob
auto retrieved = BaseEntity::deserialize("user_123", blob);

// Convert to JSON for debugging/export
std::string json = entity.toJson();
```

### Vector Embeddings

```cpp
// Create entity with embedding
std::vector<float> embedding = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
entity.setField("embedding", embedding);

// Fast extraction for ANN index
auto vec = entity.extractVector("embedding");
if (vec) {
    // Add to HNSW index
    ann_index.add(*vec, entity.getPrimaryKey());
}
```

## Performance Considerations

### Lazy Parsing
- Field cache is populated on first access
- Subsequent field accesses use the cache
- Cache is invalidated when blob is modified

### simdjson Integration
- JSON format uses simdjson on-demand API for maximum speed
- Field extraction from JSON is O(1) without full parse
- Parsing multi-GB/s throughput on modern CPUs

### Binary Format Efficiency
- Compact serialization (smaller than JSON)
- No parsing overhead for field access
- Optimized for float vectors (no extra encoding)

## Storage Format Details

### Binary Format Structure
```
<Object>
  <num_fields: uint32>
  <field_1>
    <name_length: uint32>
    <name: bytes>
    <type_tag: uint8>
    <value: varies by type>
  <field_2>
    ...
```

### Type Tags
```cpp
NULL_VALUE  = 0x00
BOOL_FALSE  = 0x01
BOOL_TRUE   = 0x02
INT32       = 0x10
INT64       = 0x11
UINT32      = 0x12
UINT64      = 0x13
FLOAT       = 0x20
DOUBLE      = 0x21
STRING      = 0x30
BINARY      = 0x40
ARRAY       = 0x50
OBJECT      = 0x60
VECTOR_FLOAT = 0x70  // Optimized for embeddings
```

## Index Integration

### Secondary Index Updates
```cpp
// Extract all fields for index maintenance
auto attrs = entity.extractAllFields();

auto batch = db.createWriteBatch();

// Update base entity
batch->put(entity_key, entity.serialize());

// Update secondary indexes
for (const auto& [field, value] : attrs) {
    std::string idx_key = KeySchema::makeSecondaryIndexKey(
        table, field, value, pk
    );
    batch->put(idx_key, pk_bytes);
}

batch->commit();  // ACID transaction
```

### Vector Index Updates
```cpp
// Extract embedding
auto embedding = entity.extractVector("embedding");
if (embedding) {
    // Update HNSW index
    hnsw_index.add(*embedding, entity.getPrimaryKey());
}

// Update base entity in RocksDB
db.put(entity_key, entity.serialize());
```

## Best Practices

1. **Use Binary Format for Storage**: Always serialize to binary for RocksDB storage (automatic)
2. **JSON for Debugging**: Use `toJson()` for logs and debugging only
3. **Field Extraction**: Use `extractField()` for index updates (faster than full parse)
4. **Batch Updates**: Use WriteBatch for atomic updates across entity + all indexes
5. **Vector Optimization**: Store embeddings as `std::vector<float>` for optimal performance

## Thread Safety

- Each `BaseEntity` instance is NOT thread-safe
- simdjson parser is thread-local (automatically handled)
- Safe to use different instances on different threads
- Use locks if sharing instances across threads

## Future Enhancements

- [ ] Compression support (LZ4/Snappy) for large blobs
- [ ] Schema validation (optional)
- [ ] Nested object support
- [ ] Custom type extensions
- [ ] Memory-mapped blob access for very large entities

## References

- simdjson: https://github.com/simdjson/simdjson
- VelocyPack: https://github.com/arangodb/velocypack
- Architecture Doc: `Hybride Datenbankarchitektur C++_Rust.txt`
