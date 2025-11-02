#include "server/ranger_adapter.h"
#include "server/policy_engine.h"
#include <curl/curl.h>
#include <sstream>
#include <algorithm>

using json = nlohmann::json;

namespace themis { namespace server {

namespace {
    size_t writeToString(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total = size * nmemb;
        std::string* s = static_cast<std::string*>(userp);
        s->append(static_cast<char*>(contents), total);
        return total;
    }
}

RangerClient::RangerClient(RangerClientConfig cfg) : cfg_(std::move(cfg)) {}

std::optional<json> RangerClient::fetchPolicies(std::string* err) const {
    CURL* curl = curl_easy_init();
    if (!curl) { if (err) *err = "curl init failed"; return std::nullopt; }

    std::string url = cfg_.base_url;
    if (!cfg_.policies_path.empty()) {
        if (!url.empty() && url.back() == '/' && cfg_.policies_path.front() == '/') {
            url.pop_back();
        }
        url += cfg_.policies_path;
    }
    // Typical Ranger API allows query by service name
    if (url.find("?") == std::string::npos) {
        url += std::string("?serviceName=") + cfg_.service_name;
    } else {
        url += std::string("&serviceName=") + cfg_.service_name;
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    if (!cfg_.bearer_token.empty()) {
        std::string auth = std::string("Authorization: Bearer ") + cfg_.bearer_token;
        headers = curl_slist_append(headers, auth.c_str());
    }
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "themisdb/1.0");

    // TLS options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cfg_.tls_verify ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, cfg_.tls_verify ? 2L : 0L);
    if (cfg_.ca_cert_path) curl_easy_setopt(curl, CURLOPT_CAINFO, cfg_.ca_cert_path->c_str());
    if (cfg_.client_cert_path) curl_easy_setopt(curl, CURLOPT_SSLCERT, cfg_.client_cert_path->c_str());
    if (cfg_.client_key_path) curl_easy_setopt(curl, CURLOPT_SSLKEY, cfg_.client_key_path->c_str());

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        if (err) *err = curl_easy_strerror(rc);
        return std::nullopt;
    }
    if (http_code < 200 || http_code >= 300) {
        if (err) { std::ostringstream o; o << "HTTP " << http_code << ": " << response; *err = o.str(); }
        return std::nullopt;
    }
    try {
        return json::parse(response);
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return std::nullopt;
    }
}

// Helper: to lower
static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

std::vector<themis::PolicyEngine::Policy>
RangerClient::convertFromRanger(const json& rangerJson) {
    std::vector<themis::PolicyEngine::Policy> out;
    auto pushPolicies = [&](const json& items, const json& resources, bool effect_allow){
        // Derive resource prefixes (path resource typical)
        std::vector<std::string> resource_prefixes;
        if (resources.contains("path")) {
            const auto& path = resources["path"];
            if (path.contains("value") && path["value"].is_string()) {
                resource_prefixes.push_back(path["value"].get<std::string>());
            }
            if (path.contains("values") && path["values"].is_array()) {
                for (const auto& v : path["values"]) if (v.is_string()) resource_prefixes.push_back(v.get<std::string>());
            }
        }
        // Fallback to empty (root) if none
        if (resource_prefixes.empty()) resource_prefixes.push_back("/");

        // Aggregate across items: build one policy per item for simplicity
        if (items.is_array()) {
            for (const auto& it : items) {
                themis::PolicyEngine::Policy p;
                p.id = "ranger-" + std::to_string(out.size()+1);
                p.name = it.value("itemName", std::string("ranger-policy-item"));
                p.effect_allow = effect_allow;
                // subjects: users (groups not supported yet)
                if (it.contains("users") && it["users"].is_array()) {
                    for (const auto& u : it["users"]) if (u.is_string()) p.subjects.insert(u.get<std::string>());
                }
                // actions: from accesses[].type
                if (it.contains("accesses") && it["accesses"].is_array()) {
                    for (const auto& a : it["accesses"]) {
                        if (a.contains("type") && a["type"].is_string()) {
                            auto t = lower(a["type"].get<std::string>());
                            p.actions.insert(t);
                        }
                    }
                }
                // resources
                p.resources = resource_prefixes;
                out.push_back(std::move(p));
            }
        }
    };

    if (rangerJson.is_array()) {
        for (const auto& pol : rangerJson) {
            json resources = pol.value("resources", json::object());
            if (pol.contains("policyItems")) pushPolicies(pol["policyItems"], resources, true);
            if (pol.contains("denyPolicyItems")) pushPolicies(pol["denyPolicyItems"], resources, false);
        }
    } else if (rangerJson.is_object()) {
        json resources = rangerJson.value("resources", json::object());
        if (rangerJson.contains("policyItems")) pushPolicies(rangerJson["policyItems"], resources, true);
        if (rangerJson.contains("denyPolicyItems")) pushPolicies(rangerJson["denyPolicyItems"], resources, false);
    }
    return out;
}

json RangerClient::convertToRanger(const std::vector<themis::PolicyEngine::Policy>& policies,
                                   const std::string& service_name) {
    // Minimal, not 1:1 with Ranger schema; enough for inspection/testing
    json arr = json::array();
    for (const auto& p : policies) {
        json rp;
        rp["name"] = p.name.empty() ? p.id : p.name;
        rp["service"] = service_name;
        rp["resources"] = json::object();
        if (!p.resources.empty()) {
            rp["resources"]["path"] = json{{"values", p.resources}, {"isRecursive", true}};
        }
        json accesses = json::array();
        for (const auto& a : p.actions) accesses.push_back(json{{"type", a}, {"isAllowed", p.effect_allow}});
        json item;
        item["users"] = json::array();
        for (const auto& u : p.subjects) item["users"].push_back(u);
        item["accesses"] = accesses;
        if (p.effect_allow) rp["policyItems"] = json::array({item});
        else rp["denyPolicyItems"] = json::array({item});
        arr.push_back(std::move(rp));
    }
    return arr;
}

}} // namespace themis::server
