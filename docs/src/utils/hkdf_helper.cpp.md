# hkdf_helper.cpp

Path: `src/utils/hkdf_helper.cpp`

Purpose: HKDF derivation helpers and cache integration for key derivation.

Public functions / symbols:
- `if (!kdf) {`
- ``
- `if (!pctx) {`
- `EVP_KDF_free(kdf);`
- `EVP_KDF_CTX_free(kctx);`
- `EVP_PKEY_CTX_free(pctx);`

