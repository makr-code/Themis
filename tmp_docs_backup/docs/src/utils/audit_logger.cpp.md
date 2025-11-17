# audit_logger.cpp

Path: `src/utils/audit_logger.cpp`

Purpose: Audit logging helper for security events.

Public functions / symbols:
- `static std::string base64_encode_local(const std::vector<uint8_t>& data) {`
- ``
- `std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);`
- `std::scoped_lock lk(file_mu_);`
- `std::ofstream ofs(cfg_.log_path, std::ios::app | std::ios::binary);`

