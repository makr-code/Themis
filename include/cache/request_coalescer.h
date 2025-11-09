#pragma once

#include <string>
#include <functional>
#include <future>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace themis { namespace cache {

// Minimal Singleflight-Coalescer: API-Skizze (nicht produktionsreif)
class RequestCoalescer {
public:
    struct Result {
        bool success{false};
        std::string data; // JSON payload
        uint64_t version{0};
    };

    template<typename F>
    std::shared_ptr<Result> Do(const std::string& key, F&& f) {
        // Skizze: direkte Ausführung ohne echte Zusammenlegung; Hook für spätere Umsetzung
        auto res = std::make_shared<Result>();
        try {
            auto r = f();
            // Erwartet r: { success, data(json), version }
            res->success = r.success;
            res->data = r.data;
            res->version = r.version;
        } catch (...) { res->success = false; }
        return res;
    }
};

}} // namespace themis::cache
