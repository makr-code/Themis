#include "utils/zstd_codec.h"

#include <algorithm>
#include <stdexcept>

#ifdef THEMIS_HAS_ZSTD
#include <zstd.h>
#endif

namespace themis {
namespace utils {

std::vector<uint8_t> zstd_compress(const uint8_t* data, size_t size, int level) {
#ifdef THEMIS_HAS_ZSTD
    if (!data || size == 0) return {};
    size_t bound = ZSTD_compressBound(size);
    std::vector<uint8_t> out(bound);
    size_t written = ZSTD_compress(out.data(), out.size(), data, size, level);
    if (ZSTD_isError(written)) {
        return {};
    }
    out.resize(written);
    return out;
#else
    (void)data; (void)size; (void)level;
    return {};
#endif
}

std::vector<uint8_t> zstd_decompress(const std::vector<uint8_t>& compressed) {
#ifdef THEMIS_HAS_ZSTD
    if (compressed.empty()) return {};
    unsigned long long rsize = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (rsize == ZSTD_CONTENTSIZE_ERROR || rsize == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Unknown size: fall back to a growing buffer approach
        size_t capacity = compressed.size() * 8; // heuristic
        std::vector<uint8_t> out(capacity);
        ZSTD_DCtx* dctx = ZSTD_createDCtx();
        if (!dctx) return {};
        size_t res = ZSTD_decompressDCtx(dctx, out.data(), out.size(), compressed.data(), compressed.size());
        ZSTD_freeDCtx(dctx);
        if (ZSTD_isError(res)) return {};
        out.resize(res);
        return out;
    } else {
        std::vector<uint8_t> out(static_cast<size_t>(rsize));
        size_t res = ZSTD_decompress(out.data(), out.size(), compressed.data(), compressed.size());
        if (ZSTD_isError(res)) return {};
        out.resize(res);
        return out;
    }
#else
    (void)compressed;
    return {};
#endif
}

} // namespace utils
} // namespace themis
