# mock_key_provider.cpp

Path: `src/security/mock_key_provider.cpp`

Purpose: Test key provider used in unit tests to simulate key retrieval and signing operations.

Public functions / symbols:
- `for (const auto& [version, entry] : keys_[key_id]) {`
- `if (entry.metadata.status == KeyStatus::ACTIVE && version > latest_version) {`
- ``
- `if (version > max_version) {`
- `for (auto& [version, entry] : keys_[key_id]) {`
- `if (entry.metadata.status == KeyStatus::ACTIVE) {`
- `for (const auto& [version, entry] : versions) {`
- `for (const auto& [v, entry] : keys_[key_id]) {`
- `if (entry.metadata.status == KeyStatus::ACTIVE && v > latest_version) {`
- `if (entry.metadata.created_at_ms == 0) {`
- `for (auto& byte : key) {`
- `std::lock_guard<std::mutex> lock(mutex_);`
- `throw KeyNotFoundException(key_id, 0);`
- `throw KeyOperationException("No ACTIVE key found for: " + key_id);`
- `throw KeyNotFoundException(key_id, version);`
- `std::vector<uint8_t> key(32);  // 256 bits`
- `MockKeyProvider::MockKeyProvider() {`

