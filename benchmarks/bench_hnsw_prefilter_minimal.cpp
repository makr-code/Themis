#include <benchmark/benchmark.h>
#include <random>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <numeric>

// hnswlib is header-only in most vcpkg builds
#include <hnswlib/hnswlib.h>

namespace {
struct Dataset {
    int dim;
    int n;
    std::vector<float> data; // row-major, size n*dim
    explicit Dataset(int n_, int dim_, uint32_t seed=123) : dim(dim_), n(n_), data(n_*dim_) {
        std::mt19937 rng(seed);
        std::normal_distribution<float> nd(0.f, 1.f);
        for (int i=0;i<n;i++) {
            float* row = &data[i*dim];
            float norm = 0.f;
            for (int j=0;j<dim;j++) { row[j] = nd(rng); norm += row[j]*row[j]; }
            norm = std::sqrt(std::max(norm, 1e-6f));
            for (int j=0;j<dim;j++) row[j] /= norm; // unit vectors for cosine
        }
    }
    const float* row(int i) const { return &data[i*dim]; }
};

// Build an HNSW index over the dataset
std::unique_ptr<hnswlib::HierarchicalNSW<float>> build_hnsw(const Dataset& ds, int M=16, int efC=200) {
    auto space = std::make_unique<hnswlib::InnerProductSpace>(ds.dim);
    auto index = std::make_unique<hnswlib::HierarchicalNSW<float>>(space.get(), ds.n, M, efC);
    for (int i=0;i<ds.n;i++) index->addPoint((void*)ds.row(i), i);
    index->setEf(200);
    // Keep space alive by moving ownership into index's templated alias workaround
    // We just leak space on purpose here (benchmark process), to keep index valid
    space.release();
    return index;
}

// Prefilter pushdown: iterative growth of ANN candidates until k whitelist hits
static void search_prefilter(const hnswlib::HierarchicalNSW<float>& index,
                             const float* query, int k,
                             const std::unordered_set<int>& whitelist,
                             std::vector<std::pair<float,int>>& out,
                             int init_factor=4, int min_cand=64,
                             int max_attempts=5, double growth=1.7) {
    out.clear();
    out.reserve(k);
    std::unordered_set<int> seen;
    int attempt = 0;
    int cand = std::max(min_cand, k*init_factor);
    while ((int)out.size() < k && attempt < max_attempts) {
        auto res = index.searchKnn(query, std::max(cand, k));
        // results are in max-heap; collect then reverse
        std::vector<std::pair<float,int>> buf; buf.reserve(res.size());
        while(!res.empty()) { buf.emplace_back(res.top().first, (int)res.top().second); res.pop(); }
        std::reverse(buf.begin(), buf.end());
        for (auto& p : buf) {
            int id = p.second;
            if (seen.insert(id).second && whitelist.find(id) != whitelist.end()) {
                out.emplace_back(p.first, id);
                if ((int)out.size() >= k) break;
            }
        }
        cand = (int)std::max<double>(cand+1, cand*growth);
        attempt++;
    }
    if ((int)out.size() < k) {
        // fill remaining by scanning whitelist with a brute-force over query vs ds (not available)
        // For minimal benchmark, leave as is (measures pushdown effectiveness without fallback)
    }
}

// Postfilter baseline: single ANN call, then filter results by whitelist, enlarge candidate count
static void search_postfilter(const hnswlib::HierarchicalNSW<float>& index,
                              const float* query, int k,
                              const std::unordered_set<int>& whitelist,
                              std::vector<std::pair<float,int>>& out,
                              int factor=12) {
    out.clear();
    int cand = std::max(k*factor, k);
    auto res = index.searchKnn(query, cand);
    std::vector<std::pair<float,int>> buf; buf.reserve(res.size());
    while(!res.empty()) { buf.emplace_back(res.top().first, (int)res.top().second); res.pop(); }
    std::reverse(buf.begin(), buf.end());
    for (auto& p : buf) {
        if ((int)out.size() >= k) break;
        if (whitelist.find(p.second) != whitelist.end()) out.emplace_back(p.first, p.second);
    }
}

// Benchmark harness
static void BenchPrefilter(benchmark::State& state) {
    const int N = 50000;
    const int D = 128;
    const int K = 10;
    const int whitelist_size = static_cast<int>(state.range(0));

    Dataset ds(N, D, 1337);
    auto index = build_hnsw(ds, 16, 200);

    // Build whitelist as first X ids, shuffled for realism
    std::vector<int> ids(N); std::iota(ids.begin(), ids.end(), 0);
    std::mt19937 rng(42); std::shuffle(ids.begin(), ids.end(), rng);
    std::unordered_set<int> whitelist; whitelist.reserve(whitelist_size*2);
    for (int i=0;i<whitelist_size;i++) whitelist.insert(ids[i]);

    std::vector<std::pair<float,int>> out;
    std::vector<int> qidx(256);
    std::iota(qidx.begin(), qidx.end(), 0);
    std::shuffle(qidx.begin(), qidx.end(), rng);

    for (auto _ : state) {
        int acc = 0;
        for (int qi=0; qi<(int)qidx.size(); ++qi) {
            search_prefilter(*index, ds.row(qidx[qi]), K, whitelist, out);
            acc += (int)out.size();
        }
        benchmark::DoNotOptimize(acc);
    }
    state.SetItemsProcessed(state.iterations() * (int)qidx.size());
}

static void BenchPostfilter(benchmark::State& state) {
    const int N = 50000;
    const int D = 128;
    const int K = 10;
    const int whitelist_size = static_cast<int>(state.range(0));

    Dataset ds(N, D, 1337);
    auto index = build_hnsw(ds, 16, 200);

    std::vector<int> ids(N); std::iota(ids.begin(), ids.end(), 0);
    std::mt19937 rng(43); std::shuffle(ids.begin(), ids.end(), rng);
    std::unordered_set<int> whitelist; whitelist.reserve(whitelist_size*2);
    for (int i=0;i<whitelist_size;i++) whitelist.insert(ids[i]);

    std::vector<std::pair<float,int>> out;
    std::vector<int> qidx(256);
    std::iota(qidx.begin(), qidx.end(), 0);
    std::shuffle(qidx.begin(), qidx.end(), rng);

    for (auto _ : state) {
        int acc = 0;
        for (int qi=0; qi<(int)qidx.size(); ++qi) {
            search_postfilter(*index, ds.row(qidx[qi]), K, whitelist, out);
            acc += (int)out.size();
        }
        benchmark::DoNotOptimize(acc);
    }
    state.SetItemsProcessed(state.iterations() * (int)qidx.size());
}

BENCHMARK(BenchPrefilter)->Arg(1000)->Arg(5000)->Arg(10000)->Arg(20000);
BENCHMARK(BenchPostfilter)->Arg(1000)->Arg(5000)->Arg(10000)->Arg(20000);

} // namespace
