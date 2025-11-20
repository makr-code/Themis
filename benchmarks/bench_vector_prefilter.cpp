// Benchmark: Whitelist Prefilter vs. Brute-Force Fallback
// Measures latency improvements of iterative HNSW whitelist prefiltering.
// Configuration keys (config:vector JSON):
//   whitelist_prefilter_enabled (bool)
//   whitelist_initial_factor (int)
//   whitelist_min_candidates (int)
//   whitelist_max_attempts (int)
//   whitelist_growth_factor (double)
//
// We build a dataset of N vectors, then for varying whitelist sizes
// run queries with and without prefilter enabled.

#include <benchmark/benchmark.h>
#include <random>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/vector_index.h"

using themis::RocksDBWrapper;
using themis::BaseEntity;
using themis::VectorIndexManager;

namespace {

std::vector<float> randVec(int dim, std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0f,1.0f);
    std::vector<float> v(dim);
    for(int i=0;i<dim;++i) v[i]=dist(rng);
    // Normalize for COSINE
    float s=0.f; for(float x: v) s+=x*x; s = std::sqrt(std::max(s,1e-12f));
    for(float& x: v) x/=s; return v;
}

struct PrefilterEnv {
    int dim = 128;
    size_t N = 30000; // 30k vectors
    std::vector<std::vector<float>> data;
    std::vector<std::string> pks;
    std::shared_ptr<RocksDBWrapper> db;
    std::shared_ptr<VectorIndexManager> vix;
    bool ready = false;

    static PrefilterEnv& inst(){ static PrefilterEnv e; return e; }

    void init() {
        if(ready) return;
        const std::string path = "data/themis_bench_vector_prefilter";
        std::error_code ec; std::filesystem::remove_all(path, ec);
        RocksDBWrapper::Config cfg; cfg.db_path = path; cfg.memtable_size_mb=128; cfg.block_cache_size_mb=256;
        cfg.compression_default="lz4"; cfg.compression_bottommost="zstd";
        db = std::make_shared<RocksDBWrapper>(cfg); if(!db->open()) throw std::runtime_error("RocksDB open failed");
        vix = std::make_shared<VectorIndexManager>(*db);
        auto st = vix->init("chunks", dim, VectorIndexManager::Metric::COSINE, 16, 200, 64);
        if(!st.ok) throw std::runtime_error("VectorIndex init failed: "+st.message);
        data.reserve(N); pks.reserve(N);
        std::mt19937 rng(777);
        for(size_t i=0;i<N;++i){
            auto vec = randVec(dim, rng);
            data.push_back(vec);
            std::string pk = "pf_" + std::to_string(i);
            pks.push_back(pk);
            BaseEntity e(pk);
            e.setField("embedding", themis::Value{vec});
            auto rst = vix->addEntity(e);
            if(!rst.ok) throw std::runtime_error("addEntity failed at i="+std::to_string(i));
        }
        ready = true;
    }
};

// Helper: Set vector config JSON
void setConfig(RocksDBWrapper& db, bool enabled) {
    nlohmann::json j;
    j["whitelist_prefilter_enabled"] = enabled;
    j["whitelist_initial_factor"] = 3;
    j["whitelist_min_candidates"] = 32;
    j["whitelist_max_attempts"] = 4;
    j["whitelist_growth_factor"] = 2.0;
    auto s = j.dump();
    db.put("config:vector", std::vector<uint8_t>(s.begin(), s.end()));
}

} // namespace

// Benchmark parameters: {whitelist_size, k, prefilter_flag(0/1)}
static void BM_VectorWhitelistPrefilter(benchmark::State& state) {
    size_t whitelistSize = static_cast<size_t>(state.range(0));
    size_t k = static_cast<size_t>(state.range(1));
    bool prefilterOn = (state.range(2) != 0);
    auto& env = PrefilterEnv::inst(); env.init();

    setConfig(*env.db, prefilterOn);

    // Build whitelist (sample of pks)
    whitelistSize = std::min(whitelistSize, env.pks.size());
    std::vector<std::string> whitelist(env.pks.begin(), env.pks.begin() + whitelistSize);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<size_t> pick(0, env.N - 1);

    size_t queries = 0;
    for(auto _ : state) {
        const auto& q = env.data[pick(rng)];
        auto [st, res] = env.vix->searchKnn(q, k, &whitelist);
        if(!st.ok) { state.SkipWithError(st.message.c_str()); break; }
        benchmark::DoNotOptimize(res);
        ++queries;
    }
    state.counters["whitelist"] = static_cast<double>(whitelistSize);
    state.counters["k"] = static_cast<double>(k);
    state.counters["prefilter"] = prefilterOn ? 1.0 : 0.0;
    state.counters["queries"] = static_cast<double>(queries);
}

// Whitelist sizes and prefilter toggle combinations
BENCHMARK(BM_VectorWhitelistPrefilter)
    ->Args({1000, 10, 0})
    ->Args({1000, 10, 1})
    ->Args({5000, 10, 0})
    ->Args({5000, 10, 1})
    ->Args({10000, 10, 0})
    ->Args({10000, 10, 1})
    ->Args({20000, 10, 0})
    ->Args({20000, 10, 1})
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
