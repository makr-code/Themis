#ifndef THEMIS_GORILLA_H
#define THEMIS_GORILLA_H

#include <cstdint>
#include <vector>
#include <utility>
#include <optional>

namespace themis {

// Minimal Gorilla-style time-series compression for (timestamp_ms, double)
// Implements:
//  - Timestamps: delta-of-delta encoded with ZigZag + varint
//  - Values: XOR of IEEE-754 double bit patterns with leading/trailing zero optimization
// Reference: Gorilla: A Fast, Scalable, In-Memory Time Series Database

class BitWriter {
public:
    void writeBit(bool bit);
    void writeBits(uint64_t value, int bits);
    void writeVarUInt(uint64_t value);
    void writeZigZag64(int64_t value);
    void alignToByte();
    std::vector<uint8_t> finish();

private:
    std::vector<uint8_t> buf_;
    uint8_t cur_ {0};
    int bitpos_ {0}; // 0..7
};

class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t>& data);
    bool readBit();
    uint64_t readBits(int bits);
    uint64_t readVarUInt();
    int64_t readZigZag64();
    bool eof() const;
    void alignToByte();

private:
    const std::vector<uint8_t>& buf_;
    size_t idx_ {0};
    int bitpos_ {8};
    uint8_t cur_ {0};
};

class GorillaEncoder {
public:
    void add(int64_t timestamp_ms, double value);
    std::vector<uint8_t> finish();

private:
    bool first_ {true};
    int64_t prev_ts_ {0};
    int64_t prev_dt_ {0};
    uint64_t prev_vbits_ {0};
    int prev_leading_ {64};
    int prev_trailing_ {64};
    BitWriter bw_;
};

class GorillaDecoder {
public:
    explicit GorillaDecoder(const std::vector<uint8_t>& data);
    std::optional<std::pair<int64_t,double>> next();

private:
    bool first_ {true};
    int64_t prev_ts_ {0};
    int64_t prev_dt_ {0};
    uint64_t prev_vbits_ {0};
    int prev_leading_ {64};
    int prev_trailing_ {64};
    BitReader br_;
};

} // namespace themis

#endif // THEMIS_GORILLA_H
