# zstd_codec.cpp

Path: `src/utils/zstd_codec.cpp`

Purpose: ZSTD compression/decompression helper wrappers guarded by `THEMIS_HAS_ZSTD`.

Public functions / symbols:
- ``
- `if (rsize == ZSTD_CONTENTSIZE_ERROR || rsize == ZSTD_CONTENTSIZE_UNKNOWN) {`
- `std::vector<uint8_t> out(bound);`
- `std::vector<uint8_t> out(capacity);`
- `ZSTD_freeDCtx(dctx);`

Notes / TODOs:
- Document config flags and behavior for skipped MIME types and compression levels.
