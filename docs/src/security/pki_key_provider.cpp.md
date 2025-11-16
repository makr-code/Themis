# pki_key_provider.cpp

Path: `src/security/pki_key_provider.cpp`

Purpose: PKI Key provider interface and OpenSSL/Vault backed implementations for signing/verification (eIDAS scope noted).

Public functions / symbols:
- ``
- `if (key_id == "dek") {`
- `for (const auto& [key_id, _] : field_key_cache_) {`
- `loadOrCreateDEK(current_dek_version_);`
- `EVP_CIPHER_CTX_free(ctx);`
- `std::vector<uint8_t> dek(32); // 256-bit`
- `std::vector<uint8_t> iv(12);`
- `return getKey(key_id, 0);`
- `std::scoped_lock lk(mu_);`
- `return loadOrCreateDEK(v);`
- `return deriveFieldKey(key_id);`
- `return rotateDEK();`

Notes / TODOs:
- Document legal/compliance requirements for eIDAS if enabling production signing.
