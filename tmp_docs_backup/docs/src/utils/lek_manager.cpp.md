# lek_manager.cpp

Path: `src/utils/lek_manager.cpp`

Purpose: LEK (local encryption key) manager helpers and rotation policies.

Public functions / symbols:
- ``
- `localtime_s(&tm, &time_t);`
- `localtime_r(&time_t, &tm);`
- `std::vector<uint8_t> lek(32); // 256-bit AES key`
- `FieldEncryption enc(key_provider_);`
- `std::scoped_lock lk(mu_);`
- `ensureLEKExists(date_str);`

