// Vector Search Benchmarks (HNSW / Fallback)
// - efSearch Sweep (Latenz vs. Suchaufwand)
// - Insert-Throughput in Batches

#include <benchmark/benchmark.h>
#include <memory>
#include <random>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/vector_index.h"

using themis::RocksDBWrapper;
using themis::BaseEntity;
using themis::VectorIndexManager;

namespace {

struct VecUtil {
    static std::vector<float> randomVec(int dim, std::mt19937& rng) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> v(dim);
        for (int i = 0; i < dim; ++i) v[i] = dist(rng);
        // L2-Normalisieren für COSINE stabilere Werte
        float s = 0.0f; for (float x : v) s += x * x; s = std::sqrt(std::max(s, 1e-12f));
        for (float& x : v) x /= s;
        return v;
    }
};

struct SearchEnv {
    std::shared_ptr<RocksDBWrapper> db;
    std::shared_ptr<VectorIndexManager> vix;
    int dim = 128;
    size_t N = 20000; // 20k Vektoren für flotte Laufzeiten
    bool ready = false;
    std::vector<std::vector<float>> dataset;

    static SearchEnv& instance() { static SearchEnv env; return env; }

    static std::string padInt(size_t v, int width = 8) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "%0*zu", width, v); return std::string(buf);
    }

    void initOnce() {
        if (ready) return;
        const std::string db_path = "data/themis_bench_vector_search";
        std::error_code ec; std::filesystem::remove_all(db_path, ec);

        RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 128; cfg.block_cache_size_mb = 256;
        cfg.compression_default = "lz4"; cfg.compression_bottommost = "zstd";
        db = std::make_shared<RocksDBWrapper>(cfg);
        if (!db->open()) throw std::runtime_error("Failed to open RocksDB for vector benchmark");

        vix = std::make_shared<VectorIndexManager>(*db);
        auto st = vix->init("chunks", dim, VectorIndexManager::Metric::COSINE, /*M*/16, /*efC*/200, /*ef*/64);
        if (!st.ok) throw std::runtime_error("VectorIndex init failed: " + st.message);

        // Datensatz generieren und einfügen
        dataset.reserve(N);
        std::mt19937 rng(42);
        for (size_t i = 0; i < N; ++i) {
            auto vec = VecUtil::randomVec(dim, rng);
            dataset.push_back(vec);
            BaseEntity e("v_" + padInt(i));
            e.setField("embedding", themis::Value{vec});
            auto rst = vix->addEntity(e);
            if (!rst.ok) throw std::runtime_error("addEntity failed at i=" + std::to_string(i));
        }

        ready = true;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Search benchmark: sweep efSearch; k aus Args
// Args: {efSearch, k}
static void BM_VectorSearch_efSearch(benchmark::State& state) {
    const int ef = static_cast<int>(state.range(0));
    const int k = static_cast<int>(state.range(1));
    auto& env = SearchEnv::instance(); env.initOnce();

    // Kann auch im Fallback Modus (ohne HNSW) laufen; setEfSearch ist no-op dort
    env.vix->setEfSearch(ef);

    std::mt19937 rng(123);
    std::uniform_int_distribution<size_t> pick(0, env.N - 1);

    size_t queries = 0;
    for (auto _ : state) {
        const auto& q = env.dataset[pick(rng)];
        auto [st, res] = env.vix->searchKnn(q, static_cast<size_t>(k));
        if (!st.ok) { state.SkipWithError(st.message.c_str()); break; }
        benchmark::DoNotOptimize(res);
        ++queries;
    }
    state.counters["efSearch"] = ef;
    state.counters["k"] = k;
    state.counters["queries"] = static_cast<double>(queries);
    state.counters["vectors"] = static_cast<double>(env.N);
}

// ---------------------------------------------------------------------------
// Insert benchmark: batch 100 vectors per iteration
// Args: {dim}
static void BM_VectorInsert_Batch100(benchmark::State& state) {
    const int dim = static_cast<int>(state.range(0));
    const std::string db_path = "data/themis_bench_vector_insert";
    std::error_code ec; std::filesystem::remove_all(db_path, ec);

    RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 128; cfg.block_cache_size_mb = 256;
    cfg.compression_default = "lz4"; cfg.compression_bottommost = "zstd";
    RocksDBWrapper db(cfg); if (!db.open()) { state.SkipWithError("RocksDB open failed"); return; }

    VectorIndexManager vix(db);
    auto st = vix.init("chunks", dim, VectorIndexManager::Metric::COSINE, 16, 200, 64);
    if (!st.ok) { state.SkipWithError(st.message.c_str()); return; }

    std::mt19937 rng(321);
    size_t inserted = 0;
    for (auto _ : state) {
        auto batch = db.createWriteBatch();
        for (int i = 0; i < 100; ++i) {
            auto vec = VecUtil::randomVec(dim, rng);
            BaseEntity e("vi_" + std::to_string(inserted + i));
            e.setField("embedding", themis::Value{vec});
            auto rst = vix.addEntity(e, *batch);
            if (!rst.ok) { state.SkipWithError(rst.message.c_str()); break; }
        }
        batch->commit();
        inserted += 100;
    }
    state.counters["inserted"] = static_cast<double>(inserted);
}

// Register
BENCHMARK(BM_VectorSearch_efSearch)
    ->Args({32, 10})
    ->Args({64, 10})
    ->Args({128, 10})
    ->Args({256, 10})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_VectorInsert_Batch100)
    ->Args({64})  // dim=64
    ->Args({128}) // dim=128
    ->Unit(benchmark::kMillisecond);
