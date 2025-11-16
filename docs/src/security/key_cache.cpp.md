# key_cache.cpp

Path: `src/security/key_cache.cpp`

Purpose: Caching of derived keys (HKDF) and key metadata for FieldEncryption.

Public functions / symbols:
- `if (now > it->second.expires_at_ms) {`
- ``
- `if (it->second.last_access_ms < oldest_access) {`
- `std::lock_guard<std::mutex> lock(mutex_);`
- `evictLRU();`

