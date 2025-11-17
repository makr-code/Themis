# vault_key_provider.cpp

Path: `src/security/vault_key_provider.cpp`

Purpose: Integration with HashiCorp Vault or similar to fetch and use keys for signing/encryption.

Public functions / symbols:
- `static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {`
- `static std::vector<uint8_t> base64_decode(const std::string& encoded) {`
- `for (unsigned char c : encoded) {`
- `if (valb >= 0) {`
- `static std::string base64_encode(const std::vector<uint8_t>& data) {`
- `for (uint8_t c : data) {`
- `while (valb >= 0) {`
- `if (!curl) {`
- `if (curl) {`
- ``
- `if (method == "GET") {`
- `if (it->second.expiry_ms < now) {`
- `if (it->second.last_access_ms < oldest->second.last_access_ms) {`
- `if (impl_->config.kv_version == "v2") {`
- `if (version > 0) {`
- `if (impl_->config.kv_version != "v2") {`
- `for (const auto& key : j["data"]["keys"]) {`
- `if (meta.status == KeyStatus::ACTIVE) {`
- `std::vector<int> T(256, -1);`
- `curl_global_init(CURL_GLOBAL_DEFAULT);`
- `throw KeyOperationException("Failed to initialize libcurl");`
- `curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);`
- `curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.request_timeout_ms);`
- `curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config.verify_ssl ? 1L : 0L);`
- `curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config.verify_ssl ? 2L : 0L);`
- `curl_easy_cleanup(curl);`
- `curl_global_cleanup();`
- `std::lock_guard<std::mutex> lock(mutex);`
- `curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);`
- `curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);`
- `curl_easy_setopt(curl, CURLOPT_POST, 1L);`
- `curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "LIST");`
- `curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);`
- `curl_slist_free_all(headers);`
- `curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);`
- `throw KeyNotFoundException("key", 0);  // Will be refined by caller`
- `for (auto it = cache.begin(); it != cache.end(); ++it) {`
- `return httpGet(path);`
- `throw KeyNotFoundException(key_id, version);`
- `throw KeyOperationException("Metadata only available in KV v2");`
- `throw KeyNotFoundException(key_id, 0);`
- `throw KeyOperationException("Invalid Vault metadata response");`
- `return getKey(key_id, 0);  // 0 = latest version`
- `std::lock_guard<std::mutex> lock(impl_->mutex);`
- `std::vector<uint8_t> new_key(32);  // 256 bits`
- `writeSecret(key_id, key_b64, new_version);`
- `for (auto it = impl_->cache.begin(); it != impl_->cache.end();) {`
- `throw KeyOperationException("Key deletion only supported in KV v2");`
- `throw KeyOperationException("Cannot delete ACTIVE key. Deprecate it first via rotation.");`
- `curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "DELETE");`

