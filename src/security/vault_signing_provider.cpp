#include "security/vault_signing_provider.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace themis {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Simple base64 helpers (minimal, robust for our small use)
static const std::string b64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string ret;
    int val = 0, valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            ret.push_back(b64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) ret.push_back(b64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (ret.size() % 4) ret.push_back('=');
    return ret;
}

static std::vector<uint8_t> base64_decode(const std::string& encoded) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)b64_chars[i]] = i;

    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((uint8_t)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

VaultSigningProvider::VaultSigningProvider(const Config& cfg) {
    // store config by copying into a simple global curl state via local static
    // For this prototype we store nothing special; HTTP calls will construct CURL per-call
    (void)cfg; // no-op stored in this minimal prototype
}

VaultSigningProvider::~VaultSigningProvider() = default;

SigningResult VaultSigningProvider::sign(const std::string& key_id, const std::vector<uint8_t>& data) {
    // Prototype behaviour:
    // - If environment doesn't provide a reachable Vault (empty vault_addr), fall back to a local mock
    // - Otherwise attempt an HTTP call to Vault Transit (basic best-effort)

    // In this prototype we detect Vault availability via environment variable THEMIS_VAULT_ADDR.
    const char* env_addr = std::getenv("THEMIS_VAULT_ADDR");
    const char* env_token = std::getenv("THEMIS_VAULT_TOKEN");
    const char* env_mount = std::getenv("THEMIS_VAULT_TRANSIT_MOUNT");

    if (!env_addr || std::string(env_addr).empty()) {
        // Mock: compute SHA256(data) and return as signature (deterministic, no key material leakage)
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        SigningResult res;
        res.signature.assign(hash, hash + SHA256_DIGEST_LENGTH);
        res.algorithm = "MOCK+SHA256";
        return res;
    }

    std::string vault_addr(env_addr);
    std::string vault_token = env_token ? env_token : std::string();
    std::string transit_mount = env_mount ? env_mount : std::string("transit");

    // Build URL: /v1/<transit_mount>/sign/<key_id>
    std::string url = vault_addr;
    if (url.back() == '/') url.pop_back();
    url += "/v1/" + transit_mount + "/sign/" + key_id;

    // Prepare JSON payload: { "input": base64(data) }
    json payload;
    payload["input"] = base64_encode(data);

    // Setup CURL
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to init CURL for VaultSigningProvider");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.dump().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!vault_token.empty()) headers = curl_slist_append(headers, (std::string("X-Vault-Token: ") + vault_token).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        // Fall back to mock signature to keep prototype usable even when Vault unreachable
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        SigningResult res;
        res.signature.assign(hash, hash + SHA256_DIGEST_LENGTH);
        res.algorithm = "MOCK+SHA256";
        return res;
    }

    // Parse response JSON to extract signature
    try {
        json j = json::parse(response);
        std::string sig_b64;
        if (j.contains("data") && j["data"].contains("signature")) {
            sig_b64 = j["data"]["signature"].get<std::string>();
        } else if (j.contains("data") && j["data"].contains("signatures") && j["data"]["signatures"].is_array()) {
            sig_b64 = j["data"]["signatures"][0].get<std::string>();
        } else if (j.contains("data") && j["data"].contains("signed")) {
            // transit/sign can return a 'signed' field for some backends
            sig_b64 = j["data"]["signed"].get<std::string>();
        }

        // Vault transit may return signatures prefixed like "vault:v1:BASE64" or raw base64.
        if (sig_b64.rfind("vault:", 0) == 0) {
            // strip prefix 'vault:v1:'
            size_t pos = sig_b64.find(':', 6); // after 'vault:'
            if (pos != std::string::npos && pos + 1 < sig_b64.size()) {
                std::string inner = sig_b64.substr(pos + 1);
                std::vector<uint8_t> sig = base64_decode(inner);
                SigningResult res;
                res.signature = std::move(sig);
                res.algorithm = "VAULT+TRANSIT";
                return res;
            }
        }

        // Otherwise assume sig_b64 itself is base64
        if (!sig_b64.empty()) {
            SigningResult res;
            res.signature = base64_decode(sig_b64);
            res.algorithm = "VAULT+TRANSIT";
            return res;
        }
    } catch (...) {
        // ignore parse errors and fall through to mock
    }

    // If we couldn't extract a signature, return a mock SHA256 as deterministic fallback
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data.data(), data.size(), hash);
    SigningResult res;
    res.signature.assign(hash, hash + SHA256_DIGEST_LENGTH);
    res.algorithm = "MOCK+SHA256";
    return res;
}

} // namespace themis
