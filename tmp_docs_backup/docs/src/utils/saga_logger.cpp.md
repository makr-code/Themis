# saga_logger.cpp

Path: `src/utils/saga_logger.cpp`

Purpose: Logging utilities specific to SAGA/transaction flows.

Public functions / symbols:
- `for (const auto& step : buffer_) {`
- ``
- `if (pki_) {`
- `if (computed_hash != batch_meta->ciphertext_hash) {`
- `for (const auto& entry : batch_array) {`
- `signAndFlushBatch();`
- `std::scoped_lock lk(mu_);`
- `std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);`
- `std::ofstream ofs(path, std::ios::app | std::ios::binary);`
- `appendJsonLine(cfg_.log_path, log_entry);`
- `appendJsonLine(cfg_.log_path, batch_array);`
- `std::ifstream sig_file(cfg_.signature_path);`
- `std::ifstream log_file(cfg_.log_path);`

