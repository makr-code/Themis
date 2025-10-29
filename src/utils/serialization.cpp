#include "utils/serialization.h"
#include <cstring>

namespace themis {
namespace utils {

// Encoder implementation

Serialization::Encoder::Encoder() {
    buffer_.reserve(1024); // Pre-allocate
}

void Serialization::Encoder::writeTag(TypeTag tag) {
    buffer_.push_back(static_cast<uint8_t>(tag));
}

void Serialization::Encoder::writeUInt32(uint32_t value) {
    buffer_.push_back((value >> 0) & 0xFF);
    buffer_.push_back((value >> 8) & 0xFF);
    buffer_.push_back((value >> 16) & 0xFF);
    buffer_.push_back((value >> 24) & 0xFF);
}

void Serialization::Encoder::writeUInt64(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        buffer_.push_back((value >> (i * 8)) & 0xFF);
    }
}

void Serialization::Encoder::encodeNull() {
    writeTag(TypeTag::NULL_VALUE);
}

void Serialization::Encoder::encodeBool(bool value) {
    writeTag(value ? TypeTag::BOOL_TRUE : TypeTag::BOOL_FALSE);
}

void Serialization::Encoder::encodeInt32(int32_t value) {
    writeTag(TypeTag::INT32);
    writeUInt32(static_cast<uint32_t>(value));
}

void Serialization::Encoder::encodeInt64(int64_t value) {
    writeTag(TypeTag::INT64);
    writeUInt64(static_cast<uint64_t>(value));
}

void Serialization::Encoder::encodeUInt32(uint32_t value) {
    writeTag(TypeTag::UINT32);
    writeUInt32(value);
}

void Serialization::Encoder::encodeUInt64(uint64_t value) {
    writeTag(TypeTag::UINT64);
    writeUInt64(value);
}

void Serialization::Encoder::encodeFloat(float value) {
    writeTag(TypeTag::FLOAT);
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(float));
    writeUInt32(bits);
}

void Serialization::Encoder::encodeDouble(double value) {
    writeTag(TypeTag::DOUBLE);
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    writeUInt64(bits);
}

void Serialization::Encoder::encodeString(std::string_view str) {
    writeTag(TypeTag::STRING);
    writeUInt32(static_cast<uint32_t>(str.size()));
    buffer_.insert(buffer_.end(), str.begin(), str.end());
}

void Serialization::Encoder::encodeBinary(const std::vector<uint8_t>& data) {
    writeTag(TypeTag::BINARY);
    writeUInt32(static_cast<uint32_t>(data.size()));
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

void Serialization::Encoder::encodeFloatVector(const std::vector<float>& vec) {
    writeTag(TypeTag::VECTOR_FLOAT);
    writeUInt32(static_cast<uint32_t>(vec.size()));
    
    // Write floats as raw bytes (platform-dependent but fast)
    const uint8_t* data = reinterpret_cast<const uint8_t*>(vec.data());
    buffer_.insert(buffer_.end(), data, data + vec.size() * sizeof(float));
}

void Serialization::Encoder::beginArray(size_t size) {
    writeTag(TypeTag::ARRAY);
    writeUInt32(static_cast<uint32_t>(size));
}

void Serialization::Encoder::endArray() {
    // No-op for now
}

void Serialization::Encoder::beginObject(size_t num_fields) {
    writeTag(TypeTag::OBJECT);
    writeUInt32(static_cast<uint32_t>(num_fields));
}

void Serialization::Encoder::endObject() {
    // No-op for now
}

std::vector<uint8_t> Serialization::Encoder::finish() {
    return std::move(buffer_);
}

// Decoder implementation

Serialization::Decoder::Decoder(const std::vector<uint8_t>& data) : data_(data) {}

Serialization::TypeTag Serialization::Decoder::peekType() const {
    if (pos_ >= data_.size()) {
        return TypeTag::NULL_VALUE;
    }
    return static_cast<TypeTag>(data_[pos_]);
}

Serialization::TypeTag Serialization::Decoder::readTag() {
    if (pos_ >= data_.size()) {
        return TypeTag::NULL_VALUE;
    }
    return static_cast<TypeTag>(data_[pos_++]);
}

uint32_t Serialization::Decoder::readUInt32() {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(data_[pos_++]) << (i * 8);
    }
    return value;
}

uint64_t Serialization::Decoder::readUInt64() {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data_[pos_++]) << (i * 8);
    }
    return value;
}

bool Serialization::Decoder::isNull() const {
    return peekType() == TypeTag::NULL_VALUE;
}

bool Serialization::Decoder::decodeBool() {
    TypeTag tag = readTag();
    return tag == TypeTag::BOOL_TRUE;
}

int32_t Serialization::Decoder::decodeInt32() {
    readTag(); // Skip type tag
    return static_cast<int32_t>(readUInt32());
}

int64_t Serialization::Decoder::decodeInt64() {
    readTag();
    return static_cast<int64_t>(readUInt64());
}

uint32_t Serialization::Decoder::decodeUInt32() {
    readTag();
    return readUInt32();
}

uint64_t Serialization::Decoder::decodeUInt64() {
    readTag();
    return readUInt64();
}

float Serialization::Decoder::decodeFloat() {
    readTag();
    uint32_t bits = readUInt32();
    float value;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

double Serialization::Decoder::decodeDouble() {
    readTag();
    uint64_t bits = readUInt64();
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

std::string Serialization::Decoder::decodeString() {
    readTag();
    uint32_t size = readUInt32();
    std::string str(reinterpret_cast<const char*>(&data_[pos_]), size);
    pos_ += size;
    return str;
}

std::vector<uint8_t> Serialization::Decoder::decodeBinary() {
    readTag();
    uint32_t size = readUInt32();
    std::vector<uint8_t> binary(data_.begin() + pos_, data_.begin() + pos_ + size);
    pos_ += size;
    return binary;
}

std::vector<float> Serialization::Decoder::decodeFloatVector() {
    readTag();
    uint32_t count = readUInt32();
    
    std::vector<float> vec(count);
    const uint8_t* data = &data_[pos_];
    std::memcpy(vec.data(), data, count * sizeof(float));
    pos_ += count * sizeof(float);
    
    return vec;
}

size_t Serialization::Decoder::beginArray() {
    readTag();
    return readUInt32();
}

void Serialization::Decoder::endArray() {
    // No-op
}

size_t Serialization::Decoder::beginObject() {
    readTag();
    return readUInt32();
}

void Serialization::Decoder::endObject() {
    // No-op
}

bool Serialization::Decoder::hasMore() const {
    return pos_ < data_.size();
}

} // namespace utils
} // namespace themis
