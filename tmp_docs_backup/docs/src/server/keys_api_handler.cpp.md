# keys_api_handler.cpp

Path: `src/server/keys_api_handler.cpp`

Purpose: Handlers for key management API (create/list/rotate keys).

Public functions / symbols:
- `: key_provider_(key_provider) {`
- `if (!key_provider_) {`
- ``
- `switch (key_meta.status) {`
- `THEMIS_WARN("Keys API: KeyProvider not initialized, returning empty list");`
- `THEMIS_ERROR("Keys API: KeyProvider not initialized");`
- `THEMIS_WARN("Keys API: Key not found: {}", key_id);`

