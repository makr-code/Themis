#include "storage/base_entity.h"
#include "utils/serialization.h"
#include "utils/logger.h"
#include <simdjson.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>

namespace themis {

// Global simdjson parser (thread-local for thread safety)
thread_local simdjson::ondemand::parser g_parser;

// ===== Constructors =====

BaseEntity::BaseEntity(std::string_view pk) : primary_key_(pk) {}

BaseEntity::BaseEntity(std::string_view pk, const FieldMap& fields) 
    : primary_key_(pk) {
    field_cache_ = std::make_shared<FieldMap>(fields);
    cache_valid_ = true;
    rebuildBlob();
}

BaseEntity::BaseEntity(std::string_view pk, Blob blob, Format format) 
    : primary_key_(pk), blob_(std::move(blob)), format_(format) {}

// ===== Blob Management =====

void BaseEntity::setBlob(Blob blob, Format format) {
    blob_ = std::move(blob);
    format_ = format;
    invalidateCache();
}

void BaseEntity::clear() {
    primary_key_.clear();
    blob_.clear();
    invalidateCache();
}

// ===== Field Access =====

void BaseEntity::ensureCache() const {
    if (cache_valid_ && field_cache_) {
        return;
    }
    
    if (blob_.empty()) {
        field_cache_ = std::make_shared<FieldMap>();
        cache_valid_ = true;
        return;
    }
    
    try {
        if (format_ == Format::JSON) {
            field_cache_ = std::make_shared<FieldMap>(parseJson());
        } else {
            field_cache_ = std::make_shared<FieldMap>(parseBinary());
        }
        cache_valid_ = true;
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to parse entity blob: {}", e.what());
        field_cache_ = std::make_shared<FieldMap>();
        cache_valid_ = true;
    }
}

void BaseEntity::invalidateCache() {
    cache_valid_ = false;
    field_cache_.reset();
}

bool BaseEntity::hasField(std::string_view field_name) const {
    ensureCache();
    return field_cache_->find(std::string(field_name)) != field_cache_->end();
}

std::optional<Value> BaseEntity::getField(std::string_view field_name) const {
    ensureCache();
    auto it = field_cache_->find(std::string(field_name));
    if (it != field_cache_->end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> BaseEntity::getFieldAsString(std::string_view field_name) const {
    auto value = getField(field_name);
    if (!value) return std::nullopt;
    
    return std::visit([](auto&& arg) -> std::optional<std::string> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        }
        return std::nullopt;
    }, *value);
}

std::optional<int64_t> BaseEntity::getFieldAsInt(std::string_view field_name) const {
    auto value = getField(field_name);
    if (!value) return std::nullopt;
    
    return std::visit([](auto&& arg) -> std::optional<int64_t> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return arg;
        } else if constexpr (std::is_same_v<T, double>) {
            return static_cast<int64_t>(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? 1 : 0;
        }
        return std::nullopt;
    }, *value);
}

std::optional<double> BaseEntity::getFieldAsDouble(std::string_view field_name) const {
    auto value = getField(field_name);
    if (!value) return std::nullopt;
    
    return std::visit([](auto&& arg) -> std::optional<double> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, double>) {
            return arg;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<double>(arg);
        }
        return std::nullopt;
    }, *value);
}

std::optional<bool> BaseEntity::getFieldAsBool(std::string_view field_name) const {
    auto value = getField(field_name);
    if (!value) return std::nullopt;
    
    return std::visit([](auto&& arg) -> std::optional<bool> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            return arg;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return arg != 0;
        }
        return std::nullopt;
    }, *value);
}

std::optional<std::vector<float>> BaseEntity::getFieldAsVector(std::string_view field_name) const {
    auto value = getField(field_name);
    if (!value) return std::nullopt;
    
    return std::visit([](auto&& arg) -> std::optional<std::vector<float>> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::vector<float>>) {
            return arg;
        }
        return std::nullopt;
    }, *value);
}

void BaseEntity::setField(std::string_view field_name, const Value& value) {
    ensureCache();
    (*field_cache_)[std::string(field_name)] = value;
    rebuildBlob();
}

BaseEntity::FieldMap BaseEntity::getAllFields() const {
    ensureCache();
    return *field_cache_;
}

// ===== JSON Parsing (simdjson) =====

BaseEntity::FieldMap BaseEntity::parseJson() const {
    FieldMap fields;
    
    try {
        // Use simdjson on-demand API for maximum speed
        simdjson::padded_string padded(reinterpret_cast<const char*>(blob_.data()), blob_.size());
        simdjson::ondemand::document doc = g_parser.iterate(padded);
        simdjson::ondemand::object obj = doc.get_object();
        
        for (auto field : obj) {
            std::string_view key = field.unescaped_key();
            std::string key_str(key);
            
            simdjson::ondemand::value val = field.value();
            
            // Determine type and convert
            switch (val.type()) {
                case simdjson::ondemand::json_type::null:
                    fields[key_str] = std::monostate{};
                    break;
                    
                case simdjson::ondemand::json_type::boolean:
                    fields[key_str] = bool(val.get_bool());
                    break;
                    
                case simdjson::ondemand::json_type::number: {
                    // Try int first, then double
                    int64_t int_val;
                    if (val.get_int64().get(int_val) == simdjson::SUCCESS) {
                        fields[key_str] = int_val;
                    } else {
                        fields[key_str] = double(val.get_double());
                    }
                    break;
                }
                    
                case simdjson::ondemand::json_type::string: {
                    auto sv_res = val.get_string();
                    if (!sv_res.error()) {
                        fields[key_str] = std::string(sv_res.value());
                    }
                    break;
                }
                    
                case simdjson::ondemand::json_type::array: {
                    // Check if it's a float vector (for embeddings)
                    simdjson::ondemand::array arr = val.get_array();
                    std::vector<float> vec;
                    bool is_float_vec = true;
                    
                    for (auto elem : arr) {
                        double d;
                        if (elem.get_double().get(d) == simdjson::SUCCESS) {
                            vec.push_back(static_cast<float>(d));
                        } else {
                            is_float_vec = false;
                            break;
                        }
                    }
                    
                    if (is_float_vec && !vec.empty()) {
                        fields[key_str] = vec;
                    }
                    // Note: nested objects/arrays not fully supported yet
                    break;
                }
                    
                default:
                    // Skip objects and other complex types for now
                    break;
            }
        }
    } catch (const simdjson::simdjson_error& e) {
        THEMIS_ERROR("simdjson parse error: {}", e.what());
        throw std::runtime_error("JSON parse failed");
    }
    
    return fields;
}

// ===== Binary Parsing =====

BaseEntity::FieldMap BaseEntity::parseBinary() const {
    FieldMap fields;
    
    try {
        utils::Serialization::Decoder decoder(blob_);
        
        // Binary format: <num_fields> <field1> <field2> ...
        // Each field: <name_len> <name> <type_tag> <value>
        
        size_t num_fields = decoder.beginObject();
        
        for (size_t i = 0; i < num_fields; ++i) {
            std::string field_name = decoder.decodeString();
            auto type = decoder.peekType();
            
            switch (type) {
                case utils::Serialization::TypeTag::NULL_VALUE:
                    decoder.readTag();
                    fields[field_name] = std::monostate{};
                    break;
                    
                case utils::Serialization::TypeTag::BOOL_FALSE:
                case utils::Serialization::TypeTag::BOOL_TRUE:
                    fields[field_name] = decoder.decodeBool();
                    break;
                    
                case utils::Serialization::TypeTag::INT32:
                case utils::Serialization::TypeTag::INT64:
                    fields[field_name] = decoder.decodeInt64();
                    break;
                    
                case utils::Serialization::TypeTag::UINT32:
                case utils::Serialization::TypeTag::UINT64:
                    fields[field_name] = static_cast<int64_t>(decoder.decodeUInt64());
                    break;
                    
                case utils::Serialization::TypeTag::FLOAT:
                    fields[field_name] = static_cast<double>(decoder.decodeFloat());
                    break;
                    
                case utils::Serialization::TypeTag::DOUBLE:
                    fields[field_name] = decoder.decodeDouble();
                    break;
                    
                case utils::Serialization::TypeTag::STRING:
                    fields[field_name] = decoder.decodeString();
                    break;
                    
                case utils::Serialization::TypeTag::VECTOR_FLOAT:
                    fields[field_name] = decoder.decodeFloatVector();
                    break;
                    
                default:
                    // Skip unknown types
                    break;
            }
        }
        
        decoder.endObject();
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Binary parse error: {}", e.what());
        throw std::runtime_error("Binary parse failed");
    }
    
    return fields;
}

// ===== Serialization =====

void BaseEntity::rebuildBlob() {
    if (!field_cache_ || field_cache_->empty()) {
        blob_.clear();
        return;
    }
    
    // Always use binary format for storage (more efficient)
    utils::Serialization::Encoder encoder;
    
    encoder.beginObject(field_cache_->size());
    
    for (const auto& [name, value] : *field_cache_) {
        encoder.encodeString(name);
        
        std::visit([&encoder](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                encoder.encodeNull();
            } else if constexpr (std::is_same_v<T, bool>) {
                encoder.encodeBool(arg);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                encoder.encodeInt64(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                encoder.encodeDouble(arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
                encoder.encodeString(arg);
            } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                encoder.encodeFloatVector(arg);
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                encoder.encodeBinary(arg);
            }
        }, value);
    }
    
    encoder.endObject();
    blob_ = encoder.finish();
    format_ = Format::BINARY;
}

BaseEntity::Blob BaseEntity::serialize() const {
    if (!cache_valid_ || !field_cache_) {
        return blob_; // Return as-is
    }
    
    // Rebuild blob from cache if modified
    const_cast<BaseEntity*>(this)->rebuildBlob();
    return blob_;
}

std::string BaseEntity::toJson() const {
    ensureCache();
    
    std::ostringstream oss;
    oss << "{";
    
    bool first = true;
    for (const auto& [name, value] : *field_cache_) {
        if (!first) oss << ",";
        first = false;
        
        oss << "\"" << name << "\":";
        
        std::visit([&oss](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                oss << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                oss << (arg ? "true" : "false");
            } else if constexpr (std::is_same_v<T, int64_t>) {
                oss << arg;
            } else if constexpr (std::is_same_v<T, double>) {
                oss << arg;
            } else if constexpr (std::is_same_v<T, std::string>) {
                // Use nlohmann::json for proper string escaping (prevents injection)
                oss << nlohmann::json(arg).dump();
            } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                oss << "[";
                for (size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << arg[i];
                }
                oss << "]";
            }
        }, value);
    }
    
    oss << "}";
    return oss.str();
}

// ===== Factory Methods =====

BaseEntity BaseEntity::fromJson(std::string_view pk, std::string_view json_str) {
    BaseEntity entity(pk);
    entity.blob_ = Blob(json_str.begin(), json_str.end());
    entity.format_ = Format::JSON;
    return entity;
}

BaseEntity BaseEntity::fromFields(std::string_view pk, const FieldMap& fields) {
    return BaseEntity(pk, fields);
}

BaseEntity BaseEntity::deserialize(std::string_view pk, const Blob& blob) {
    // Heuristik: Erkennen, ob das Blob JSON oder binär ist
    Format fmt = Format::BINARY;
    if (!blob.empty()) {
        unsigned char c = blob[0];
        // Sehr einfache Erkennung: JSON-Objekt/Array beginnt mit '{' oder '['
        if (c == '{' || c == '[') {
            fmt = Format::JSON;
        }
    }
    return BaseEntity(pk, blob, fmt);
}

// ===== Index Support (Fast Extraction) =====

std::optional<std::string> BaseEntity::extractField(std::string_view field_name) const {
    // Always use cache for reliable field extraction
    // (simdjson on-demand can only be iterated once)
    return getFieldAsString(field_name);
}

std::optional<std::vector<float>> BaseEntity::extractVector(std::string_view field_name) const {
    return getFieldAsVector(field_name);
}

BaseEntity::Attributes BaseEntity::extractAllFields() const {
    ensureCache();
    
    Attributes attrs;
    for (const auto& [name, value] : *field_cache_) {
        // Convert value to string
        std::visit([&attrs, &name](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                attrs[name] = arg;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                attrs[name] = std::to_string(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                attrs[name] = std::to_string(arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                attrs[name] = arg ? "true" : "false";
            }
        }, value);
    }
    
    return attrs;
}

BaseEntity::Attributes BaseEntity::extractFieldsWithPrefix(std::string_view prefix) const {
    ensureCache();
    
    Attributes attrs;
    for (const auto& [name, value] : *field_cache_) {
        if (name.starts_with(prefix)) {
            std::visit([&attrs, &name](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    attrs[name] = arg;
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    attrs[name] = std::to_string(arg);
                } else if constexpr (std::is_same_v<T, double>) {
                    attrs[name] = std::to_string(arg);
                }
            }, value);
        }
    }
    
    return attrs;
}

} // namespace themis
