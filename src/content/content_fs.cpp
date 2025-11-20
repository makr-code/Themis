#include "content/content_fs.h"
#include "storage/key_schema.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

#include <nlohmann/json.hpp>

#include <openssl/sha.h>

namespace themis {

static std::string toHex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string ContentFS::sha256Hex(const std::vector<uint8_t>& data) {
    uint8_t md[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    if (!data.empty()) SHA256_Update(&ctx, data.data(), data.size());
    SHA256_Final(md, &ctx);
    return toHex(md, sizeof(md));
}

ContentFS::Status ContentFS::put(const std::string& pk,
                                 const std::vector<uint8_t>& data,
                                 const std::string& mime,
                                 const std::optional<std::string>& sha256_expected_hex) {
    if (pk.empty()) return Status::Error("put: pk must not be empty");
    // Compute checksum
    std::string hex = sha256Hex(data);
    if (sha256_expected_hex && !sha256_expected_hex->empty() && *sha256_expected_hex != hex) {
        return Status::Error("put: checksum mismatch");
    }

    // Decide storage layout: chunked for large payloads
    const uint64_t total_size = static_cast<uint64_t>(data.size());
    const bool use_chunked = total_size > chunk_size_bytes_;

    // Write payload
    uint64_t chunk_sz = 0;
    uint64_t chunks = 0;
    if (use_chunked) {
        chunk_sz = chunk_size_bytes_;
        chunks = (total_size + chunk_sz - 1) / chunk_sz;
        // Write chunks sequentially
        for (uint64_t i = 0; i < chunks; ++i) {
            uint64_t off = i * chunk_sz;
            uint64_t end = std::min<uint64_t>(total_size, off + chunk_sz);
            std::vector<uint8_t> part;
            part.insert(part.end(), data.begin() + static_cast<ptrdiff_t>(off), data.begin() + static_cast<ptrdiff_t>(end));
            if (!db_.put(chunkKey(pk, i), part)) {
                return Status::Error("put: failed to write chunk");
            }
        }
        // Ensure legacy blob key is removed to avoid confusion
        db_.del(blobKey(pk));
    } else {
        // Single blob write
        if (!db_.put(blobKey(pk), data)) {
            return Status::Error("put: failed to write blob");
        }
        // Optionally remove old chunks if any existed (best-effort): read old meta to know chunk count
        auto oldMeta = db_.get(metaKey(pk));
        if (oldMeta) {
            try {
                auto jm = nlohmann::json::from_cbor(*oldMeta);
                uint64_t old_chunks = jm.value("chunks", static_cast<uint64_t>(0));
                for (uint64_t i = 0; i < old_chunks; ++i) db_.del(chunkKey(pk, i));
            } catch (...) {}
        }
    }

    // Serialize metadata as CBOR JSON
    nlohmann::json j;
    j["pk"] = pk;
    j["mime"] = mime;
    j["size"] = total_size;
    j["sha256_hex"] = hex;
    j["chunk_size"] = use_chunked ? chunk_sz : 0;
    j["chunks"] = use_chunked ? chunks : 0;
    auto metaBytes = nlohmann::json::to_cbor(j);

    if (!db_.put(metaKey(pk), metaBytes)) {
        return Status::Error("put: failed to write meta");
    }
    return Status::OK();
}

std::pair<ContentFS::Status, std::vector<uint8_t>> ContentFS::get(const std::string& pk) const {
    // Read meta first to decide storage format
    auto meta = db_.get(metaKey(pk));
    if (!meta) return {Status::Error("get: not found"), {}};
    try {
        auto j = nlohmann::json::from_cbor(*meta);
        uint64_t chunks = j.value("chunks", static_cast<uint64_t>(0));
        if (chunks == 0) {
            auto blob = db_.get(blobKey(pk));
            if (!blob) return {Status::Error("get: not found"), {}};
            return {Status::OK(), *blob};
        } else {
            uint64_t total = j.value("size", static_cast<uint64_t>(0));
            uint64_t chunk_sz = j.value("chunk_size", chunk_size_bytes_);
            std::vector<uint8_t> out;
            out.reserve(static_cast<size_t>(total));
            for (uint64_t i = 0; i < chunks; ++i) {
                auto part = db_.get(chunkKey(pk, i));
                if (!part) return {Status::Error("get: missing chunk"), {}};
                out.insert(out.end(), part->begin(), part->end());
            }
            return {Status::OK(), std::move(out)};
        }
    } catch (...) {
        return {Status::Error("get: invalid meta"), {}};
    }
}

std::pair<ContentFS::Status, std::vector<uint8_t>> ContentFS::getRange(const std::string& pk, uint64_t offset, uint64_t length) const {
    auto meta = db_.get(metaKey(pk));
    if (!meta) return {Status::Error("getRange: not found"), {}};
    try {
        auto j = nlohmann::json::from_cbor(*meta);
        uint64_t total = j.value("size", static_cast<uint64_t>(0));
        uint64_t chunks = j.value("chunks", static_cast<uint64_t>(0));
        if (offset > total) return {Status::Error("getRange: offset beyond end"), {}};
        uint64_t end = (length == 0) ? total : std::min(total, offset + length);
        if (end < offset) end = offset;
        std::vector<uint8_t> out;
        out.reserve(static_cast<size_t>(end - offset));
        if (chunks == 0) {
            // Unchunked: slice from full blob (fallback)
            auto blob = db_.get(blobKey(pk));
            if (!blob) return {Status::Error("getRange: not found"), {}};
            out.insert(out.end(), blob->begin() + static_cast<ptrdiff_t>(offset), blob->begin() + static_cast<ptrdiff_t>(end));
            return {Status::OK(), std::move(out)};
        } else {
            uint64_t chunk_sz = j.value("chunk_size", chunk_size_bytes_);
            uint64_t start_idx = offset / chunk_sz;
            uint64_t end_idx = (end == 0) ? 0 : ((end - 1) / chunk_sz);
            if (start_idx >= chunks) return {Status::Error("getRange: offset beyond end"), {}};
            if (end_idx >= chunks) end_idx = chunks - 1;
            for (uint64_t i = start_idx; i <= end_idx; ++i) {
                auto part = db_.get(chunkKey(pk, i));
                if (!part) return {Status::Error("getRange: missing chunk"), {}};
                uint64_t chunk_off = i * chunk_sz;
                uint64_t part_start = (i == start_idx) ? (offset - chunk_off) : 0;
                uint64_t part_end = (i == end_idx) ? (end - chunk_off) : static_cast<uint64_t>(part->size());
                if (part_end > part->size()) part_end = static_cast<uint64_t>(part->size());
                if (part_start > part_end) part_start = part_end;
                out.insert(out.end(), part->begin() + static_cast<ptrdiff_t>(part_start), part->begin() + static_cast<ptrdiff_t>(part_end));
            }
            return {Status::OK(), std::move(out)};
        }
    } catch (...) {
        return {Status::Error("getRange: invalid meta"), {}};
    }
}

std::pair<ContentFS::Status, ContentMeta> ContentFS::head(const std::string& pk) const {
    auto meta = db_.get(metaKey(pk));
    if (!meta) return {Status::Error("head: not found"), {}};
    try {
        auto j = nlohmann::json::from_cbor(*meta);
        ContentMeta m;
        m.pk = j.value("pk", pk);
        m.mime = j.value("mime", std::string{});
        m.size = j.value("size", static_cast<uint64_t>(0));
        m.sha256_hex = j.value("sha256_hex", std::string{});
        m.chunk_size = j.value("chunk_size", static_cast<uint64_t>(0));
        m.chunks = j.value("chunks", static_cast<uint64_t>(0));
        return {Status::OK(), m};
    } catch (...) {
        return {Status::Error("head: invalid meta encoding"), {}};
    }
}

ContentFS::Status ContentFS::remove(const std::string& pk) {
    // Read meta to know if chunked
    uint64_t chunks = 0;
    if (auto meta = db_.get(metaKey(pk))) {
        try {
            auto j = nlohmann::json::from_cbor(*meta);
            chunks = j.value("chunks", static_cast<uint64_t>(0));
        } catch (...) {}
    }
    bool ok1 = db_.del(metaKey(pk));
    bool ok2 = db_.del(blobKey(pk));
    bool ok3 = false;
    if (chunks > 0) {
        ok3 = true;
        for (uint64_t i = 0; i < chunks; ++i) {
            bool r = db_.del(chunkKey(pk, i));
            ok3 = ok3 && r;
        }
    }
    if (!ok1 && !ok2 && chunks == 0) return Status::Error("remove: not found");
    return Status::OK();
}

} // namespace themis
