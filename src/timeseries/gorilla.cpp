#include "timeseries/gorilla.h"
#include <cstring>
#include <limits>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace themis {

static inline uint64_t dbl_to_bits(double v) {
    uint64_t b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

static inline double bits_to_dbl(uint64_t b) {
    double v;
    std::memcpy(&v, &b, sizeof(v));
    return v;
}

static inline int clz64(uint64_t x) {
    if (x == 0) return 64;
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return 63 - static_cast<int>(idx);
#else
    return __builtin_clzll(x);
#endif
}

static inline int ctz64(uint64_t x) {
    if (x == 0) return 64;
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return static_cast<int>(idx);
#else
    return __builtin_ctzll(x);
#endif
}

// ------- BitWriter -------
void BitWriter::writeBit(bool bit) {
    cur_ |= (static_cast<uint8_t>(bit) & 1) << bitpos_;
    bitpos_++;
    if (bitpos_ == 8) {
        buf_.push_back(cur_);
        cur_ = 0;
        bitpos_ = 0;
    }
}

void BitWriter::writeBits(uint64_t value, int bits) {
    for (int i = 0; i < bits; ++i) {
        writeBit((value >> i) & 1ULL);
    }
}

void BitWriter::writeVarUInt(uint64_t v) {
    // LEB128 unsigned
    while (v >= 0x80) {
        buf_.push_back(static_cast<uint8_t>(v & 0x7FUL) | 0x80U);
        v >>= 7;
    }
    buf_.push_back(static_cast<uint8_t>(v & 0x7FUL));
}

void BitWriter::writeZigZag64(int64_t value) {
    uint64_t zz = (static_cast<uint64_t>(value) << 1) ^ static_cast<uint64_t>(value >> 63);
    writeVarUInt(zz);
}

void BitWriter::alignToByte() {
    if (bitpos_ != 0) {
        buf_.push_back(cur_);
        cur_ = 0;
        bitpos_ = 0;
    }
}

std::vector<uint8_t> BitWriter::finish() {
    if (bitpos_ != 0) {
        buf_.push_back(cur_);
        cur_ = 0;
        bitpos_ = 0;
    }
    return buf_;
}

// ------- BitReader -------
BitReader::BitReader(const std::vector<uint8_t>& data)
    : buf_(data) {
    if (!buf_.empty()) { cur_ = buf_[0]; idx_ = 0; bitpos_ = 0; }
}

bool BitReader::readBit() {
    if (idx_ >= buf_.size()) return false;
    bool bit = ((cur_ >> bitpos_) & 1U) != 0;
    bitpos_++;
    if (bitpos_ == 8) {
        idx_++;
        if (idx_ < buf_.size()) cur_ = buf_[idx_];
        bitpos_ = 0;
    }
    return bit;
}

uint64_t BitReader::readBits(int bits) {
    uint64_t v = 0;
    for (int i = 0; i < bits; ++i) {
        if (readBit()) v |= (1ULL << i);
    }
    return v;
}

uint64_t BitReader::readVarUInt() {
    // LEB128 unsigned; caller ensures byte alignment when required
    uint64_t result = 0;
    int shift = 0;
    while (true) {
        if (idx_ >= buf_.size()) return result;
        uint64_t byte = readBits(8);
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

int64_t BitReader::readZigZag64() {
    uint64_t zz = readVarUInt();
    int64_t v = static_cast<int64_t>(zz >> 1);
    if (zz & 1ULL) v = ~v;
    return v;
}

bool BitReader::eof() const { return idx_ >= buf_.size(); }

void BitReader::alignToByte() {
    while (bitpos_ != 0) { (void)readBit(); }
}

// ------- GorillaEncoder -------
void GorillaEncoder::add(int64_t timestamp_ms, double value) {
    if (first_) {
        // Write first timestamp and value in full
        bw_.writeZigZag64(timestamp_ms); // allow negative for completeness
        bw_.writeBits(dbl_to_bits(value), 64);
        prev_ts_ = timestamp_ms;
        prev_dt_ = 0;
        prev_vbits_ = dbl_to_bits(value);
        prev_leading_ = 64;
        prev_trailing_ = 64;
        first_ = false;
        return;
    }

    // Ensure we start new point with varint aligned to byte boundary
    bw_.alignToByte();
    // Timestamp: delta-of-delta with ZigZag+varint (byte-aligned)
    int64_t dt = timestamp_ms - prev_ts_;
    int64_t dod = dt - prev_dt_;
    bw_.writeZigZag64(dod);
    prev_ts_ = timestamp_ms;
    prev_dt_ = dt;

    // Value: XOR encoding
    uint64_t vbits = dbl_to_bits(value);
    uint64_t xorv = vbits ^ prev_vbits_;
    
    if (xorv == 0) {
        // Write control bit 0
        bw_.writeBit(false);
        // Keep previous value the same explicitly
        prev_vbits_ = vbits;
    } else {
        bw_.writeBit(true); // different
        // New header each time in this simplified variant: use actual leading/trailing zeros
        int leading = clz64(xorv);
        int trailing = ctz64(xorv);
        // Handle edge case: if all 64 bits are significant, we need special encoding
        // Since we can only encode 0-63 in 6 bits, we use sig=0 to mean 64
        int significant = 64 - leading - trailing;
        if (significant > 64) significant = 64;  // safety clamp
        
        // Write leading (6 bits), significant length (6 bits: 0 means 64, 1-63 literal), then value bits
        bw_.writeBits(static_cast<uint64_t>(leading), 6);
        bw_.writeBits(static_cast<uint64_t>(significant & 63), 6);  // 64 becomes 0
        bw_.writeBits((xorv >> trailing), significant);
        prev_leading_ = leading;
        prev_trailing_ = trailing;
        prev_vbits_ = vbits;
    }
    // After finishing value bits for this point, keep bitstream as-is; next call will align before varint
}

std::vector<uint8_t> GorillaEncoder::finish() {
    return bw_.finish();
}

// ------- GorillaDecoder -------
GorillaDecoder::GorillaDecoder(const std::vector<uint8_t>& data)
    : br_(data) {}

std::optional<std::pair<int64_t,double>> GorillaDecoder::next() {
    if (first_) {
        if (br_.eof()) return std::nullopt;
        // First timestamp varint is at byte boundary
        br_.alignToByte();
        if (br_.eof()) return std::nullopt;
        int64_t ts = br_.readZigZag64();
        uint64_t vbits = br_.readBits(64);
        prev_ts_ = ts;
        prev_dt_ = 0;
        prev_vbits_ = vbits;
        prev_leading_ = 64;
        prev_trailing_ = 64;
        first_ = false;
        return std::make_pair(ts, bits_to_dbl(vbits));
    }

    // Subsequent varints are byte-aligned; align and check EOF before reading
    br_.alignToByte();
    if (br_.eof()) return std::nullopt;
    
    int64_t dod = br_.readZigZag64();
    int64_t dt = prev_dt_ + dod;
    int64_t ts = prev_ts_ + dt;
    prev_dt_ = dt;
    prev_ts_ = ts;

    if (br_.eof()) return std::nullopt;
    bool different = br_.readBit();
    
    uint64_t vbits;
    if (!different) {
        vbits = prev_vbits_;
    } else {
        int leading = static_cast<int>(br_.readBits(6));
        int significant = static_cast<int>(br_.readBits(6));
        if (significant == 0) significant = 64;  // 0 encodes 64
        uint64_t payload = br_.readBits(significant);
        int trailing = 64 - leading - significant;
        uint64_t xorv = (payload << trailing);
        vbits = prev_vbits_ ^ xorv;
        prev_leading_ = leading;
        prev_trailing_ = trailing;
    }
    
    prev_vbits_ = vbits;
    return std::make_optional(std::make_pair(ts, bits_to_dbl(vbits)));
}

} // namespace themis
