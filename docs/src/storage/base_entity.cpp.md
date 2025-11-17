# base_entity.cpp

Path: `src/storage/base_entity.cpp`

Purpose: Base classes and helpers for entities persisted in storage layer.

Public functions / symbols:
- `: primary_key_(pk) {`
- `if (cache_valid_ && field_cache_) {`
- `if (format_ == Format::JSON) {`
- `if constexpr (std::is_same_v<T, std::string>) {`
- `if constexpr (std::is_same_v<T, int64_t>) {`
- `if constexpr (std::is_same_v<T, double>) {`
- `if constexpr (std::is_same_v<T, bool>) {`
- `if constexpr (std::is_same_v<T, std::vector<float>>) {`
- ``
- `if constexpr (std::is_same_v<T, std::monostate>) {`
- `if (!cache_valid_ || !field_cache_) {`
- `for (const auto& [name, value] : *field_cache_) {`
- `if (c == '{' || c == '[') {`
- `rebuildBlob();`
- `invalidateCache();`
- `ensureCache();`
- `std::string key_str(key);`
- `utils::Serialization::Decoder decoder(blob_);`
- `BaseEntity entity(pk);`
- `return BaseEntity(pk, fields);`
- `return BaseEntity(pk, blob, fmt);`
- `return getFieldAsString(field_name);`
- `return getFieldAsVector(field_name);`

