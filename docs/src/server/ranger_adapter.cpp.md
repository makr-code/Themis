# ranger_adapter.cpp

Path: `src/server/ranger_adapter.cpp`

Purpose: Adapter for Ranger policy enforcement or external authorization service integration.

Public functions / symbols:
- `size_t writeToString(void* contents, size_t size, size_t nmemb, void* userp) {`
- ``
- `if (!curl) { last_err = "curl init failed"; break; }`
- `if (success) {`
- `if (rc != CURLE_OK) {`
- `if (!should_retry || attempts >= max_attempts) {`
- `if (backoff > 0) {`
- `static std::string lower(std::string s) {`
- `for (const auto& it : items) {`
- `for (const auto& a : it["accesses"]) {`
- `for (const auto& pol : rangerJson) {`
- `for (const auto& p : policies) {`
- `curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);`
- `curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);`
- `curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);`
- `curl_easy_setopt(curl, CURLOPT_USERAGENT, "themisdb/1.0");`
- `curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cfg_.tls_verify ? 1L : 0L);`
- `curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, cfg_.tls_verify ? 2L : 0L);`
- `curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);`
- `curl_easy_cleanup(curl);`
- `RangerClient::convertFromRanger(const json& rangerJson) {`

