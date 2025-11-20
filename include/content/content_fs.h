#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "storage/rocksdb_wrapper.h"

namespace themis {

struct ContentMeta {
    std::string pk;
    std::string mime;
    uint64_t size = 0;
    std::string sha256_hex; // optional: empty if not provided
    uint64_t chunk_size = 0; // 0 => ungechunked (vollständiger Blob)
    uint64_t chunks = 0;     // Anzahl der Chunks (0 => ungechunked)
};

class ContentFS {
public:
    explicit ContentFS(RocksDBWrapper& db) : db_(db) {}
    void setChunkSizeBytes(uint64_t sz) { chunk_size_bytes_ = sz == 0 ? kDefaultChunkSize : sz; }
    uint64_t getChunkSizeBytes() const { return chunk_size_bytes_; }

    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {true, {}}; }
        static Status Error(std::string msg) { return {false, std::move(msg)}; }
        operator bool() const { return ok; }
    };

    // Store entire blob in one value under content:<pk>:blob, metadata in content:<pk>:meta
    Status put(const std::string& pk,
               const std::vector<uint8_t>& data,
               const std::string& mime,
               const std::optional<std::string>& sha256_expected_hex = std::nullopt);

    // Get full blob
    std::pair<Status, std::vector<uint8_t>> get(const std::string& pk) const;

    // Range read [offset, offset+length) (length==0 => to end)
    std::pair<Status, std::vector<uint8_t>> getRange(const std::string& pk, uint64_t offset, uint64_t length) const;

    // Head (metadata only)
    std::pair<Status, ContentMeta> head(const std::string& pk) const;

    // Delete blob + meta
    Status remove(const std::string& pk);

    // Utility: compute SHA-256 hex for buffer
    static std::string sha256Hex(const std::vector<uint8_t>& data);

private:
    RocksDBWrapper& db_;
    uint64_t chunk_size_bytes_ = kDefaultChunkSize;
    static std::string metaKey(const std::string& pk) { return std::string("content:") + pk + ":meta"; }
    static std::string blobKey(const std::string& pk) { return std::string("content:") + pk + ":blob"; }
    static std::string chunkKey(const std::string& pk, uint64_t idx) {
        return std::string("content:") + pk + ":chunk:" + std::to_string(idx);
    }
public:
    static constexpr uint64_t kDefaultChunkSize = 1024ull * 1024ull; // 1 MiB
};

} // namespace themis
