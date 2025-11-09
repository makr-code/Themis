#pragma once

#include "cache_provider.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <vector>
#include <functional>
#include <atomic>

namespace themis { namespace cache {

// Count-Min Sketch für Frequency Estimation (TinyLFU)
class CountMinSketch {
public:
    CountMinSketch(size_t width = 1024, size_t depth = 4)
        : width_(width), depth_(depth), counters_(depth * width, 0) {}

    void increment(const std::string& key) {
        for (size_t d = 0; d < depth_; ++d) {
            size_t idx = hash(key, d) % width_ + d * width_;
            if (counters_[idx] < 15) ++counters_[idx]; // 4-bit saturating counter
        }
    }

    uint32_t estimate(const std::string& key) const {
        uint32_t minVal = 15;
        for (size_t d = 0; d < depth_; ++d) {
            size_t idx = hash(key, d) % width_ + d * width_;
            if (counters_[idx] < minVal) minVal = counters_[idx];
        }
        return minVal;
    }

    void reset() {
        std::fill(counters_.begin(), counters_.end(), 0);
    }

private:
    size_t hash(const std::string& key, size_t seed) const {
        std::hash<std::string> h;
        return h(key) ^ (seed * 0x9e3779b9);
    }

    size_t width_;
    size_t depth_;
    std::vector<uint8_t> counters_;
};

// TinyLFU-basierte L1-Cache mit Admission Control
class L1TinyLFUCache : public CacheProvider {
public:
    explicit L1TinyLFUCache(size_t capacity = 10000)
        : capacity_(capacity), hits_(0), misses_(0), evictions_(0), admissions_(0) {}

    bool Get(std::string_view key, CacheValue& out) override {
        std::lock_guard<std::mutex> lg(mtx_);
        auto it = map_.find(std::string(key));
        if (it == map_.end()) {
            ++misses_;
            return false;
        }
        ++hits_;
        touch(it);
        sketch_.increment(it->first);
        out = it->second.value;
        return true;
    }

    void Put(std::string_view key, const CacheValue& v, uint64_t /*ttl_ms*/) override {
        std::lock_guard<std::mutex> lg(mtx_);
        const std::string k(key);
        auto it = map_.find(k);
        if (it != map_.end()) {
            it->second.value = v;
            touch(it);
            return;
        }
        // Admission: TinyLFU-Check
        if (map_.size() >= capacity_) {
            if (!shouldAdmit(k)) return; // reject candidate
            evictOne();
        }
        lru_.push_front(k);
        map_.emplace(k, Node{v, lru_.begin()});
        ++admissions_;
    }

    void Invalidate(std::string_view key) override {
        std::lock_guard<std::mutex> lg(mtx_);
        const std::string k(key);
        auto it = map_.find(k);
        if (it == map_.end()) return;
        lru_.erase(it->second.it);
        map_.erase(it);
    }

    struct Stats {
        uint64_t hits{0};
        uint64_t misses{0};
        uint64_t evictions{0};
        uint64_t admissions{0};
        size_t size{0};
        size_t capacity{0};
        double hit_rate() const {
            auto total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };

    Stats getStats() const {
        std::lock_guard<std::mutex> lg(mtx_);
        Stats s;
        s.hits = hits_;
        s.misses = misses_;
        s.evictions = evictions_;
        s.admissions = admissions_;
        s.size = map_.size();
        s.capacity = capacity_;
        return s;
    }

private:
    struct Node {
        CacheValue value;
        std::list<std::string>::iterator it;
    };

    void touch(typename std::unordered_map<std::string, Node>::iterator it) {
        lru_.erase(it->second.it);
        lru_.push_front(it->first);
        it->second.it = lru_.begin();
    }

    bool shouldAdmit(const std::string& candidate) {
        if (lru_.empty()) return true;
        const std::string& victim = lru_.back();
        uint32_t candFreq = sketch_.estimate(candidate);
        uint32_t victimFreq = sketch_.estimate(victim);
        return candFreq > victimFreq; // admit if candidate is hotter
    }

    void evictOne() {
        if (lru_.empty()) return;
        auto back = lru_.back();
        lru_.pop_back();
        map_.erase(back);
        ++evictions_;
    }

    size_t capacity_;
    std::unordered_map<std::string, Node> map_;
    std::list<std::string> lru_;
    CountMinSketch sketch_;
    mutable std::mutex mtx_;
    uint64_t hits_;
    uint64_t misses_;
    uint64_t evictions_;
    uint64_t admissions_;
};

}} // namespace themis::cache
